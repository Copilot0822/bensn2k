#line 1 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSendPeriodMs = 100;
constexpr uint32_t kReconnectPeriodMs = 5000;
constexpr uint16_t kWindUdpPort = 20000;
constexpr char kApSsid[] = "M5StampPLC-Wind";
constexpr char kApPassword[] = "wind1234";
constexpr char kPacketPrefix[] = "angle=";
constexpr float kPlaceholderAngleDeg = 45.0f;
const IPAddress kPlcIp(192, 168, 4, 1);

WiFiUDP windUdp;
uint32_t lastSendMs = 0;
uint32_t lastReconnectMs = 0;

float readPlaceholderAngleDeg() {
  return kPlaceholderAngleDeg;
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

void sendWindPacket() {
  const float angleDeg = readPlaceholderAngleDeg();
  char payload[32];
  snprintf(payload, sizeof(payload), "%s%.2f", kPacketPrefix, angleDeg);

  windUdp.beginPacket(kPlcIp, kWindUdpPort);
  windUdp.write(reinterpret_cast<const uint8_t*>(payload), strlen(payload));
  windUdp.endPacket();

  Serial.printf("Sent %s\n", payload);
}

}  // namespace

#line 75 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
void setup();
#line 83 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
void loop();
#line 75 "C:\\Users\\copil\\Documents\\GitHub\\bensn2k\\New folder\\WindWifiSenderESP32\\WindWifiSenderESP32.ino"
void setup() {
  Serial.begin(kSerialBaud);
  delay(500);
  Serial.println();
  Serial.println("ESP32 WiFi wind sender");
  connectToPlcAp();
}

void loop() {
  ensureWiFiConnected();

  if (WiFi.status() != WL_CONNECTED) {
    delay(10);
    return;
  }

  const uint32_t now = millis();
  if (now - lastSendMs >= kSendPeriodMs) {
    sendWindPacket();
    lastSendMs = now;
  }
}

