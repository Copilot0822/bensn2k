#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <M5Unified.hpp>
#include <M5StamPLC.h>
#include <NMEA2000.h>
#include <N2kMessages.h>
#include <NMEA2000_esp32_twai.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2CFrequency = 100000;
constexpr uint32_t kBootSettleMs = 500;
constexpr uint32_t kSerialWaitMs = 15000;
constexpr uint32_t kBatterySamplePeriodMs = 500;
constexpr uint32_t kBatterySendPeriodMs = 1500;
constexpr uint32_t kWindSendPeriodMs = 100;
constexpr uint32_t kDisplayPeriodMs = 250;
constexpr uint32_t kWindFreshMs = 3000;
constexpr uint32_t kN2kFreshMs = 5000;
constexpr uint32_t kSeatalkFreshMs = 3000;
constexpr double kApparentWindSpeedMetersPerSecond = 8.0 * 0.5144444444444445;
constexpr uint32_t kDueHelloPeriodMs = 2000;
constexpr uint32_t kDueSerialBaud = 115200;
constexpr uint8_t kDueRxPin = 40;  // M5Stamp PLC G40: receive from Due TX2 pin 16
constexpr uint8_t kDueTxPin = 41;  // M5Stamp PLC G41: transmit to Due RX2 pin 17
constexpr size_t kDueLineMax = 160;
constexpr size_t kDebugLineCount = 8;
constexpr uint8_t kInaAddress = 0x41;
constexpr uint8_t kInaRegConfig = 0x00;
constexpr uint8_t kInaRegBusVoltage = 0x02;
constexpr uint8_t kInaRegCalibration = 0x05;
constexpr uint8_t kInaRegManufacturerId = 0xFE;
constexpr uint8_t kInaRegDieId = 0xFF;
constexpr uint16_t kInaManufacturerId = 0x5449;
constexpr uint16_t kInaDieId = 0x2260;
constexpr uint16_t kInaConfigValue = 0x4527;
constexpr uint16_t kInaCalibrationValue = 0x0800;
constexpr float kOffsetStepDeg = 1.0f;
constexpr uint16_t kWindUdpPort = 20000;
constexpr char kApSsid[] = "M5StampPLC-Wind";
constexpr char kApPassword[] = "wind1234";
constexpr char kPreferencesNamespace[] = "windcfg";
constexpr char kPreferencesOffsetKey[] = "offset_deg";
constexpr uint8_t kPlcRelay1Channel = 0;
constexpr uint8_t kPlcRelay2Channel = 1;
constexpr gpio_num_t kCanTxPin = static_cast<gpio_num_t>(STAMPLC_PIN_CAN_TX);
constexpr gpio_num_t kCanRxPin = static_cast<gpio_num_t>(STAMPLC_PIN_CAN_RX);
constexpr uint32_t kCanBaudRate = 250000;
const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kApGateway(192, 168, 4, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

enum class DisplayMode : uint8_t {
  Summary = 0,
  Wind = 1,
};

struct BatteryChannel {
  const char* name;
  int sdaPin;
  int sclPin;
  uint8_t instance;
  bool present;
  float voltage;
  uint8_t sid;
};

struct WindState {
  bool valid;
  float rawAngleDeg;
  float offsetDeg;
  uint8_t sid;
  uint32_t lastPacketMs;
  uint32_t packetCount;
  IPAddress lastSenderIp;
};

struct SeatalkState {
  bool dueConnected;
  bool statusValid;
  char mode[16];
  float rudderAngleDeg;
  float targetHeadingDeg;
  float compassHeadingDeg;
  float windAngleDeg;
  uint32_t windAngleLastMs;
  uint32_t dueLastSeenMs;
  uint32_t lastSeenMs;
  uint32_t rawPacketCount;
  uint32_t decodedPacketCount;
  uint32_t errorCount;
};

struct WindTargetState {
  bool valid;
  float angleDeg;
  uint32_t lastSetMs;
};

struct N2kRxState {
  float headingDeg;
  float cogDeg;
  float sogKn;
  float depthM;
  float waterTempC;
  uint32_t headingLastMs;
  uint32_t cogSogLastMs;
  uint32_t speedLastMs;
  uint32_t depthLastMs;
  uint32_t waterTempLastMs;
  uint32_t lastRxMs;
  uint32_t rxPacketCount;
};

TwoWire& externalBus = Wire;
WiFiUDP windUdp;
WebServer webServer(80);
HardwareSerial dueSerial(1);
Preferences preferences;
NMEA2000_esp32_twai NMEA2000(kCanTxPin, kCanRxPin);
AW9523_Class plcIoExpander;
char apSsid[32] = {};
char dueLine[kDueLineMax] = {};
size_t dueLineLen = 0;

BatteryChannel batteries[] = {
    {"Port A", 2, 1, 0, false, NAN, 0},
    {"Port C", 5, 4, 1, false, NAN, 0},
};

constexpr size_t kBatteryCount = sizeof(batteries) / sizeof(batteries[0]);

WindState wind = {false, NAN, 0.0f, 0, 0, 0, IPAddress(0, 0, 0, 0)};
SeatalkState seatalk = {false, false, "--", NAN, NAN, NAN, NAN, 0, 0, 0, 0, 0, 0};
WindTargetState windTarget = {false, NAN, 0};
N2kRxState n2kRx = {NAN, NAN, NAN, NAN, NAN, 0, 0, 0, 0, 0, 0, 0};
DisplayMode displayMode = DisplayMode::Summary;
bool canBusEnabled = false;

String rawSeatalkLines[kDebugLineCount];
String decodedSeatalkLines[kDebugLineCount];
String rawN2kLines[kDebugLineCount];
uint32_t lastBatterySampleMs = 0;
uint32_t lastBatterySendMs = 0;
uint32_t lastWindSendMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastCanStatusMs = 0;
uint32_t lastDueHelloMs = 0;
uint32_t n2kPacketCount = 0;
int activeSdaPin = -1;
int activeSclPin = -1;

const unsigned long kTransmitMessages[] PROGMEM = {127508L, 130306L, 0};
const unsigned long kReceiveMessages[] PROGMEM = {
    127250L,  // Vessel heading
    128259L,  // Boat speed
    128267L,  // Water depth
    129026L,  // COG/SOG rapid
    130310L,  // Outside environmental parameters
    130311L,  // Environmental parameters
    130312L,  // Temperature
    130316L,  // Temperature extended range
    0};

const tNMEA2000::tProductInformation kProductInformation PROGMEM = {
    2101,
    100,
    "M5StampPLC Batt+Wind",
    "1.1.0",
    "1.1.0",
    "M5STAMPLC-BAT-WIND",
    1,
    1,
};

const char kManufacturerInformation[] PROGMEM = "Copilot0822";
const char kInstallationDescription1[] PROGMEM = "Batt A/C + WiFi AWA";
const char kInstallationDescription2[] PROGMEM = "PGN 127508 + 130306";

float wrapDegrees(float degrees) {
  while (degrees < 0.0f) {
    degrees += 360.0f;
  }
  while (degrees >= 360.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

float normalizeSignedDegrees(float degrees) {
  degrees = wrapDegrees(degrees);
  if (degrees > 180.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

bool windFresh(uint32_t now) {
  return wind.valid && (now - wind.lastPacketMs <= kWindFreshMs);
}

float adjustedWindAngleDeg() {
  if (!wind.valid || isnan(wind.rawAngleDeg)) {
    return NAN;
  }
  return wrapDegrees(wind.rawAngleDeg + wind.offsetDeg);
}

bool seatalkWindFresh(uint32_t now) {
  return !isnan(seatalk.windAngleDeg) && seatalk.windAngleLastMs != 0 &&
         (now - seatalk.windAngleLastMs <= kSeatalkFreshMs);
}

bool latestApparentWindAngleDeg(uint32_t now, float& angleDeg) {
  if (seatalkWindFresh(now)) {
    angleDeg = seatalk.windAngleDeg;
    return true;
  }

  if (wind.valid && !isnan(wind.rawAngleDeg)) {
    angleDeg = adjustedWindAngleDeg();
    return !isnan(angleDeg);
  }

  return false;
}

void captureWindTarget(uint32_t now) {
  float angleDeg = NAN;
  if (!latestApparentWindAngleDeg(now, angleDeg)) {
    return;
  }

  windTarget.valid = true;
  windTarget.angleDeg = wrapDegrees(angleDeg);
  windTarget.lastSetMs = now;
}

void adjustWindTarget(float deltaDeg, uint32_t now) {
  if (!windTarget.valid || isnan(windTarget.angleDeg)) {
    captureWindTarget(now);
  }

  if (!windTarget.valid || isnan(windTarget.angleDeg)) {
    return;
  }

  windTarget.angleDeg = wrapDegrees(windTarget.angleDeg + deltaDeg);
  windTarget.lastSetMs = now;
}

void mirrorWindTarget(uint32_t now) {
  if (!windTarget.valid || isnan(windTarget.angleDeg)) {
    captureWindTarget(now);
  }

  if (!windTarget.valid || isnan(windTarget.angleDeg)) {
    return;
  }

  windTarget.angleDeg = wrapDegrees(360.0f - windTarget.angleDeg);
  windTarget.lastSetMs = now;
}

String formatRelativeWindAngle(float angleDeg) {
  if (isnan(angleDeg)) {
    return "--";
  }

  int rounded = static_cast<int>(roundf(wrapDegrees(angleDeg)));
  if (rounded >= 360) {
    rounded -= 360;
  }

  if (rounded == 0) {
    return "0";
  }
  if (rounded == 180) {
    return "180";
  }
  if (rounded < 180) {
    return String(rounded) + "S";
  }
  return String(360 - rounded) + "P";
}

String windStatusString(uint32_t now) {
  if (!wind.valid) {
    return "missing";
  }
  return windFresh(now) ? "ok" : "stale";
}

String formatIp(const IPAddress& ip) {
  return ip.toString();
}

bool seatalkFresh(uint32_t now) {
  return seatalk.statusValid && (now - seatalk.lastSeenMs <= kSeatalkFreshMs);
}

bool dueFresh(uint32_t now) {
  return seatalk.dueConnected && (now - seatalk.dueLastSeenMs <= (kDueHelloPeriodMs * 3));
}

bool valueFresh(uint32_t now, uint32_t lastMs, uint32_t freshMs = kN2kFreshMs) {
  return lastMs != 0 && (now - lastMs <= freshMs);
}

float freshOrNan(float value, uint32_t now, uint32_t lastMs, uint32_t freshMs = kN2kFreshMs) {
  return valueFresh(now, lastMs, freshMs) ? value : NAN;
}

float n2kDoubleToFloatOrNan(double value) {
  return N2kIsNA(value) ? NAN : static_cast<float>(value);
}

float radiansToDegreesOrNan(double radians) {
  return N2kIsNA(radians) ? NAN : wrapDegrees(static_cast<float>(radians * 180.0 / M_PI));
}

float metersPerSecondToKnotsOrNan(double metersPerSecond) {
  return N2kIsNA(metersPerSecond) ? NAN : static_cast<float>(metersPerSecond * 1.9438444924406);
}

float kelvinToCelsiusOrNan(double kelvin) {
  return N2kIsNA(kelvin) ? NAN : static_cast<float>(kelvin - 273.15);
}

void rememberLine(String lines[], const String& line) {
  for (size_t i = kDebugLineCount - 1; i > 0; --i) {
    lines[i] = lines[i - 1];
  }
  lines[0] = line;
}

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else {
      escaped += c;
    }
  }

  return escaped;
}

void appendJsonString(String& json, const String& key, const String& value) {
  json += "\"";
  json += key;
  json += "\":\"";
  json += jsonEscape(value);
  json += "\"";
}

void appendJsonNumberOrNull(String& json, const String& key, float value, uint8_t precision) {
  json += "\"";
  json += key;
  json += "\":";
  if (isnan(value)) {
    json += "null";
  } else {
    json += String(value, static_cast<unsigned int>(precision));
  }
}

void appendJsonStringOrNull(String& json, const String& key, const String& value, bool valid) {
  json += "\"";
  json += key;
  json += "\":";
  if (!valid) {
    json += "null";
    return;
  }
  json += "\"";
  json += jsonEscape(value);
  json += "\"";
}

void appendJsonLineArray(String& json, const String& key, const String lines[]) {
  json += "\"";
  json += key;
  json += "\":[";
  bool first = true;
  for (size_t i = 0; i < kDebugLineCount; ++i) {
    if (lines[i].isEmpty()) {
      continue;
    }
    if (!first) {
      json += ",";
    }
    json += "\"";
    json += jsonEscape(lines[i]);
    json += "\"";
    first = false;
  }
  json += "]";
}

void saveWindOffset() {
  preferences.putFloat(kPreferencesOffsetKey, wind.offsetDeg);
}

void loadWindOffset() {
  wind.offsetDeg = normalizeSignedDegrees(preferences.getFloat(kPreferencesOffsetKey, 0.0f));
}

void printLine(int x, int y, uint16_t color, const String& text) {
  M5.Display.setCursor(x, y);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.println(text);
}

void drawSummaryScreen(uint32_t now) {
  const int marginX = 8;
  const int marginY = 6;
  const int rowGap = 18;
  const int footerY = M5.Display.height() - 14;
  int y = marginY;

  printLine(marginX, y, TFT_WHITE, "Battery + Wind");
  y += rowGap;

  for (size_t i = 0; i < kBatteryCount; ++i) {
    const auto& battery = batteries[i];
    String line = String(battery.instance) + " " + battery.name + " ";
    if (battery.present && !isnan(battery.voltage)) {
      line += String(battery.voltage, 3) + "V";
      printLine(marginX, y, i == 0 ? TFT_GREENYELLOW : TFT_CYAN, line);
    } else {
      line += "NA";
      printLine(marginX, y, TFT_ORANGE, line);
    }
    y += rowGap;
  }

  if (wind.valid && !isnan(wind.rawAngleDeg)) {
    printLine(marginX, y, TFT_YELLOW, "AWA " + formatRelativeWindAngle(adjustedWindAngleDeg()));
  } else {
    printLine(marginX, y, TFT_ORANGE, "AWA waiting");
  }
  y += rowGap;

  printLine(marginX, y, TFT_LIGHTGREY, "Off " + String(wind.offsetDeg, 1) + " deg");
  y += rowGap;

  const String wifiLine = "WiFi sta " + String(WiFi.softAPgetStationNum()) + " udp " + String(kWindUdpPort);
  printLine(marginX, y, TFT_LIGHTGREY, wifiLine);

  printLine(marginX, footerY, TFT_LIGHTGREY, "A+ B- C>");
}

void drawWindScreen(uint32_t now) {
  const int marginX = 8;
  const int marginY = 6;
  const int rowGap = 18;
  const int footerY = M5.Display.height() - 14;
  int y = marginY;

  printLine(marginX, y, TFT_WHITE, "Wind Detail");
  y += rowGap;

  if (wind.valid && !isnan(wind.rawAngleDeg)) {
    printLine(marginX, y, TFT_CYAN, "Raw " + String(wind.rawAngleDeg, 1) + " deg");
  } else {
    printLine(marginX, y, TFT_ORANGE, "Raw waiting");
  }
  y += rowGap;

  if (wind.valid && !isnan(wind.rawAngleDeg)) {
    printLine(marginX, y, TFT_YELLOW, "Adj " + formatRelativeWindAngle(adjustedWindAngleDeg()));
  } else {
    printLine(marginX, y, TFT_ORANGE, "Adj stale");
  }
  y += rowGap;

  printLine(marginX, y, TFT_GREENYELLOW, "Off " + String(wind.offsetDeg, 1) + " deg");
  y += rowGap;

  const uint32_t ageMs = wind.valid ? now - wind.lastPacketMs : 0;
  printLine(marginX, y, TFT_LIGHTGREY, wind.valid ? "Age " + String(ageMs) + " ms" : "Age --");
  y += rowGap;

  if (wind.valid) {
    printLine(marginX, y, TFT_LIGHTGREY, "Peer " + formatIp(wind.lastSenderIp));
  } else {
    printLine(marginX, y, TFT_LIGHTGREY, "Peer --");
  }
  y += rowGap;

  printLine(marginX, y, TFT_LIGHTGREY, "Pkts " + String(wind.packetCount));
  printLine(marginX, footerY, TFT_LIGHTGREY, "A+ B- C>");
}

void updateDisplay() {
  const uint32_t now = millis();

  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextFont(2);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);

  if (displayMode == DisplayMode::Summary) {
    drawSummaryScreen(now);
  } else {
    drawWindScreen(now);
  }

  M5.Display.endWrite();
}

void recoverI2CBus(int sdaPin, int sclPin) {
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  delay(2);

  if (digitalRead(sdaPin) == HIGH && digitalRead(sclPin) == HIGH) {
    return;
  }

  pinMode(sclPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(sclPin, HIGH);

  for (uint8_t i = 0; i < 18 && digitalRead(sdaPin) == LOW; ++i) {
    digitalWrite(sclPin, LOW);
    delayMicroseconds(10);
    digitalWrite(sclPin, HIGH);
    delayMicroseconds(10);
  }

  pinMode(sdaPin, OUTPUT_OPEN_DRAIN);
  digitalWrite(sdaPin, LOW);
  delayMicroseconds(10);
  digitalWrite(sclPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(sdaPin, HIGH);
  delayMicroseconds(10);

  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
}

void activateExternalBus(const BatteryChannel& battery) {
  if (activeSdaPin == battery.sdaPin && activeSclPin == battery.sclPin) {
    return;
  }

  // This board needs a full Wire shutdown before rebinding Port A vs Port C.
  // Without end(), the ESP32 I2C driver can keep talking to the previous pins.
  recoverI2CBus(battery.sdaPin, battery.sclPin);
  delay(2);
  externalBus.end();
  externalBus.setTimeOut(50);
  externalBus.begin(battery.sdaPin, battery.sclPin, kI2CFrequency);
  activeSdaPin = battery.sdaPin;
  activeSclPin = battery.sclPin;
  delay(5);
}

bool readRegister16(uint8_t address, uint8_t reg, uint16_t& value) {
  externalBus.beginTransmission(address);
  externalBus.write(reg);
  if (externalBus.endTransmission(false) != 0) {
    return false;
  }

  const int count = externalBus.requestFrom(static_cast<int>(address), 2);
  if (count != 2) {
    return false;
  }

  value = (static_cast<uint16_t>(externalBus.read()) << 8) | externalBus.read();
  return true;
}

bool writeRegister16(uint8_t address, uint8_t reg, uint16_t value) {
  externalBus.beginTransmission(address);
  externalBus.write(reg);
  externalBus.write(static_cast<uint8_t>(value >> 8));
  externalBus.write(static_cast<uint8_t>(value & 0xFF));
  return externalBus.endTransmission() == 0;
}

bool verifyIna226() {
  uint16_t manufacturerId = 0;
  uint16_t dieId = 0;
  return readRegister16(kInaAddress, kInaRegManufacturerId, manufacturerId)
      && readRegister16(kInaAddress, kInaRegDieId, dieId)
      && manufacturerId == kInaManufacturerId
      && dieId == kInaDieId;
}

bool configureIna226() {
  return writeRegister16(kInaAddress, kInaRegConfig, kInaConfigValue)
      && writeRegister16(kInaAddress, kInaRegCalibration, kInaCalibrationValue);
}

bool readBusVoltage(float& voltage) {
  uint16_t raw = 0;
  if (!readRegister16(kInaAddress, kInaRegBusVoltage, raw)) {
    return false;
  }

  voltage = static_cast<float>(raw) * 0.00125f;
  return true;
}

void initBattery(BatteryChannel& battery) {
  activateExternalBus(battery);
  delay(kBootSettleMs);

  battery.present = verifyIna226() && configureIna226();
  battery.voltage = NAN;

  Serial.printf("%s init: %s\n", battery.name, battery.present ? "INA226 OK" : "not found");
}

void sampleBattery(BatteryChannel& battery) {
  activateExternalBus(battery);

  if (!battery.present) {
    battery.present = verifyIna226() && configureIna226();
    if (!battery.present) {
      battery.voltage = NAN;
      return;
    }
  }

  float voltage = NAN;
  if (readBusVoltage(voltage)) {
    battery.voltage = voltage;
  } else {
    battery.present = false;
    battery.voltage = NAN;
  }
}

void sampleBatteries() {
  for (auto& battery : batteries) {
    sampleBattery(battery);
  }
}

void sendBatteryStatus(BatteryChannel& battery) {
  tN2kMsg message;
  const double voltage = battery.present ? battery.voltage : N2kDoubleNA;

  SetN2kPGN127508(message, battery.instance, voltage, N2kDoubleNA, N2kDoubleNA, battery.sid++);
  NMEA2000.SendMsg(message);
  ++n2kPacketCount;
}

void sendBatteryStatuses() {
  if (!canBusEnabled) {
    return;
  }

  for (auto& battery : batteries) {
    sendBatteryStatus(battery);
  }
}

bool parseWindAnglePacket(const char* packet, float& angleDeg) {
  if (packet == nullptr) {
    return false;
  }

  const char* valueStart = strchr(packet, '=');
  if (valueStart != nullptr) {
    ++valueStart;
  } else {
    valueStart = packet;
  }

  char* end = nullptr;
  const float parsed = strtof(valueStart, &end);
  if (end == valueStart) {
    return false;
  }

  angleDeg = wrapDegrees(parsed);
  return true;
}

void receiveWindPackets() {
  int packetSize = windUdp.parsePacket();

  while (packetSize > 0) {
    char buffer[64];
    const int bytesRead = windUdp.read(buffer, sizeof(buffer) - 1);
    if (bytesRead > 0) {
      buffer[bytesRead] = '\0';

      float angleDeg = NAN;
      if (parseWindAnglePacket(buffer, angleDeg)) {
        wind.valid = true;
        wind.rawAngleDeg = angleDeg;
        wind.lastPacketMs = millis();
        wind.lastSenderIp = windUdp.remoteIP();
        ++wind.packetCount;
      }
    }

    packetSize = windUdp.parsePacket();
  }
}

bool parseFloatField(const char* line, const char* key, float& value) {
  const char* start = strstr(line, key);
  if (start == nullptr) {
    return false;
  }

  start += strlen(key);
  char* end = nullptr;
  const float parsed = strtof(start, &end);
  if (end == start) {
    return false;
  }

  value = parsed;
  return true;
}

bool parseTextField(const char* line, const char* key, char* output, size_t outputSize) {
  const char* start = strstr(line, key);
  if (start == nullptr || outputSize == 0) {
    return false;
  }

  start += strlen(key);
  const char* end = strchr(start, ';');
  const size_t length = end == nullptr ? strlen(start) : static_cast<size_t>(end - start);
  const size_t copyLength = min(length, outputSize - 1);
  memcpy(output, start, copyLength);
  output[copyLength] = '\0';
  return true;
}

bool parseHexByteField(const char* line, const char* key, uint8_t& value) {
  const char* start = strstr(line, key);
  if (start == nullptr) {
    return false;
  }

  start += strlen(key);
  char* end = nullptr;
  const unsigned long parsed = strtoul(start, &end, 16);
  if (end == start || parsed > 0xFF) {
    return false;
  }

  value = static_cast<uint8_t>(parsed);
  return true;
}

void processSeatalkKey86Line(const char* line, uint32_t now) {
  uint8_t key = 0;
  if (!parseHexByteField(line, "KEY=0x", key)) {
    return;
  }

  if (key == 0x23) {
    captureWindTarget(now);
  } else if (strcmp(seatalk.mode, "WIND") == 0) {
    switch (key) {
      case 0x21:
      case 0x22:
      case 0x28:
      case 0x61:
      case 0x62:
      case 0x68:
        mirrorWindTarget(now);
        break;
      case 0x05:
      case 0x45:
      case 0x80:
        adjustWindTarget(-1.0f, now);
        break;
      case 0x06:
      case 0x46:
      case 0x82:
        adjustWindTarget(-10.0f, now);
        break;
      case 0x07:
      case 0x47:
      case 0x81:
        adjustWindTarget(1.0f, now);
        break;
      case 0x08:
      case 0x48:
      case 0x83:
        adjustWindTarget(10.0f, now);
        break;
      default:
        break;
    }
  }
}

void processDueLine(const char* line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  const uint32_t now = millis();
  seatalk.dueConnected = true;
  seatalk.dueLastSeenMs = now;

  if (strncmp(line, "STATUS:", 7) == 0) {
    char previousMode[sizeof(seatalk.mode)];
    strncpy(previousMode, seatalk.mode, sizeof(previousMode));
    previousMode[sizeof(previousMode) - 1] = '\0';

    parseTextField(line, "MODE=", seatalk.mode, sizeof(seatalk.mode));
    parseFloatField(line, "RUDDER=", seatalk.rudderAngleDeg);
    parseFloatField(line, "SETPOINT=", seatalk.targetHeadingDeg);
    seatalk.statusValid = true;
    seatalk.lastSeenMs = now;
    ++seatalk.decodedPacketCount;
    if (strcmp(seatalk.mode, "WIND") == 0 &&
        (strcmp(previousMode, "WIND") != 0 || !windTarget.valid)) {
      captureWindTarget(now);
    }
    rememberLine(decodedSeatalkLines, String(line));
    return;
  }

  if (strncmp(line, "INFO:COMPASS=", 13) == 0) {
    seatalk.compassHeadingDeg = wrapDegrees(strtof(line + 13, nullptr));
    ++seatalk.decodedPacketCount;
    rememberLine(decodedSeatalkLines, String(line));
    return;
  }

  if (strncmp(line, "WIND_ANGLE:", 11) == 0) {
    seatalk.windAngleDeg = wrapDegrees(strtof(line + 11, nullptr));
    seatalk.windAngleLastMs = now;
    seatalk.lastSeenMs = now;
    ++seatalk.decodedPacketCount;
    if (strcmp(seatalk.mode, "WIND") == 0 && !windTarget.valid) {
      captureWindTarget(now);
    }
    rememberLine(decodedSeatalkLines, String(line));
    return;
  }

  if (strncmp(line, "KEY86:", 6) == 0) {
    processSeatalkKey86Line(line, now);
    ++seatalk.decodedPacketCount;
    rememberLine(decodedSeatalkLines, String(line));
    return;
  }

  if (strncmp(line, "ST_RX:", 6) == 0) {
    seatalk.lastSeenMs = now;
    ++seatalk.rawPacketCount;
    rememberLine(rawSeatalkLines, String(line + 6));
    return;
  }

  if (strncmp(line, "ERR:", 4) == 0 || strstr(line, "ERROR") != nullptr) {
    ++seatalk.errorCount;
    rememberLine(decodedSeatalkLines, String(line));
    return;
  }

  if (strncmp(line, "ACK:", 4) == 0 || strncmp(line, "BUTTON:", 7) == 0 ||
      strncmp(line, "ST_TX:", 6) == 0 || strncmp(line, "TX_OK", 5) == 0 ||
      strncmp(line, "WOULD_TX:", 9) == 0) {
    rememberLine(decodedSeatalkLines, String(line));
    return;
  }
}

void serviceDueSerial() {
  while (dueSerial.available() > 0) {
    const char c = static_cast<char>(dueSerial.read());

    if (c == '\n') {
      dueLine[dueLineLen] = '\0';
      processDueLine(dueLine);
      dueLineLen = 0;
    } else if (c != '\r') {
      if (dueLineLen < sizeof(dueLine) - 1) {
        dueLine[dueLineLen++] = c;
      } else {
        dueLineLen = 0;
        ++seatalk.errorCount;
      }
    }
  }

  const uint32_t now = millis();
  if (now - lastDueHelloMs >= kDueHelloPeriodMs) {
    dueSerial.println("HELLO");
    lastDueHelloMs = now;
  }
}

void sendDueButtonCommand(const char* buttonCommand) {
  rememberLine(decodedSeatalkLines, String("M5_TX:BTN:") + buttonCommand);
  dueSerial.print("BTN:");
  dueSerial.println(buttonCommand);
  Serial.printf("Due command BTN:%s\n", buttonCommand);
}

void sendWindStatus() {
  if (!canBusEnabled) {
    return;
  }

  tN2kMsg message;
  const double windAngle =
      wind.valid && !isnan(wind.rawAngleDeg) ? adjustedWindAngleDeg() * M_PI / 180.0 : N2kDoubleNA;

  SetN2kWindSpeed(message, wind.sid++, kApparentWindSpeedMetersPerSecond, windAngle, N2kWind_Apparent);
  NMEA2000.SendMsg(message);
  ++n2kPacketCount;
}

void handleButtons() {
  bool changed = false;

  if (M5.BtnA.wasClicked()) {
    wind.offsetDeg = normalizeSignedDegrees(wind.offsetDeg + kOffsetStepDeg);
    saveWindOffset();
    Serial.printf("Wind offset %.1f deg\n", wind.offsetDeg);
    changed = true;
  }

  if (M5.BtnB.wasClicked()) {
    wind.offsetDeg = normalizeSignedDegrees(wind.offsetDeg - kOffsetStepDeg);
    saveWindOffset();
    Serial.printf("Wind offset %.1f deg\n", wind.offsetDeg);
    changed = true;
  }

  if (M5.BtnC.wasClicked()) {
    displayMode = (displayMode == DisplayMode::Summary) ? DisplayMode::Wind : DisplayMode::Summary;
    Serial.printf("Display mode %s\n", displayMode == DisplayMode::Summary ? "summary" : "wind");
    changed = true;
  }

  if (changed) {
    updateDisplay();
    lastDisplayMs = millis();
  }
}

void rememberN2kLine(const char* label, float value, const char* unit) {
  String line = label;
  line += " ";
  if (isnan(value)) {
    line += "NA";
  } else {
    line += String(value, 2);
    if (unit != nullptr && unit[0] != '\0') {
      line += unit;
    }
  }
  rememberLine(rawN2kLines, line);
}

void handleN2kHeading(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  tN2kHeadingReference reference;
  double heading = N2kDoubleNA;
  double deviation = N2kDoubleNA;
  double variation = N2kDoubleNA;

  if (ParseN2kHeading(message, sid, heading, deviation, variation, reference)) {
    n2kRx.headingDeg = radiansToDegreesOrNan(heading);
    n2kRx.headingLastMs = now;
    rememberN2kLine("127250 heading", n2kRx.headingDeg, "deg");
  }
}

void handleN2kBoatSpeed(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  double sow = N2kDoubleNA;
  double sog = N2kDoubleNA;
  tN2kSpeedWaterReferenceType referenceType;

  if (ParseN2kBoatSpeed(message, sid, sow, sog, referenceType)) {
    const float sogKn = metersPerSecondToKnotsOrNan(sog);
    const float sowKn = metersPerSecondToKnotsOrNan(sow);
    n2kRx.sogKn = isnan(sogKn) ? sowKn : sogKn;
    n2kRx.speedLastMs = now;
    rememberN2kLine("128259 speed", n2kRx.sogKn, "kt");
  }
}

void handleN2kWaterDepth(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  double depthBelowTransducer = N2kDoubleNA;
  double offset = N2kDoubleNA;

  if (ParseN2kWaterDepth(message, sid, depthBelowTransducer, offset)) {
    if (N2kIsNA(depthBelowTransducer)) {
      n2kRx.depthM = NAN;
    } else {
      n2kRx.depthM = static_cast<float>(
          depthBelowTransducer + (N2kIsNA(offset) ? 0.0 : offset));
    }
    n2kRx.depthLastMs = now;
    rememberN2kLine("128267 depth", n2kRx.depthM, "m");
  }
}

void handleN2kCogSog(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  tN2kHeadingReference headingReference;
  double cog = N2kDoubleNA;
  double sog = N2kDoubleNA;

  if (ParseN2kCOGSOGRapid(message, sid, headingReference, cog, sog)) {
    n2kRx.cogDeg = radiansToDegreesOrNan(cog);
    n2kRx.sogKn = metersPerSecondToKnotsOrNan(sog);
    n2kRx.cogSogLastMs = now;
    rememberN2kLine("129026 COG", n2kRx.cogDeg, "deg");
    rememberN2kLine("129026 SOG", n2kRx.sogKn, "kt");
  }
}

void updateN2kWaterTemperature(double temperature, uint32_t now, const char* label) {
  n2kRx.waterTempC = kelvinToCelsiusOrNan(temperature);
  if (!isnan(n2kRx.waterTempC)) {
    n2kRx.waterTempLastMs = now;
    rememberN2kLine(label, n2kRx.waterTempC, "C");
  }
}

void handleN2kOutsideEnvironmental(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  double waterTemperature = N2kDoubleNA;
  double outsideAmbientAirTemperature = N2kDoubleNA;
  double atmosphericPressure = N2kDoubleNA;

  if (ParseN2kOutsideEnvironmentalParameters(
          message, sid, waterTemperature, outsideAmbientAirTemperature, atmosphericPressure)) {
    updateN2kWaterTemperature(waterTemperature, now, "130310 water temp");
  }
}

void handleN2kEnvironmental(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  tN2kTempSource tempSource;
  double temperature = N2kDoubleNA;
  tN2kHumiditySource humiditySource;
  double humidity = N2kDoubleNA;
  double atmosphericPressure = N2kDoubleNA;

  if (ParseN2kEnvironmentalParameters(
          message, sid, tempSource, temperature, humiditySource, humidity, atmosphericPressure)
      && tempSource == N2kts_SeaTemperature) {
    updateN2kWaterTemperature(temperature, now, "130311 water temp");
  }
}

void handleN2kTemperature(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  unsigned char tempInstance = 0;
  tN2kTempSource tempSource;
  double actualTemperature = N2kDoubleNA;
  double setTemperature = N2kDoubleNA;

  if (ParseN2kTemperature(message, sid, tempInstance, tempSource, actualTemperature, setTemperature)
      && tempSource == N2kts_SeaTemperature) {
    updateN2kWaterTemperature(actualTemperature, now, "130312 water temp");
  }
}

void handleN2kTemperatureExt(const tN2kMsg& message, uint32_t now) {
  unsigned char sid = 0;
  unsigned char tempInstance = 0;
  tN2kTempSource tempSource;
  double actualTemperature = N2kDoubleNA;
  double setTemperature = N2kDoubleNA;

  if (ParseN2kTemperatureExt(message, sid, tempInstance, tempSource, actualTemperature, setTemperature)
      && tempSource == N2kts_SeaTemperature) {
    updateN2kWaterTemperature(actualTemperature, now, "130316 water temp");
  }
}

void handleNmea2000Message(const tN2kMsg& message) {
  const uint32_t now = millis();
  n2kRx.lastRxMs = now;
  ++n2kRx.rxPacketCount;

  switch (message.PGN) {
    case 127250L:
      handleN2kHeading(message, now);
      break;
    case 128259L:
      handleN2kBoatSpeed(message, now);
      break;
    case 128267L:
      handleN2kWaterDepth(message, now);
      break;
    case 129026L:
      handleN2kCogSog(message, now);
      break;
    case 130310L:
      handleN2kOutsideEnvironmental(message, now);
      break;
    case 130311L:
      handleN2kEnvironmental(message, now);
      break;
    case 130312L:
      handleN2kTemperature(message, now);
      break;
    case 130316L:
      handleN2kTemperatureExt(message, now);
      break;
    default:
      break;
  }
}

void setupNmea2000() {
  NMEA2000.SetProductInformation(&kProductInformation);
  NMEA2000.SetProgmemConfigurationInformation(
      kManufacturerInformation,
      kInstallationDescription1,
      kInstallationDescription2);
  NMEA2000.SetDeviceInformation(
      130042,
      170,
      35,
      2046);
  NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
  NMEA2000.ExtendTransmitMessages(kTransmitMessages);
  NMEA2000.ExtendReceiveMessages(kReceiveMessages);
  NMEA2000.SetMsgHandler(handleNmea2000Message);
  canBusEnabled = NMEA2000.Open();

  if (canBusEnabled) {
    Serial.printf(
        "PWR-CAN enabled on GPIO%d/GPIO%d at %lu bps\n",
        static_cast<int>(kCanTxPin),
        static_cast<int>(kCanRxPin),
        static_cast<unsigned long>(kCanBaudRate));
  } else {
    Serial.println("Failed to enable PWR-CAN");
  }
}

void logCanStatus() {
  if (!canBusEnabled) {
    Serial.println("CAN status: disabled");
    return;
  }

  twai_status_info_t status = {};
  if (twai_get_status_info(&status) != ESP_OK) {
    Serial.println("CAN status: unavailable");
    return;
  }

  Serial.printf(
      "CAN status: state=%d tx_err=%lu rx_err=%lu txq=%lu rxq=%lu bus_err=%lu\n",
      static_cast<int>(status.state),
      static_cast<unsigned long>(status.tx_error_counter),
      static_cast<unsigned long>(status.rx_error_counter),
      static_cast<unsigned long>(status.msgs_to_tx),
      static_cast<unsigned long>(status.msgs_to_rx),
      static_cast<unsigned long>(status.bus_error_count));
}

String contentTypeForPath(const String& path) {
  if (path.endsWith(".html")) {
    return "text/html";
  }
  if (path.endsWith(".css")) {
    return "text/css";
  }
  if (path.endsWith(".js")) {
    return "application/javascript";
  }
  if (path.endsWith(".svg")) {
    return "image/svg+xml";
  }
  if (path.endsWith(".png")) {
    return "image/png";
  }
  if (path.endsWith(".ico")) {
    return "image/x-icon";
  }
  if (path.endsWith(".json")) {
    return "application/json";
  }
  return "application/octet-stream";
}

bool serveFileFromSpiffs(String path) {
  if (path.endsWith("/")) {
    path += "index.html";
  }

  if (!SPIFFS.exists(path)) {
    if (path.indexOf('.') < 0 && SPIFFS.exists("/index.html")) {
      path = "/index.html";
    } else {
      return false;
    }
  }

  File file = SPIFFS.open(path, "r");
  if (!file) {
    return false;
  }

  webServer.streamFile(file, contentTypeForPath(path));
  file.close();
  return true;
}

String buildDataJson() {
  const uint32_t now = millis();
  const bool seatalkIsFresh = seatalkFresh(now);
  const bool dueIsFresh = dueFresh(now);
  const bool windHasAngle = wind.valid && !isnan(wind.rawAngleDeg);
  const float adjustedAwaDeg = windHasAngle ? adjustedWindAngleDeg() : NAN;
  const bool showWindTarget =
      seatalkIsFresh && strcmp(seatalk.mode, "WIND") == 0 && windTarget.valid &&
      !isnan(windTarget.angleDeg);
  const uint32_t seatalkAgeMs = seatalk.lastSeenMs == 0 ? 0xFFFFFFFFUL : now - seatalk.lastSeenMs;
  const uint32_t n2kAgeMs = n2kRx.lastRxMs == 0 ? 0xFFFFFFFFUL : now - n2kRx.lastRxMs;
  const uint32_t windAgeMs = wind.lastPacketMs == 0 ? 0xFFFFFFFFUL : now - wind.lastPacketMs;
  const float headingDeg = valueFresh(now, n2kRx.headingLastMs)
      ? n2kRx.headingDeg
      : (seatalkIsFresh ? seatalk.compassHeadingDeg : NAN);
  const float sogKn = valueFresh(now, n2kRx.cogSogLastMs)
      ? n2kRx.sogKn
      : freshOrNan(n2kRx.sogKn, now, n2kRx.speedLastMs);

  String json;
  json.reserve(1800);
  json += "{";
  appendJsonNumberOrNull(json, "heading", headingDeg, 0);
  json += ",";
  appendJsonNumberOrNull(json, "cog", freshOrNan(n2kRx.cogDeg, now, n2kRx.cogSogLastMs), 0);
  json += ",";
  appendJsonNumberOrNull(json, "sog", sogKn, 1);
  json += ",";
  appendJsonNumberOrNull(json, "awa", adjustedAwaDeg, 0);
  json += ",";
  appendJsonStringOrNull(json, "awaDisplay", formatRelativeWindAngle(adjustedAwaDeg), windHasAngle);
  json += ",";
  appendJsonNumberOrNull(json, "awaRaw", windHasAngle ? wind.rawAngleDeg : NAN, 1);
  json += ",";
  appendJsonNumberOrNull(json, "windOffset", wind.offsetDeg, 1);
  json += ",";
  appendJsonString(json, "windStatus", windStatusString(now));
  json += ",\"windLastSeenMs\":";
  json += String(windAgeMs);
  json += ",";
  appendJsonNumberOrNull(json, "windTargetAngle", showWindTarget ? windTarget.angleDeg : NAN, 0);
  json += ",";
  appendJsonStringOrNull(
      json, "windTargetDisplay", formatRelativeWindAngle(windTarget.angleDeg), showWindTarget);
  json += ",";
  appendJsonNumberOrNull(json, "depth", freshOrNan(n2kRx.depthM, now, n2kRx.depthLastMs), 1);
  json += ",";
  appendJsonNumberOrNull(json, "waterTemp", freshOrNan(n2kRx.waterTempC, now, n2kRx.waterTempLastMs), 1);
  json += ",";
  appendJsonNumberOrNull(json, "rudderAngle", seatalkIsFresh ? seatalk.rudderAngleDeg : NAN, 0);
  json += ",";
  appendJsonNumberOrNull(json, "battery0", batteries[0].present ? batteries[0].voltage : NAN, 2);
  json += ",";
  appendJsonNumberOrNull(json, "battery1", batteries[1].present ? batteries[1].voltage : NAN, 2);
  json += ",";
  appendJsonString(json, "autopilotMode", seatalkIsFresh ? String(seatalk.mode) : String("--"));
  json += ",";
  appendJsonNumberOrNull(json, "targetHeading", seatalkIsFresh ? seatalk.targetHeadingDeg : NAN, 0);
  json += ",";
  appendJsonString(json, "seatalkStatus", seatalkIsFresh ? "ok" : (dueIsFresh ? "stale" : "missing"));
  json += ",";
  appendJsonString(json, "n2kStatus", canBusEnabled && valueFresh(now, n2kRx.lastRxMs, kN2kFreshMs) ? "ok" : "warn");
  json += ",\"seatalkLastSeenMs\":";
  json += String(seatalkAgeMs);
  json += ",\"n2kLastSeenMs\":";
  json += String(n2kAgeMs);
  json += ",\"wifiClients\":";
  json += String(WiFi.softAPgetStationNum());
  json += ",\"uptime\":";
  json += String(now);
  json += ",\"packetCounters\":{";
  json += "\"seatalkRaw\":";
  json += String(seatalk.rawPacketCount);
  json += ",\"seatalkDecoded\":";
  json += String(seatalk.decodedPacketCount);
  json += ",\"n2kPgn\":";
  json += String(n2kPacketCount + n2kRx.rxPacketCount);
  json += ",\"errors\":";
  json += String(seatalk.errorCount);
  json += "},";
  appendJsonLineArray(json, "rawSeatalk", rawSeatalkLines);
  json += ",";
  appendJsonLineArray(json, "decodedSeatalk", decodedSeatalkLines);
  json += ",";
  appendJsonLineArray(json, "rawN2k", rawN2kLines);
  json += "}";
  return json;
}

String extractJsonStringField(const String& body, const String& key) {
  const String quotedKey = "\"" + key + "\"";
  int keyIndex = body.indexOf(quotedKey);
  if (keyIndex < 0) {
    return "";
  }

  int colonIndex = body.indexOf(':', keyIndex + quotedKey.length());
  if (colonIndex < 0) {
    return "";
  }

  int firstQuote = body.indexOf('"', colonIndex + 1);
  if (firstQuote < 0) {
    return "";
  }

  int secondQuote = body.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return "";
  }

  return body.substring(firstQuote + 1, secondQuote);
}

bool extractJsonNumberField(const String& body, const String& key, float& value) {
  const String quotedKey = "\"" + key + "\"";
  int keyIndex = body.indexOf(quotedKey);
  if (keyIndex < 0) {
    return false;
  }

  int colonIndex = body.indexOf(':', keyIndex + quotedKey.length());
  if (colonIndex < 0) {
    return false;
  }

  const char* start = body.c_str() + colonIndex + 1;
  char* end = nullptr;
  value = strtof(start, &end);
  return end != start;
}

bool extractJsonBoolField(const String& body, const String& key, bool& value) {
  const String quotedKey = "\"" + key + "\"";
  int keyIndex = body.indexOf(quotedKey);
  if (keyIndex < 0) {
    return false;
  }

  int colonIndex = body.indexOf(':', keyIndex + quotedKey.length());
  if (colonIndex < 0) {
    return false;
  }

  int valueIndex = colonIndex + 1;
  while (valueIndex < static_cast<int>(body.length()) && isspace(body[valueIndex])) {
    ++valueIndex;
  }

  if (body.substring(valueIndex, valueIndex + 4) == "true") {
    value = true;
    return true;
  }
  if (body.substring(valueIndex, valueIndex + 5) == "false") {
    value = false;
    return true;
  }

  return false;
}

const char* mapAutopilotCommand(const String& command, const String& body) {
  if (command == "standby") {
    return "STBY";
  }
  if (command == "auto") {
    return "AUTO";
  }
  if (command == "wind") {
    return "WIND";
  }
  if (command == "track") {
    return "TRACK";
  }
  if (command == "tack_port") {
    return "TACK_PORT";
  }
  if (command == "tack_starboard") {
    return "TACK_STBD";
  }
  if (command == "heading_delta") {
    float value = 0.0f;
    if (!extractJsonNumberField(body, "value", value)) {
      return nullptr;
    }
    const int delta = static_cast<int>(roundf(value));
    if (delta == -10) {
      return "M10";
    }
    if (delta == -1) {
      return "M1";
    }
    if (delta == 1) {
      return "P1";
    }
    if (delta == 10) {
      return "P10";
    }
  }

  return nullptr;
}

void handleDataRequest() {
  webServer.send(200, "application/json", buildDataJson());
}

String buildWindConfigJson() {
  const uint32_t now = millis();
  const bool windHasAngle = wind.valid && !isnan(wind.rawAngleDeg);
  const float adjustedAwaDeg = windHasAngle ? adjustedWindAngleDeg() : NAN;
  const bool showWindTarget =
      seatalkFresh(now) && strcmp(seatalk.mode, "WIND") == 0 && windTarget.valid &&
      !isnan(windTarget.angleDeg);

  String json;
  json.reserve(360);
  json += "{\"ok\":true,";
  appendJsonNumberOrNull(json, "offset", wind.offsetDeg, 1);
  json += ",";
  appendJsonNumberOrNull(json, "rawAngle", windHasAngle ? wind.rawAngleDeg : NAN, 1);
  json += ",";
  appendJsonNumberOrNull(json, "adjustedAngle", adjustedAwaDeg, 1);
  json += ",";
  appendJsonStringOrNull(json, "display", formatRelativeWindAngle(adjustedAwaDeg), windHasAngle);
  json += ",";
  appendJsonString(json, "status", windStatusString(now));
  json += ",";
  appendJsonNumberOrNull(json, "windTargetAngle", showWindTarget ? windTarget.angleDeg : NAN, 1);
  json += ",";
  appendJsonStringOrNull(
      json, "windTargetDisplay", formatRelativeWindAngle(windTarget.angleDeg), showWindTarget);
  json += "}";
  return json;
}

void handleWindConfigRequest() {
  if (webServer.method() == HTTP_GET) {
    webServer.send(200, "application/json", buildWindConfigJson());
    return;
  }

  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "application/json", "{\"ok\":false,\"error\":\"method_not_allowed\"}");
    return;
  }

  const String body = webServer.arg("plain");
  bool changed = false;
  bool zeroToBow = false;
  float offset = NAN;

  if (extractJsonBoolField(body, "zeroToBow", zeroToBow) && zeroToBow) {
    if (!wind.valid || isnan(wind.rawAngleDeg)) {
      webServer.send(409, "application/json", "{\"ok\":false,\"error\":\"wind_missing\"}");
      return;
    }
    wind.offsetDeg = normalizeSignedDegrees(-wind.rawAngleDeg);
    changed = true;
  } else if (extractJsonNumberField(body, "offset", offset)) {
    wind.offsetDeg = normalizeSignedDegrees(offset);
    changed = true;
  }

  if (!changed) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_offset\"}");
    return;
  }

  saveWindOffset();
  Serial.printf("Wind offset %.1f deg\n", wind.offsetDeg);
  updateDisplay();
  lastDisplayMs = millis();
  webServer.send(200, "application/json", buildWindConfigJson());
}

