#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "RTClib.h"
#include <LiquidCrystal_I2C.h>

// ==========================================
// PIN DEFINITIONS
// ==========================================
#define PIN_G1_SIG      34
#define PIN_G2_SIG      35
#define PIN_START_BTN   27
#define PIN_SD_CS       5
// I2C: SDA=21, SCL=22 (Default ESP32 hardware I2C)
// SPI: MOSI=23, MISO=19, SCK=18 (Default ESP32 VSPI)

// ==========================================
// SETTINGS
// ==========================================
// Max time (in microseconds) between tube pulses to count as a muon coincidence.
// DIY Geiger circuits have some latency; 1000us (1ms) is a safe starting point.
const unsigned long COINCIDENCE_WINDOW_US = 1000; 

// ==========================================
// OBJECTS
// ==========================================
RTC_DS3231 rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4); // Address 0x27 is common, change to 0x3F if it doesn't work

// ==========================================
// VOLATILE VARIABLES FOR INTERRUPTS
// ==========================================
// Interval counters (reset every 5 mins)
volatile unsigned long int_d1_counts = 0;
volatile unsigned long int_d2_counts = 0;
volatile unsigned long int_muon_counts = 0;

// Daily counters (reset at midnight)
volatile unsigned long daily_d1_counts = 0;
volatile unsigned long daily_d2_counts = 0;
volatile unsigned long daily_muon_counts = 0;

// Timestamps for coincidence detection
volatile unsigned long last_g1_micros = 0;
volatile unsigned long last_g2_micros = 0;
volatile unsigned long last_muon_micros = 0;

// ==========================================
// GLOBAL STATE VARIABLES
// ==========================================
bool isWaitingForSync = true;
int currentDay = -1;
unsigned long lastLcdUpdate = 0;
int lcdScreenState = 0; // 0 = Interval data, 1 = Daily data

// ==========================================
// INTERRUPT SERVICE ROUTINES (ISR)
// ==========================================
void IRAM_ATTR isr_G1() {
  unsigned long now_us = micros();
  int_d1_counts++;
  daily_d1_counts++;
  
  // Check for coincidence
  if (now_us - last_g2_micros <= COINCIDENCE_WINDOW_US) {
    if (now_us - last_muon_micros > COINCIDENCE_WINDOW_US) { // Prevent double counting
      int_muon_counts++;
      daily_muon_counts++;
      last_muon_micros = now_us;
    }
  }
  last_g1_micros = now_us;
}

