#include <Arduino.h>
#include <WiFi.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kSerialWaitMs = 3000;
constexpr uint32_t kScanPeriodMs = 5000;

uint32_t lastScanMs = 0;

void runScan() {
  Serial.println();
  Serial.println("Scanning Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

  const int networkCount = WiFi.scanNetworks(false, true);
  if (networkCount < 0) {
    Serial.println("Wi-Fi scan failed");
    return;
  }

  Serial.printf("Found %d network(s)\n", networkCount);
  for (int i = 0; i < networkCount; ++i) {
    Serial.printf(
        "%2d: SSID=\"%s\" RSSI=%d dBm CH=%d ENC=%d BSSID=%s\n",
        i + 1,
        WiFi.SSID(i).c_str(),
        WiFi.RSSI(i),
        WiFi.channel(i),
        static_cast<int>(WiFi.encryptionType(i)),
        WiFi.BSSIDstr(i).c_str());
  }

  WiFi.scanDelete();
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
  Serial.println("XIAO ESP32C3 Wi-Fi scanner");
  runScan();
  lastScanMs = millis();
}

void loop() {
  const uint32_t now = millis();
  if (now - lastScanMs >= kScanPeriodMs) {
    runScan();
    lastScanMs = now;
  }
  delay(20);
}
