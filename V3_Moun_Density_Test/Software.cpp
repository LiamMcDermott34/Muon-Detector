#include <LiquidCrystal.h>
#include <SPI.h>
#include <SD.h>

// --- Pin Definitions ---
const int geiger1Pin = 2;  // Interrupt 0
const int geiger2Pin = 3;  // Interrupt 1
const int buttonPin = A0;  // Start button
const int sdCSPin = 10;    // SD Card Chip Select

// LCD Pins: RS=9, EN=8, D4=4, D5=5, D6=6, D7=7
LiquidCrystal lcd(9, 8, 4, 5, 6, 7);

// --- State Management ---
enum State { WAITING, INIT, RUNNING, FINISHED };
State currentState = WAITING;

// --- Timing Variables ---
unsigned long startTime = 0;
unsigned long lastScreenUpdate = 0;
unsigned long lastLogTime = 0;
const unsigned long experimentDuration = 86400000UL; // 24 hours in milliseconds
const unsigned long screenCycleTime = 3000;          // 3 seconds per screen
const unsigned long logInterval = 60000;             // Log to SD every 60 seconds
int currentScreen = 0;

// --- Geiger Counter Variables (Volatile for Interrupts) ---
volatile unsigned long counts1 = 0;
volatile unsigned long counts2 = 0;
volatile unsigned long coincidences = 0;

volatile unsigned long lastPulse1 = 0;
volatile unsigned long lastPulse2 = 0;
const unsigned long coincidenceWindow = 50; // Microseconds window for a coincidence

// --- SD Card Variables ---
char filename[] = "DATA00.CSV";

void setup() {
  Serial.begin(9600);
  
  // Initialize LCD
  lcd.begin(16, 2);
  
  // Initialize Pins
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(geiger1Pin, INPUT);
  pinMode(geiger2Pin, INPUT);
  pinMode(sdCSPin, OUTPUT);

  // Attach Interrupts for Geiger Counters
  attachInterrupt(digitalPinToInterrupt(geiger1Pin), isrGeiger1, FALLING);
  attachInterrupt(digitalPinToInterrupt(geiger2Pin), isrGeiger2, FALLING);

  drawHomeScreen();
}

void loop() {
  switch (currentState) {
    case WAITING:
      waitForStartButton();
      break;
      
    case INIT:
      initExperiment();
      break;
      
    case RUNNING:
      runExperiment();
      break;
      
    case FINISHED:
      finishExperiment();
      break;
  }
}

// --- Interrupt Service Routines ---
void isrGeiger1() {
  unsigned long now = micros();
  counts1++;
  if (now - lastPulse2 <= coincidenceWindow) {
    coincidences++;
  }
  lastPulse1 = now;
}

void isrGeiger2() {
  unsigned long now = micros();
  counts2++;
  if (now - lastPulse1 <= coincidenceWindow) {
    coincidences++;
  }
  lastPulse2 = now;
}

// --- Functions ---

void drawHomeScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(" MUON DETECTOR  ");
  lcd.setCursor(0, 1);
  lcd.print("  Press START   ");
}

void waitForStartButton() {
  // Check if button is pressed (LOW due to pullup)
  if (digitalRead(buttonPin) == LOW) {
    delay(50); // Debounce
    if (digitalRead(buttonPin) == LOW) {
      currentState = INIT;
    }
  }
}

void initExperiment() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  lcd.setCursor(0, 1);
  lcd.print("Please Wait");
  
  // Initialize SD Card
  if (!SD.begin(sdCSPin)) {
    lcd.clear();
    lcd.print("SD Init Failed!");
    while (1); // Halt if SD fails
  }

  // Find next available file name (DATA00.CSV to DATA99.CSV)
  for (uint8_t i = 0; i < 100; i++) {
    filename[4] = i / 10 + '0';
    filename[5] = i % 10 + '0';
    if (!SD.exists(filename)) {
      // Create and write header
      File dataFile = SD.open(filename, FILE_WRITE);
      if (dataFile) {
        dataFile.println("Time(ms),D1_Counts,D2_Counts,Assumed_Moun");
        dataFile.close();
      }
      break;
    }
  }

  // Reset variables
  counts1 = 0;
  counts2 = 0;
  coincidences = 0;
  startTime = millis();
  lastScreenUpdate = millis();
  lastLogTime = millis();
  
  currentState = RUNNING;
}

