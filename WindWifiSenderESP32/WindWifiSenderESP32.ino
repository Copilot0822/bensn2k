#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSerialWaitMs = 3000;
constexpr uint32_t kI2CFrequency = 100000;
constexpr uint32_t kSendPeriodMs = 100;
constexpr uint32_t kReconnectPeriodMs = 5000;
constexpr uint32_t kSensorStatusPeriodMs = 5000;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint16_t kWindUdpPort = 20000;
constexpr uint8_t kAs5600Address = 0x36;
constexpr uint8_t kAs5600RegStatus = 0x0B;
constexpr uint8_t kAs5600RegRawAngleHigh = 0x0C;
constexpr uint8_t kAs5600MagnetDetectedMask = 0x20;
constexpr uint16_t kAs5600CountsPerRev = 4096;
constexpr uint8_t kSdaPin = D4;  // XIAO ESP32C3 D4 = GPIO6
constexpr uint8_t kSclPin = D5;  // XIAO ESP32C3 D5 = GPIO7
constexpr char kApSsidBase[] = "M5StampPLC-Wind";
constexpr char kApSsidCanOk[] = "M5StampPLC-Wind-CANOK";
constexpr char kApSsidCanFail[] = "M5StampPLC-Wind-CANFAIL";
constexpr char kApPassword[] = "wind1234";
constexpr char kPacketPrefix[] = "angle=";
const IPAddress kPlcIp(192, 168, 4, 1);
const char* const kApCandidates[] = {kApSsidCanOk, kApSsidCanFail, kApSsidBase};

WiFiUDP windUdp;
uint32_t lastSendMs = 0;
uint32_t lastReconnectMs = 0;
uint32_t lastSensorStatusMs = 0;
bool sensorReady = false;

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

bool readAs5600AngleDeg(float& angleDeg) {
  uint8_t status = 0;
  if (!readAs5600Register(kAs5600RegStatus, &status, 1)) {
    return false;
  }
  if ((status & kAs5600MagnetDetectedMask) == 0) {
    return false;
  }

  uint8_t rawAngleBytes[2];
  if (!readAs5600Register(kAs5600RegRawAngleHigh, rawAngleBytes, sizeof(rawAngleBytes))) {
    return false;
  }

  const uint16_t rawAngle =
      (static_cast<uint16_t>(rawAngleBytes[0]) << 8 | rawAngleBytes[1]) & 0x0FFF;
  angleDeg = static_cast<float>(rawAngle) * (360.0f / kAs5600CountsPerRev);
  return true;
}

void logSensorStatus(bool ready) {
  const uint32_t now = millis();
  if (ready == sensorReady && now - lastSensorStatusMs < kSensorStatusPeriodMs) {
    return;
  }

  if (ready) {
    Serial.printf("AS5600 ready on D4/GPIO%u D5/GPIO%u\n", kSdaPin, kSclPin);
  } else {
    Serial.println("AS5600 not detected or magnet missing");
  }

  sensorReady = ready;
  lastSensorStatusMs = now;
}

void connectToPlcAp() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  for (const char* ssid : kApCandidates) {
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.begin(ssid, kApPassword);

    Serial.printf("Connecting to %s", ssid);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Connected to %s, IP %s\n", ssid, WiFi.localIP().toString().c_str());
      return;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect timed out on all PLC SSIDs");
  }
}

void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastReconnectMs < kReconnectPeriodMs) {
    return;
  }

  lastReconnectMs = now;
  WiFi.disconnect(true, true);
  delay(100);
  connectToPlcAp();
}

bool sampleEncoderAngle(float& angleDeg) {
  if (!readAs5600AngleDeg(angleDeg)) {
    logSensorStatus(false);
    return false;
  }

  logSensorStatus(true);
  Serial.printf("Encoder angle %.2f deg\n", angleDeg);
  return true;
}

void sendWindPacket(float angleDeg) {
  char payload[32];
  snprintf(payload, sizeof(payload), "%s%.2f", kPacketPrefix, angleDeg);

  windUdp.beginPacket(kPlcIp, kWindUdpPort);
  windUdp.write(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
  windUdp.endPacket();

  Serial.printf("Sent %s\n", payload);
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
  Serial.println("XIAO ESP32C3 WiFi wind sender");
  Wire.begin(kSdaPin, kSclPin, kI2CFrequency);
  Serial.printf("I2C started on D4/GPIO%u D5/GPIO%u at %lu Hz\n", kSdaPin, kSclPin,
                kI2CFrequency);
  connectToPlcAp();
}

void loop() {
  ensureWiFiConnected();

  const uint32_t now = millis();
  if (now - lastSendMs >= kSendPeriodMs) {
    float angleDeg = 0.0f;
    if (sampleEncoderAngle(angleDeg) && WiFi.status() == WL_CONNECTED) {
      sendWindPacket(angleDeg);
    }
    lastSendMs = now;
  }

  delay(10);
}
