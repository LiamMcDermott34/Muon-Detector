#include <SPI.h>
#include <SD.h>


// SD card
const int chipSelect = 10;


// Geiger detector pins
const byte detector1Pin = 2;
const byte detector2Pin = 3;


// Counts
volatile unsigned long detector1Count = 0;
volatile unsigned long detector2Count = 0;


// Timing
unsigned long lastSecond = 0;
unsigned long secondsElapsed = 0;


// File
File dataFile;
char filename[13];


// Interrupt functions
void detector1Pulse() {
  detector1Count++;
}


void detector2Pulse() {
  detector2Count++;
}


// Find a new filename
void createNewFilename() {

  for (int i = 1; i < 1000; i++) {

    sprintf(filename, "G%03d.CSV", i);

    if (!SD.exists(filename)) {
      return;
    }
  }

  Serial.println("No filenames available");
  while (1);
}



void setup() {

  Serial.begin(9600);

  Serial.println("Starting Geiger Logger");


  // Setup detectors
  pinMode(detector1Pin, INPUT);
  pinMode(detector2Pin, INPUT);


  attachInterrupt(
    digitalPinToInterrupt(detector1Pin),
    detector1Pulse,
    FALLING
  );


  attachInterrupt(
    digitalPinToInterrupt(detector2Pin),
    detector2Pulse,
    FALLING
  );


  // Start SD card
  Serial.println("Starting SD card");


  if (!SD.begin(chipSelect)) {

    Serial.println("SD initialization failed");
    while (1);

  }


  Serial.println("SD OK");


  // Create new file name
  createNewFilename();


  Serial.print("Creating file: ");
  Serial.println(filename);


  dataFile = SD.open(filename, FILE_WRITE);


  if (!dataFile) {

    Serial.println("File creation failed");
    while (1);

  }


  // CSV header
  dataFile.println(
    "Time_s,D1_Count,D2_Count,Both_Fired"
  );


  dataFile.close();


  Serial.println("Logging started");

}




void loop() {


  // Run every second
  if (millis() - lastSecond >= 1000) {


    lastSecond += 1000;

    secondsElapsed++;


    // Copy counts safely
    noInterrupts();

    unsigned long d1 = detector1Count;
    unsigned long d2 = detector2Count;

    detector1Count = 0;
    detector2Count = 0;

    interrupts();



    int bothFired = 0;

    if (d1 > 0 && d2 > 0) {
      bothFired = 1;
    }



    // Serial output
    Serial.print("Time: ");
    Serial.print(secondsElapsed);

    Serial.print("  D1: ");
    Serial.print(d1);

    Serial.print("  D2: ");
    Serial.print(d2);

    Serial.print("  Both: ");
    Serial.println(bothFired);



    // Write to SD
    dataFile = SD.open(filename, FILE_WRITE);


    if (dataFile) {

      dataFile.print(secondsElapsed);
      dataFile.print(",");

      dataFile.print(d1);
      dataFile.print(",");

      dataFile.print(d2);
      dataFile.print(",");

      dataFile.println(bothFired);


      dataFile.close();

    }
    else {

      Serial.println("SD write error");

    }

  }

}