void IRAM_ATTR isr_G2() {
  unsigned long now_us = micros();
  int_d2_counts++;
  daily_d2_counts++;
  
  // Check for coincidence
  if (now_us - last_g1_micros <= COINCIDENCE_WINDOW_US) {
    if (now_us - last_muon_micros > COINCIDENCE_WINDOW_US) {
      int_muon_counts++;
      daily_muon_counts++;
      last_muon_micros = now_us;
    }
  }
  last_g2_micros = now_us;
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Init LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Muon Detector Boot");

  // Init Start Button
  pinMode(PIN_START_BTN, INPUT_PULLUP);

  // 1. Check RTC
  lcd.setCursor(0, 1);
  if (!rtc.begin()) {
    lcd.print("RTC: FAILED! Halt.");
    while (1);
  }
  lcd.print("RTC: OK");
  
  if (rtc.lostPower()) {
    // If battery died, set to compile time
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // 2. Check SD Card
  lcd.setCursor(0, 2);
  if (!SD.begin(PIN_SD_CS)) {
    lcd.print("SD Card: FAILED! Hlt");
    while (1);
  }
  lcd.print("SD Card: OK");

  delay(2000);
  lcd.clear();

  // 3. Wait for START Button
  lcd.setCursor(0, 0);
  lcd.print("System Ready.");
  lcd.setCursor(0, 2);
  lcd.print("Press START Button");
  lcd.setCursor(0, 3);
  lcd.print("to begin detection..");

  while (digitalRead(PIN_START_BTN) == HIGH) {
    delay(50); // Wait for button press (LOW)
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Starting...");
  delay(1000);
  lcd.clear();

  // 4. Attach Interrupts
  pinMode(PIN_G1_SIG, INPUT_PULLUP); // Assuming standard open-collector/active-low Geiger output
  pinMode(PIN_G2_SIG, INPUT_PULLUP);
  // NOTE: If your Geiger outputs are active-high, change FALLING to RISING
  attachInterrupt(digitalPinToInterrupt(PIN_G1_SIG), isr_G1, FALLING); 
  attachInterrupt(digitalPinToInterrupt(PIN_G2_SIG), isr_G2, FALLING);

  DateTime now = rtc.now();
  currentDay = now.day();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  DateTime now = rtc.now();

  // Handle midnight reset
  if (now.day() != currentDay) {
    noInterrupts();
    daily_d1_counts = 0;
    daily_d2_counts = 0;
    daily_muon_counts = 0;
    interrupts();
    currentDay = now.day();
  }

  // State Machine Logic
  if (isWaitingForSync) {
    // Waiting for a time perfectly divisible by 5 (e.g. 12:00:00, 12:05:00)
    if (now.minute() % 5 == 0 && now.second() == 0) {
      // Time is aligned! Reset interval counters and start the run
      noInterrupts();
      int_d1_counts = 0;
      int_d2_counts = 0;
      int_muon_counts = 0;
      interrupts();
      
      isWaitingForSync = false; 
      delay(1000); // Wait 1 second so we don't double-trigger on the 0th second
    }
  } 
  else {
    // We are currently actively recording a 5-minute block.
    // Check if the 5 minutes are up
    if (now.minute() % 5 == 0 && now.second() == 0) {
      saveDataToSD(now);
      
      // Reset counters for the next 5 mins
      noInterrupts();
      int_d1_counts = 0;
      int_d2_counts = 0;
      int_muon_counts = 0;
      interrupts();
      
      delay(1000); // Prevent double-triggering
    }
  }

  // Update LCD every 2.5 seconds
  if (millis() - lastLcdUpdate > 2500) {
    updateLCD(now);
    lastLcdUpdate = millis();
  }
}

// ==========================================
// HELPER FUNCTIONS
// ==========================================

void saveDataToSD(DateTime t) {
  // Disable interrupts briefly to grab clean copy of counters
  noInterrupts();
  unsigned long d1 = int_d1_counts;
  unsigned long d2 = int_d2_counts;
  unsigned long muons = int_muon_counts;
  interrupts();

  // Create filename: YYYY-MM-DD.csv (ESP32 SD supports long names)
  char filename[16];
  snprintf(filename, sizeof(filename), "/%04d-%02d-%02d.csv", t.year(), t.month(), t.day());

  // Check if file exists to determine if we need headers
  bool fileExists = SD.exists(filename);
  
  File dataFile = SD.open(filename, FILE_APPEND);
  if (dataFile) {
    if (!fileExists) {
      dataFile.println("Date,Time,D1_Counts,D2_Counts,Coincidences");
    }
    
    // Format: YYYY-MM-DD,HH:MM:SS,D1,D2,Muons
    char dataString[64];
    snprintf(dataString, sizeof(dataString), "%04d-%02d-%02d,%02d:%02d:%02d,%lu,%lu,%lu", 
             t.year(), t.month(), t.day(),
             t.hour(), t.minute(), t.second(),
             d1, d2, muons);
             
    dataFile.println(dataString);
    dataFile.close();
    Serial.println(dataString);
  } else {
    Serial.println("Error opening file for writing!");
  }
}

void updateLCD(DateTime t) {
  // Grab safe copies of volatiles
  noInterrupts();
  unsigned long i_d1 = int_d1_counts;
  unsigned long i_d2 = int_d2_counts;
  unsigned long i_muons = int_muon_counts;
  unsigned long d_d1 = daily_d1_counts;
  unsigned long d_d2 = daily_d2_counts;
  unsigned long d_muons = daily_muon_counts;
  interrupts();

  char timeStr[9];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", t.hour(), t.minute(), t.second());

  lcd.clear();

  if (isWaitingForSync) {
    lcd.setCursor(0, 0);
    lcd.print("Syncing Clock...");
    lcd.setCursor(0, 1);
    lcd.print("Time: "); lcd.print(timeStr);
    lcd.setCursor(0, 3);
    lcd.print("Waiting for XX:05...");
  } 
  else {
    // Cycle between Screen 0 (5-min interval) and Screen 1 (Daily stats)
    if (lcdScreenState == 0) {
      lcd.setCursor(0, 0);
      lcd.print("--- 5 MIN DATA ---");
      lcd.setCursor(0, 1);
      lcd.print("Time: "); lcd.print(timeStr);
      lcd.setCursor(0, 2);
      lcd.print("G1: "); lcd.print(i_d1);
      lcd.print(" G2: "); lcd.print(i_d2);
      lcd.setCursor(0, 3);
      lcd.print("MUONS DETECTED: "); lcd.print(i_muons);
    } 
    else {
      char dateStr[11];
      snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", t.year(), t.month(), t.day());
      
      lcd.setCursor(0, 0);
      lcd.print("--- DAILY TOTAL ---");
      lcd.setCursor(0, 1);
      lcd.print("Date: "); lcd.print(dateStr);
      lcd.setCursor(0, 2);
      lcd.print("G1: "); lcd.print(d_d1);
      lcd.setCursor(10, 2);
      lcd.print("G2: "); lcd.print(d_d2);
      lcd.setCursor(0, 3);
      lcd.print("TOTAL MUONS: "); lcd.print(d_muons);
    }
    
    // Toggle screen for next cycle
    lcdScreenState = !lcdScreenState;
  }
}