void handleAutopilotRequest() {
  const String body = webServer.arg("plain");
  const String command = extractJsonStringField(body, "command");
  const char* dueCommand = mapAutopilotCommand(command, body);

  if (dueCommand == nullptr) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"unsupported_command\"}");
    return;
  }

  const bool isStandby = strcmp(dueCommand, "STBY") == 0;
  if (!isStandby && !seatalkFresh(millis())) {
    webServer.send(409, "application/json", "{\"ok\":false,\"error\":\"seatalk_stale\"}");
    return;
  }

  sendDueButtonCommand(dueCommand);
  const uint32_t now = millis();
  if (strcmp(dueCommand, "WIND") == 0) {
    captureWindTarget(now);
  } else if ((strcmp(dueCommand, "TACK_PORT") == 0 || strcmp(dueCommand, "TACK_STBD") == 0) &&
             strcmp(seatalk.mode, "WIND") == 0) {
    mirrorWindTarget(now);
  } else if (command == "heading_delta" && strcmp(seatalk.mode, "WIND") == 0) {
    float value = 0.0f;
    if (extractJsonNumberField(body, "value", value)) {
      adjustWindTarget(value, now);
    }
  }
  webServer.send(202, "application/json", "{\"ok\":true}");
}

void handleNotFound() {
  if (serveFileFromSpiffs(webServer.uri())) {
    return;
  }

  webServer.send(404, "text/plain", "Not found");
}

