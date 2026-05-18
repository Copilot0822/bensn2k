#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

const char* AP_SSID = "MarineDashboardTest";
const char* AP_PASS = "dashboard";

WebServer server(80);

String autopilotMode = "STANDBY";
int targetHeading = 185;
uint32_t commandCount = 0;
uint32_t busErrorCount = 0;

int wrapHeading(int value) {
  value %= 360;
  if (value < 0) value += 360;
  return value;
}

String contentTypeFor(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

bool serveStaticFile(String path) {
  if (path == "/") path = "/index.html";
  if (!LittleFS.exists(path)) return false;

  File file = LittleFS.open(path, "r");
  if (!file) return false;

  server.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

String randomHexByte() {
  char buffer[3];
  snprintf(buffer, sizeof(buffer), "%02X", random(0, 256));
  return String(buffer);
}

String jsonStringArray(const String& a, const String& b, const String& c) {
  return "[\"" + a + "\",\"" + b + "\",\"" + c + "\"]";
}

void handleData() {
  const uint32_t now = millis();
  const float t = now / 1000.0f;

  const int heading = wrapHeading(184 + (int)(sinf(t / 8.0f) * 7.0f) + random(-1, 2));
  const int cog = wrapHeading(181 + (int)(sinf(t / 9.0f) * 6.0f) + random(-1, 2));
  const float sog = 5.4f + sinf(t / 6.0f) * 0.5f + random(-8, 9) / 100.0f;
  const int awa = 38 + (int)(sinf(t / 4.0f) * 10.0f) + random(-2, 3);
  const float depth = 12.4f + sinf(t / 7.0f) * 0.35f + random(-6, 7) / 100.0f;
  const float waterTemp = 18.6f + sinf(t / 20.0f) * 0.25f + random(-3, 4) / 100.0f;
  const int rudderAngle = (int)(sinf(t / 2.8f) * 9.0f) + random(-1, 2);
  const float battery0 = 12.72f + sinf(t / 14.0f) * 0.05f + random(-4, 5) / 100.0f;
  const float battery1 = 12.64f + cosf(t / 15.0f) * 0.05f + random(-4, 5) / 100.0f;
  const int seatalkAge = random(80, 420);
  const int n2kAge = random(90, 380);
  const uint32_t uptime = now;
  const uint32_t packetBase = now / 1000;

  String rawSeatalk = jsonStringArray(
    "84 20 " + randomHexByte() + " 00",
    "89 02 " + randomHexByte() + " 10",
    autopilotMode == "STANDBY" ? "9C 00 02 F1" : "9C 10 02 F1"
  );
  String decodedSeatalk = jsonStringArray(
    "Compass heading " + String(heading) + " deg",
    "Rudder angle " + String(rudderAngle) + " deg",
    "Pilot mode " + autopilotMode
  );
  String rawN2k = jsonStringArray("127250 heading", "128259 speed", "128267 depth");

  String json = "{";
  json += "\"heading\":" + String(heading) + ",";
  json += "\"cog\":" + String(cog) + ",";
  json += "\"sog\":" + String(sog, 1) + ",";
  json += "\"awa\":" + String(awa) + ",";
  json += "\"depth\":" + String(depth, 1) + ",";
  json += "\"waterTemp\":" + String(waterTemp, 1) + ",";
  json += "\"rudderAngle\":" + String(rudderAngle) + ",";
  json += "\"battery0\":" + String(battery0, 2) + ",";
  json += "\"battery1\":" + String(battery1, 2) + ",";
  json += "\"batteryDiff\":" + String(battery0 - battery1, 2) + ",";
  json += "\"autopilotMode\":\"" + autopilotMode + "\",";
  json += "\"targetHeading\":" + String(targetHeading) + ",";
  json += "\"seatalkStatus\":\"ok\",";
  json += "\"n2kStatus\":\"ok\",";
  json += "\"seatalkLastSeenMs\":" + String(seatalkAge) + ",";
  json += "\"n2kLastSeenMs\":" + String(n2kAge) + ",";
  json += "\"wifiClients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"uptime\":" + String(uptime) + ",";
  json += "\"packetCounters\":{";
  json += "\"seatalkRaw\":" + String(800 + packetBase * 7) + ",";
  json += "\"seatalkDecoded\":" + String(760 + packetBase * 6) + ",";
  json += "\"n2kPgn\":" + String(420 + packetBase * 5) + ",";
  json += "\"errors\":" + String(busErrorCount);
  json += "},";
  json += "\"rawSeatalk\":" + rawSeatalk + ",";
  json += "\"decodedSeatalk\":" + decodedSeatalk + ",";
  json += "\"rawN2k\":" + rawN2k;
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

int extractDeltaValue(const String& body) {
  int valueIndex = body.indexOf("\"value\"");
  if (valueIndex < 0) return 0;
  int colonIndex = body.indexOf(':', valueIndex);
  if (colonIndex < 0) return 0;
  int endIndex = body.indexOf(',', colonIndex);
  if (endIndex < 0) endIndex = body.indexOf('}', colonIndex);
  if (endIndex < 0) return 0;
  return body.substring(colonIndex + 1, endIndex).toInt();
}

void handleAutopilot() {
  const String body = server.arg("plain");
  commandCount++;

  if (body.indexOf("\"standby\"") >= 0) autopilotMode = "STANDBY";
  else if (body.indexOf("\"auto\"") >= 0) autopilotMode = "AUTO";
  else if (body.indexOf("\"wind\"") >= 0) autopilotMode = "WIND";
  else if (body.indexOf("\"track\"") >= 0) autopilotMode = "TRACK";
  else if (body.indexOf("\"heading_delta\"") >= 0) targetHeading = wrapHeading(targetHeading + extractDeltaValue(body));

  server.send(200, "application/json", "{\"ok\":true,\"commands\":" + String(commandCount) + "}");
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed((uint32_t)esp_random());

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.println();
  Serial.println("ESP32 Marine Dashboard Test");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("Open: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/data", HTTP_GET, handleData);
  server.on("/api/autopilot", HTTP_OPTIONS, handleOptions);
  server.on("/api/autopilot", HTTP_POST, handleAutopilot);
  server.onNotFound([]() {
    if (!serveStaticFile(server.uri())) {
      server.send(404, "text/plain", "Not found");
    }
  });
  server.begin();
}

void loop() {
  server.handleClient();
}
