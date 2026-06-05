#include <Arduino.h>

// Bench-only SeaTalk interface electrical test for Arduino Due.
//
// This does not speak SeaTalk. It only holds the Due TX1/SN74LS07 input
// high or low so a multimeter can verify the external DATA line.
//
// Wiring expected by dueseatalk1:
//   Due TX1 pin 18 -> SN74LS07 input
//   SN74LS07 open-collector output -> fake SeaTalk DATA
//   Fake SeaTalk DATA -> PC817 RX input circuit
//   PC817 output/RX node -> Due RX1 pin 19 and Due digital pin 22

constexpr uint32_t kBaud = 115200;
constexpr int kSeatalkTxPin = 18;
constexpr int kSeatalkRxPin = 19;
constexpr int kIdleSensePin = 22;

char line[48];
size_t lineLen = 0;
bool autoPulse = false;
uint32_t lastPulseMs = 0;
bool pulseLow = false;

void releaseDataLine() {
  // SN74LS07 is a non-inverting open-collector buffer:
  // Due HIGH releases the external DATA line to its pull-up.
  digitalWrite(kSeatalkTxPin, HIGH);
}

void pullDataLineLow() {
  // Due LOW makes the SN74LS07 sink the external DATA line.
  digitalWrite(kSeatalkTxPin, LOW);
}

void printReadback() {
  Serial.print("READ:RX1_PIN_19=");
  Serial.print(digitalRead(kSeatalkRxPin) == HIGH ? "HIGH" : "LOW");
  Serial.print(";SENSE_PIN_22=");
  Serial.println(digitalRead(kIdleSensePin) == HIGH ? "HIGH" : "LOW");
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  RELEASE  - release DATA line to pull-up");
  Serial.println("  LOW      - pull DATA line low");
  Serial.println("  READ     - print Due RX/sense pin levels");
  Serial.println("  PULSE    - pull low for 1000 ms, then release");
  Serial.println("  AUTO     - toggle low/release every 2000 ms");
  Serial.println("  STOP     - stop AUTO and release DATA line");
}

void handleLine(char* cmd) {
  while (*cmd == ' ' || *cmd == '\t') {
    ++cmd;
  }

  for (char* p = cmd; *p; ++p) {
    if (*p >= 'a' && *p <= 'z') {
      *p = static_cast<char>(*p - 'a' + 'A');
    }
  }

  if (strcmp(cmd, "RELEASE") == 0 || strcmp(cmd, "HIGH") == 0) {
    autoPulse = false;
    releaseDataLine();
    Serial.println("TX:RELEASED");
    printReadback();
  } else if (strcmp(cmd, "LOW") == 0) {
    autoPulse = false;
    pullDataLineLow();
    Serial.println("TX:LOW");
    printReadback();
  } else if (strcmp(cmd, "READ") == 0 || strcmp(cmd, "RX?") == 0) {
    printReadback();
  } else if (strcmp(cmd, "PULSE") == 0) {
    pullDataLineLow();
    Serial.println("TX:LOW_FOR_1000MS");
    delay(1000);
    releaseDataLine();
    Serial.println("TX:RELEASED");
    printReadback();
  } else if (strcmp(cmd, "AUTO") == 0) {
    autoPulse = true;
    pulseLow = false;
    lastPulseMs = 0;
    Serial.println("AUTO:ON");
  } else if (strcmp(cmd, "STOP") == 0) {
    autoPulse = false;
    releaseDataLine();
    Serial.println("AUTO:OFF;TX:RELEASED");
  } else if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (*cmd != '\0') {
    Serial.print("ERR:UNKNOWN:");
    Serial.println(cmd);
    printHelp();
  }
}

void setup() {
  Serial.begin(kBaud);

  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
  }

  pinMode(kSeatalkTxPin, OUTPUT);
  releaseDataLine();

  pinMode(kSeatalkRxPin, INPUT);
  pinMode(kIdleSensePin, INPUT);

  Serial.println();
  Serial.println("Due SeaTalk bench electrical test");
  Serial.println("Serial baud: 115200");
  Serial.println("TX1 pin 18 HIGH = release DATA, LOW = pull DATA low");
  printHelp();
  printReadback();
}

void loop() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());

    if (c == '\n') {
      line[lineLen] = '\0';
      handleLine(line);
      lineLen = 0;
    } else if (c != '\r') {
      if (lineLen < sizeof(line) - 1) {
        line[lineLen++] = c;
      } else {
        lineLen = 0;
        Serial.println("ERR:LINE_TOO_LONG");
      }
    }
  }

  if (autoPulse && millis() - lastPulseMs >= 2000) {
    lastPulseMs = millis();
    pulseLow = !pulseLow;

    if (pulseLow) {
      pullDataLineLow();
      Serial.println("AUTO:TX_LOW");
    } else {
      releaseDataLine();
      Serial.println("AUTO:TX_RELEASED");
    }

    printReadback();
  }
}