void setupSerial() {
  Serial.begin(kSerialBaud);
  const uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < kSerialWaitMs) {
    delay(10);
  }
  delay(kBootSettleMs);
}

void setupDueSerial() {
  dueSerial.begin(kDueSerialBaud, SERIAL_8N1, kDueRxPin, kDueTxPin);
  Serial.printf(
      "Due serial bridge on PLC G%u/RX and G%u/TX at %lu baud\n",
      kDueRxPin,
      kDueTxPin,
      static_cast<unsigned long>(kDueSerialBaud));
}

void setupDisplay() {
  auto cfg = M5.config();
  cfg.serial_baudrate = kSerialBaud;
  cfg.external_rtc = false;
  cfg.external_imu = false;
  cfg.internal_rtc = false;
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.internal_spk = false;
  cfg.led_brightness = 0;
  M5.begin(cfg);

  auto& ioexp = M5.getIOExpander(0);
  ioexp.setDirection(7, true);
  ioexp.setPullMode(7, false);
  ioexp.setHighImpedance(7, false);
  ioexp.digitalWrite(7, false);

  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }
  M5.Display.setTextFont(2);
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(TFT_BLACK);
}

void setupWiFiAp() {
  snprintf(apSsid, sizeof(apSsid), "%s-%s", kApSsid, canBusEnabled ? "CANOK" : "CANFAIL");

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPdisconnect(true);
  WiFi.softAPConfig(kApIp, kApGateway, kApSubnet);
  WiFi.softAP(apSsid, kApPassword);
  windUdp.begin(kWindUdpPort);

  Serial.printf("WiFi AP SSID: %s\n", apSsid);
  Serial.printf("WiFi AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("Wind UDP port: %u\n", kWindUdpPort);
}

void setupWebServer() {
  if (!SPIFFS.begin(false)) {
    Serial.println("SPIFFS mount failed; dashboard files unavailable");
  } else {
    Serial.printf("SPIFFS mounted: %lu/%lu bytes used\n",
                  static_cast<unsigned long>(SPIFFS.usedBytes()),
                  static_cast<unsigned long>(SPIFFS.totalBytes()));
  }

  webServer.on("/data", HTTP_GET, handleDataRequest);
  webServer.on("/api/autopilot", HTTP_POST, handleAutopilotRequest);
  webServer.on("/api/wind-config", HTTP_GET, handleWindConfigRequest);
  webServer.on("/api/wind-config", HTTP_POST, handleWindConfigRequest);
  webServer.onNotFound(handleNotFound);
  webServer.begin();

  Serial.println("HTTP server started on port 80");
}

void setupPlcRelays() {
  if (!plcIoExpander.begin()) {
    Serial.println("PLC relay expander not found");
    return;
  }

  plcIoExpander.configureDirection(0x0000);
  plcIoExpander.openDrainPort0(false);
  plcIoExpander.interruptEnableGPIO(0x0000);

  for (uint8_t channel = 0; channel < 4; ++channel) {
    plcIoExpander.pinMode(channel, AW9523_Class::AW_OUTPUT);
    plcIoExpander.digitalWrite(channel, false);
  }

  plcIoExpander.disableIrq();
  plcIoExpander.digitalWrite(kPlcRelay1Channel, true);
  plcIoExpander.digitalWrite(kPlcRelay2Channel, true);

  Serial.println("PLC relay 1 ON");
  Serial.println("PLC relay 2 ON");
}

}  // namespace

