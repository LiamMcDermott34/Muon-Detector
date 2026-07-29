Here is the fully combined, unified script for your ESP32 muon detector. It incorporates everything:

* **Freenove I2C LCD** (configured for 20x4, can be easily changed to 16x2 on line 12 if needed)
* **Wi-Fi & NTP Time Sync** (automatically grabs the precise clock at startup without needing a broken physical RTC module)
* **MicroSD Card Logging** (saves data with accurate timestamps, elapsed time, detector counts, and coincidences to `/DATA00.CSV` with auto-incrementing file names)
* **Precise Geiger Counter Interrupts** (uses `IRAM_ATTR` and a $50\mu s$ coincidence window on GPIO 34 and 35)
* **Start Button & State Machine** (waits for a button press on GPIO 27 to initialize the run)

```cpp
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <LiquidCrystal_I2C.h>

// --- Wi-Fi Credentials (Change to your network) ---
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// --- NTP Server Settings ---
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long  gmtOffset_sec = -18000;   // Adjust for your timezone (e.g., -18000 for EST)
const int   daylightOffset_sec = 3600; // Adjust for daylight saving time (0 if none)

// --- Pin Definitions (ESP32) ---
const int geiger1Pin = 34;   // Input-only pin
const int geiger2Pin = 35;   // Input-only pin
const int buttonPin = 27;    // Start button (active low with internal pull-up)
const int sdCSPin = 5;       // SD Card Chip Select

// --- LCD Initialization (Freenove I2C 20x4 - change to 16, 2 if using a 16x2) ---
LiquidCrystal_I2C lcd(0x27, 20, 4);

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

// --- Geiger Counter & Coincidence Variables (Volatile for ISR) ---
volatile unsigned long counts1 = 0;
volatile unsigned long counts2 = 0;
volatile unsigned long coincidences = 0;

volatile unsigned long lastPulse1 = 0;
volatile unsigned long lastPulse2 = 0;
const unsigned long coincidenceWindow = 50; // Microseconds window for a coincidence

// --- SD Card Variables ---
char filename[] = "/DATA00.CSV";

// --- FreeRTOS Mutex for Thread Safety ---
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// --- Function Prototypes ---
void IRAM_ATTR isrGeiger1();
void IRAM_ATTR isrGeiger2();
void drawHomeScreen();
void waitForStartButton();
void initExperiment();
void runExperiment();
void updateDisplay(unsigned long elapsedTime);
void logToSD(unsigned long elapsedTime);
void finishExperiment();

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C and LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  
  lcd.clear();
  lcd.print("Connecting WiFi...");

  // Connect to Wi-Fi to sync time via NTP
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 40) { // Timeout after 20 seconds
      Serial.println("\nWiFi Failed! Proceeding with internal timer.");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    lcd.clear();
    lcd.print("Syncing Time...");
    
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.println("Time synced via NTP!");
      lcd.clear();
      lcd.print("Time Synced!");
      delay(1200);
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

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
void IRAM_ATTR isrGeiger1() {
  unsigned long now = micros();
  counts1++;
  if (now - lastPulse2 <= coincidenceWindow) {
    coincidences++;
  }
  lastPulse1 = now;
}

void IRAM_ATTR isrGeiger2() {
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
  lcd.print("   MUON DETECTOR    ");
  lcd.setCursor(0, 2);
  lcd.print("  Press START Button ");
}

void waitForStartButton() {
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
  
  // Initialize SD Card (Explicitly setting SPI pins: SCK=18, MISO=19, MOSI=23, SS=5)
  SPI.begin(18, 19, 23, sdCSPin);
  if (!SD.begin(sdCSPin)) {
    lcd.clear();
    lcd.print("SD Init Failed!");
    while (1); 
  }

  // Find next available file name (/DATA00.CSV to /DATA99.CSV)
  for (uint8_t i = 0; i < 100; i++) {
    filename[5] = i / 10 + '0';
    filename[6] = i % 10 + '0';
    if (!SD.exists(filename)) {
      File dataFile = SD.open(filename, FILE_WRITE);
      if (dataFile) {
        dataFile.println("Date,Time,Elapsed(ms),D1_Counts,D2_Counts,Coincidences");
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

  if (elapsedTime >= experimentDuration) {
    logToSD(elapsedTime);
    currentState = FINISHED;
    return;
  }

  if (currentTime - lastScreenUpdate >= screenCycleTime) {
    lastScreenUpdate = currentTime;
    currentScreen = (currentScreen + 1) % 4;
    updateDisplay(elapsedTime);
  }

  if (currentTime - lastLogTime >= logInterval) {
    lastLogTime = currentTime;
    logToSD(elapsedTime);
  }
}

void updateDisplay(unsigned long elapsedTime) {
  lcd.clear();
  
  portENTER_CRITICAL(&mux);
  unsigned long safeC1 = counts1;
  unsigned long safeC2 = counts2;
  unsigned long safeCoin = coincidences;
  portEXIT_CRITICAL(&mux);

  if (currentScreen == 0) {
    // Screen 1: Timer
    lcd.setCursor(0, 0);
    lcd.print("Status: RUNNING");
    lcd.setCursor(0, 1);
    lcd.print("Elapsed Time:");
    
    unsigned long totalSeconds = elapsedTime / 1000;
    unsigned long hours = totalSeconds / 3600;
    unsigned long mins = (totalSeconds % 3600) / 60;
    unsigned long secs = totalSeconds % 60;
    
    char timeStr[9];
    sprintf(timeStr, "%02lu:%02lu:%02lu", hours, mins, secs);
    lcd.setCursor(0, 2);
    lcd.print(timeStr);
    
  } else if (currentScreen == 1) {
    // Screen 2: Progress
    lcd.setCursor(0, 0);
    lcd.print("Experiment Progress");
    
    int percent = (int)((elapsedTime * 100) / experimentDuration);
    int bars = (percent * 10) / 100; // 0 to 10 bars
    
    lcd.setCursor(0, 1);
    lcd.print(percent);
    lcd.print("% ");
    
    lcd.setCursor(0, 2);
    for (int i = 0; i < bars; i++) {
      lcd.print("#");
    }
    
  } else if (currentScreen == 2) {
    // Screen 3: Individual Counts
    lcd.setCursor(0, 0);
    lcd.print("Detector Counts");
    lcd.setCursor(0, 1);
    lcd.print("Detector 1: ");
    lcd.print(safeC1);
    lcd.setCursor(0, 2);
    lcd.print("Detector 2: ");
    lcd.print(safeC2);
    
  } else if (currentScreen == 3) {
    // Screen 4: Coincidences / Muons
    lcd.setCursor(0, 0);
    lcd.print("Muon Detection");
    lcd.setCursor(0, 1);
    lcd.print("Coincidences:");
    lcd.setCursor(0, 2);
    lcd.print(safeCoin);
  }
}

void logToSD(unsigned long elapsedTime) {
  portENTER_CRITICAL(&mux);
  unsigned long safeC1 = counts1;
  unsigned long safeC2 = counts2;
  unsigned long safeCoin = coincidences;
  portEXIT_CRITICAL(&mux);

  struct tm timeinfo;
  File dataFile = SD.open(filename, FILE_APPEND);
  
  if (dataFile) {
    if (getLocalTime(&timeinfo)) {
      dataFile.printf("%04d/%02d/%02d,%02d:%02d:%02d,", 
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, 
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
      dataFile.print("N/A,N/A,");
    }

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
  
  while (true) {
    delay(1000);
  }
}

```
