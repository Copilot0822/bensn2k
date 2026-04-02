#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2CFrequency = 100000;
constexpr uint8_t kInaAddress = 0x41;
constexpr uint32_t kPollMs = 1000;
constexpr int kPortASdaPin = 2;
constexpr int kPortASclPin = 1;
constexpr uint32_t kBootSettleMs = 500;
constexpr uint32_t kSerialWaitMs = 15000;

TwoWire& portA = Wire;
uint32_t lastPollMs = 0;

void recoverI2CBus() {
  pinMode(kPortASdaPin, INPUT_PULLUP);
  pinMode(kPortASclPin, INPUT_PULLUP);
  delay(2);

  if (digitalRead(kPortASdaPin) == HIGH && digitalRead(kPortASclPin) == HIGH) {
    return;
  }

  pinMode(kPortASclPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kPortASclPin, HIGH);

  for (uint8_t i = 0; i < 18 && digitalRead(kPortASdaPin) == LOW; ++i) {
    digitalWrite(kPortASclPin, LOW);
    delayMicroseconds(10);
    digitalWrite(kPortASclPin, HIGH);
    delayMicroseconds(10);
  }

  pinMode(kPortASdaPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(kPortASdaPin, LOW);
  delayMicroseconds(10);
  digitalWrite(kPortASclPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(kPortASdaPin, HIGH);
  delayMicroseconds(10);

  pinMode(kPortASdaPin, INPUT_PULLUP);
  pinMode(kPortASclPin, INPUT_PULLUP);
}

bool readRegister16(uint8_t address, uint8_t reg, uint16_t& value) {
  portA.beginTransmission(address);
  portA.write(reg);
  if (portA.endTransmission(false) != 0) {
    return false;
  }

  const int count = portA.requestFrom(static_cast<int>(address), 2);
  if (count != 2) {
    return false;
  }

  value = (static_cast<uint16_t>(portA.read()) << 8) | portA.read();
  return true;
}

bool writeRegister16(uint8_t address, uint8_t reg, uint16_t value) {
  portA.beginTransmission(address);
  portA.write(reg);
  portA.write(static_cast<uint8_t>(value >> 8));
  portA.write(static_cast<uint8_t>(value & 0xFF));
  return portA.endTransmission() == 0;
}

bool configureIna226(uint8_t address) {
  constexpr uint16_t kConfig = 0x4527;       // avg=16, vbus=1.1ms, vshunt=1.1ms, continuous shunt+bus
  constexpr uint16_t kCalibration = 0x0800;  // 0.1 ohm, ~2A expected current
  return writeRegister16(address, 0x00, kConfig) && writeRegister16(address, 0x05, kCalibration);
}

bool readBusVoltage(uint8_t address, float& voltage) {
  uint16_t raw = 0;
  if (!readRegister16(address, 0x02, raw)) {
    return false;
  }
  voltage = static_cast<float>(raw) * 0.00125f;
  return true;
}

void scanBus() {
  Serial.printf("Port A Wire SDA=%d SCL=%d scan:", kPortASdaPin, kPortASclPin);
  bool foundAny = false;
  for (uint8_t address = 8; address < 0x78; ++address) {
    portA.beginTransmission(address);
    if (portA.endTransmission() == 0) {
      Serial.printf(" 0x%02X", address);
      foundAny = true;
    }
  }
  if (!foundAny) {
    Serial.print(" none");
  }
  Serial.println();
}

void probePortA() {
  uint16_t manufacturerId = 0;
  uint16_t dieId = 0;
  const bool idsOk = readRegister16(kInaAddress, 0xFE, manufacturerId)
                  && readRegister16(kInaAddress, 0xFF, dieId);
  const bool validIna = idsOk && manufacturerId == 0x5449 && dieId == 0x2260;

  bool configured = false;
  float voltage = NAN;
  if (validIna) {
    configured = configureIna226(kInaAddress);
    delay(10);
    if (configured) {
      readBusVoltage(kInaAddress, voltage);
    }
  }

  Serial.println("----------------------------------------");
  scanBus();
  Serial.printf("0x41 ids=%s", idsOk ? "ok" : "read-fail");
  if (idsOk) {
    Serial.printf(" mfg=0x%04X die=0x%04X", manufacturerId, dieId);
  }
  Serial.println();
  Serial.printf("0x41 ina226=%s\n", validIna ? "yes" : "no");
  Serial.printf("0x41 configured=%s\n", configured ? "yes" : "no");
  if (configured && !isnan(voltage)) {
    Serial.printf("0x41 voltage=%.6f V\n", voltage);
  }
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  const uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < kSerialWaitMs) {
    delay(10);
  }

  delay(kBootSettleMs);

  recoverI2CBus();
  delay(20);

  portA.setTimeOut(50);
  portA.begin(kPortASdaPin, kPortASclPin, kI2CFrequency);
  delay(kBootSettleMs);

  Serial.println();
  Serial.println("Direct Port A INA226 probe via raw Wire with bus recovery");
  probePortA();
}

void loop() {
  if (millis() - lastPollMs >= kPollMs) {
    probePortA();
    lastPollMs = millis();
  }
}
