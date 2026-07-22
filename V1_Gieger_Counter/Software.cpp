// Counts per minute Geiger Counter
// Arduino UNO
// Signal wire connected to D2

unsigned long counts = 0;          // Variable for GM tube events
unsigned long previousMillis = 0;  // Timer variable

void impulse() {
  counts++; // Count each radiation pulse
}

#define LOG_PERIOD 60000 // 1 minute (60000 milliseconds)

void setup() {
  counts = 0;

  Serial.begin(9600);

  pinMode(2, INPUT);

  // D2 is interrupt pin on Arduino UNO
  attachInterrupt(digitalPinToInterrupt(2), impulse, FALLING);

  Serial.println("Start counter");
}

void loop() {

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis > LOG_PERIOD) {

    previousMillis = currentMillis;

    Serial.print("CPM: ");
    Serial.println(counts);

    counts = 0;
  }
}
