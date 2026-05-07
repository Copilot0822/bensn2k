#line 1 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2CFrequency = 400000;
constexpr uint32_t kSendPeriodMs = 100;
constexpr uint32_t kReconnectPeriodMs = 5000;
constexpr uint32_t kSensorStatusPeriodMs = 5000;
constexpr uint16_t kWindUdpPort = 20000;
constexpr uint8_t kAs5600Address = 0x36;
constexpr uint8_t kAs5600RegStatus = 0x0B;
constexpr uint8_t kAs5600RegRawAngleHigh = 0x0C;
constexpr uint8_t kAs5600MagnetDetectedMask = 0x20;
constexpr uint16_t kAs5600CountsPerRev = 4096;
constexpr char kApSsid[] = "M5StampPLC-Wind";
constexpr char kApPassword[] = "wind1234";
constexpr char kPacketPrefix[] = "angle=";
const IPAddress kPlcIp(192, 168, 4, 1);

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
    Serial.printf("AS5600 ready on SDA=%u SCL=%u\n", SDA, SCL);
  } else {
    Serial.println("AS5600 not detected or magnet missing");
  }

  sensorReady = ready;
  lastSensorStatusMs = now;
}

void connectToPlcAp() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(kApSsid, kApPassword);

  Serial.printf("Connecting to %s", kApSsid);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connect timed out");
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

#line 148 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
void setup();
#line 157 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
void loop();
#line 148 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
void setup() {
  Serial.begin(kSerialBaud);
  delay(500);
  Serial.println();
  Serial.println("ESP32 WiFi wind sender");
  Wire.begin(SDA, SCL, kI2CFrequency);
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