void setup() {
  setupSerial();
  setupDueSerial();
  setupDisplay();

  Serial.println();
  Serial.println("M5StampPLC battery and wind monitor");

  preferences.begin(kPreferencesNamespace, false);
  loadWindOffset();

  for (auto& battery : batteries) {
    initBattery(battery);
  }

  setupPlcRelays();
  setupNmea2000();
  setupWiFiAp();
  setupWebServer();
  updateDisplay();

  const uint32_t now = millis();
  lastBatterySampleMs = now;
  lastBatterySendMs = now;
  lastWindSendMs = now;
  lastDisplayMs = now;
}

void loop() {
  const uint32_t now = millis();

  M5.update();
  handleButtons();
  receiveWindPackets();
  serviceDueSerial();
  webServer.handleClient();

  if (now - lastBatterySampleMs >= kBatterySamplePeriodMs) {
    sampleBatteries();
    lastBatterySampleMs = now;
  }

  if (now - lastBatterySendMs >= kBatterySendPeriodMs) {
    sendBatteryStatuses();
    lastBatterySendMs = now;
  }

  if (now - lastWindSendMs >= kWindSendPeriodMs) {
    sendWindStatus();
    lastWindSendMs = now;
  }

  if (now - lastDisplayMs >= kDisplayPeriodMs) {
    updateDisplay();
    lastDisplayMs = now;
  }

  if (now - lastCanStatusMs >= 5000) {
    logCanStatus();
    lastCanStatusMs = now;
  }

  if (canBusEnabled) {
    NMEA2000.ParseMessages();
  }
}