void runExperiment() {
  unsigned long currentTime = millis();
  unsigned long elapsedTime = currentTime - startTime;

  // Check if 24 hours have passed
  if (elapsedTime >= experimentDuration) {
    // Log final data point before finishing
    logToSD(elapsedTime);
    currentState = FINISHED;
    return;
  }

  // Cycle LCD screens every 3 seconds
  if (currentTime - lastScreenUpdate >= screenCycleTime) {
    lastScreenUpdate = currentTime;
    currentScreen = (currentScreen + 1) % 4;
    updateDisplay(elapsedTime);
  }

  // Log to SD card every interval (60 seconds)
  if (currentTime - lastLogTime >= logInterval) {
    lastLogTime = currentTime;
    logToSD(elapsedTime);
  }
}

void updateDisplay(unsigned long elapsedTime) {
  lcd.clear();
  
  // Safely read volatile variables
  noInterrupts();
  unsigned long safeC1 = counts1;
  unsigned long safeC2 = counts2;
  unsigned long safeCoin = coincidences;
  interrupts();

  if (currentScreen == 0) {
    // Screen 1: Timer
    lcd.setCursor(0, 0);
    lcd.print("Running");
    lcd.setCursor(0, 1);
    
    unsigned long totalSeconds = elapsedTime / 1000;
    unsigned long hours = totalSeconds / 3600;
    unsigned long mins = (totalSeconds % 3600) / 60;
    unsigned long secs = totalSeconds % 60;
    
    char timeStr[9];
    sprintf(timeStr, "%02lu:%02lu:%02lu", hours, mins, secs);
    lcd.print(timeStr);
    
  } else if (currentScreen == 1) {
    // Screen 2: Progress Bar
    lcd.setCursor(0, 0);
    lcd.print("Progress");
    
    int percent = (int)((elapsedTime * 100) / experimentDuration);
    int bars = (percent * 10) / 100; // 0 to 10 bars
    
    lcd.setCursor(0, 1);
    if (percent < 10) lcd.print("  ");
    else if (percent < 100) lcd.print(" ");
    lcd.print(percent);
    lcd.print("% ");
    
    for (int i = 0; i < bars; i++) {
      lcd.print("#");
    }
    
  } else if (currentScreen == 2) {
    // Screen 3: Counts
    lcd.setCursor(0, 0);
    lcd.print("Counts");
    lcd.setCursor(0, 1);
    lcd.print("D1:");
    lcd.print(safeC1);
    lcd.print(" D2:");
    lcd.print(safeC2);
    
  } else if (currentScreen == 3) {
    // Screen 4: Coincidences
    lcd.setCursor(0, 0);
    lcd.print("Assumed Moun");
    lcd.setCursor(0, 1);
    lcd.print(safeCoin);
  }
}

void logToSD(unsigned long elapsedTime) {
  // Safely read volatile variables
  noInterrupts();
  unsigned long safeC1 = counts1;
  unsigned long safeC2 = counts2;
  unsigned long safeCoin = coincidences;
  interrupts();

  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile) {
    dataFile.print(elapsedTime);
    dataFile.print(",");
    dataFile.print(safeC1);
    dataFile.print(",");
    dataFile.print(safeC2);
    dataFile.print(",");
    dataFile.println(safeCoin);
    dataFile.close();
  }
}

void finishExperiment() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Experiment");
  lcd.setCursor(0, 1);
  lcd.print("Complete!");
  
  // Halt execution in an infinite loop
  while (true) {
    delay(1000);
  }
}
