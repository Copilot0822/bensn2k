#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2CFrequency = 100000;
constexpr uint32_t kSamplePeriodMs = 250;
constexpr uint32_t kStatusPeriodMs = 3000;
constexpr uint32_t kSerialWaitMs = 3000;
constexpr uint8_t kSdaPin = D4;  // XIAO ESP32C3 board label D4 = GPIO6
constexpr uint8_t kSclPin = D5;  // XIAO ESP32C3 board label D5 = GPIO7
constexpr uint8_t kAs5600Address = 0x36;
constexpr uint8_t kAs5600RegStatus = 0x0B;
constexpr uint8_t kAs5600RegRawAngleHigh = 0x0C;
constexpr uint8_t kAs5600RegAgc = 0x1A;
constexpr uint8_t kAs5600RegMagnitudeHigh = 0x1B;
constexpr uint8_t kStatusMagnetTooStrongMask = 0x08;
constexpr uint8_t kStatusMagnetTooWeakMask = 0x10;
constexpr uint8_t kStatusMagnetDetectedMask = 0x20;
constexpr uint16_t kAs5600CountsPerRev = 4096;

uint32_t lastSampleMs = 0;
uint32_t lastStatusMs = 0;

bool probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool readAs5600Register(uint8_t startRegister, uint8_t* buffer, size_t length) {
  Wire.beginTransmission(kAs5600Address);
  Wire.write(startRegister);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  const size_t bytesRead = Wire.requestFrom(kAs5600Address, static_cast<uint8_t>(length));
  if (bytesRead != length) {
    while (Wire.available() > 0) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    buffer[i] = Wire.read();
  }

  return true;
}

uint16_t combine12Bit(uint8_t highByte, uint8_t lowByte) {
  return (static_cast<uint16_t>(highByte) << 8 | lowByte) & 0x0FFF;
}

const char* magnetStateString(uint8_t status) {
  if (status & kStatusMagnetTooStrongMask) {
    return "too strong";
  }
  if (status & kStatusMagnetTooWeakMask) {
    return "too weak";
  }
  if (status & kStatusMagnetDetectedMask) {
    return "ok";
  }
  return "not detected";
}

void scanI2cBus() {
  Serial.println("Scanning I2C bus...");

  bool foundAny = false;
  for (uint8_t address = 1; address < 0x7F; ++address) {
    if (!probeI2cAddress(address)) {
      continue;
    }

    foundAny = true;
    Serial.printf("I2C device found at 0x%02X", address);
    if (address == kAs5600Address) {
      Serial.print(" (AS5600)");
    }
    Serial.println();
  }

  if (!foundAny) {
    Serial.println("No I2C devices responded");
  }
}

void printStatusLine(const char* message) {
  const uint32_t now = millis();
  if (now - lastStatusMs < kStatusPeriodMs) {
    return;
  }
  lastStatusMs = now;
  Serial.println(message);
}

void printDiagnosticSample() {
  if (!probeI2cAddress(kAs5600Address)) {
    printStatusLine("AS5600 probe failed at 0x36");
    return;
  }

  uint8_t status = 0;
  if (!readAs5600Register(kAs5600RegStatus, &status, 1)) {
    printStatusLine("AS5600 status register read failed");
    return;
  }

  uint8_t angleBytes[2] = {};
  const bool angleReadOk =
      readAs5600Register(kAs5600RegRawAngleHigh, angleBytes, sizeof(angleBytes));

  uint8_t agc = 0;
  const bool agcReadOk = readAs5600Register(kAs5600RegAgc, &agc, 1);

  uint8_t magnitudeBytes[2] = {};
  const bool magnitudeReadOk =
      readAs5600Register(kAs5600RegMagnitudeHigh, magnitudeBytes, sizeof(magnitudeBytes));

  Serial.printf("status=0x%02X magnet=%s md=%u ml=%u mh=%u",
                status,
                magnetStateString(status),
                (status & kStatusMagnetDetectedMask) ? 1 : 0,
                (status & kStatusMagnetTooWeakMask) ? 1 : 0,
                (status & kStatusMagnetTooStrongMask) ? 1 : 0);

  if (angleReadOk) {
    const uint16_t rawAngle = combine12Bit(angleBytes[0], angleBytes[1]);
    const float angleDeg = static_cast<float>(rawAngle) * (360.0f / kAs5600CountsPerRev);
    Serial.printf(" raw=%u angle=%.2f", rawAngle, angleDeg);
  } else {
    Serial.print(" raw=<read failed>");
  }

  if (agcReadOk) {
    Serial.printf(" agc=%u", agc);
  } else {
    Serial.print(" agc=<read failed>");
  }

  if (magnitudeReadOk) {
    const uint16_t magnitude = combine12Bit(magnitudeBytes[0], magnitudeBytes[1]);
    Serial.printf(" magnitude=%u", magnitude);
  } else {
    Serial.print(" magnitude=<read failed>");
  }

  Serial.println();
}

}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  const uint32_t serialStartMs = millis();
  while (!Serial && millis() - serialStartMs < kSerialWaitMs) {
    delay(10);
  }
  delay(200);

  Serial.println();
  Serial.println("XIAO ESP32C3 AS5600 reader");
  Serial.printf("Starting I2C on D4/GPIO%u (SDA) and D5/GPIO%u (SCL) at %lu Hz\n",
                kSdaPin,
                kSclPin,
                kI2CFrequency);

  Wire.begin(kSdaPin, kSclPin, kI2CFrequency);
  scanI2cBus();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastSampleMs < kSamplePeriodMs) {
    delay(5);
    return;
  }
  lastSampleMs = now;

  printDiagnosticSample();
}
