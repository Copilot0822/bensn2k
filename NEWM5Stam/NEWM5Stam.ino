#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
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

TwoWire& externalBus = Wire;
WiFiUDP windUdp;
Preferences preferences;
NMEA2000_esp32_twai NMEA2000(kCanTxPin, kCanRxPin);
AW9523_Class plcIoExpander;
char apSsid[32] = {};

BatteryChannel batteries[] = {
    {"Port A", 2, 1, 0, false, NAN, 0},
    {"Port C", 5, 4, 1, false, NAN, 0},
};

constexpr size_t kBatteryCount = sizeof(batteries) / sizeof(batteries[0]);

WindState wind = {false, NAN, 0.0f, 0, 0, 0, IPAddress(0, 0, 0, 0)};
DisplayMode displayMode = DisplayMode::Summary;
bool canBusEnabled = false;

uint32_t lastBatterySampleMs = 0;
uint32_t lastBatterySendMs = 0;
uint32_t lastWindSendMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastCanStatusMs = 0;
int activeSdaPin = -1;
int activeSclPin = -1;

const unsigned long kTransmitMessages[] PROGMEM = {127508L, 130306L, 0};

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

bool windFresh(uint32_t now) {
  return wind.valid && (now - wind.lastPacketMs <= kWindFreshMs);
}

float adjustedWindAngleDeg() {
  if (!wind.valid || isnan(wind.rawAngleDeg)) {
    return NAN;
  }
  return wrapDegrees(wind.rawAngleDeg + wind.offsetDeg);
}

String formatIp(const IPAddress& ip) {
  return ip.toString();
}

void saveWindOffset() {
  preferences.putFloat(kPreferencesOffsetKey, wind.offsetDeg);
}

void loadWindOffset() {
  wind.offsetDeg = wrapDegrees(preferences.getFloat(kPreferencesOffsetKey, 0.0f));
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

  if (windFresh(now)) {
    printLine(marginX, y, TFT_YELLOW, "AWA " + String(adjustedWindAngleDeg(), 1) + " deg");
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

  if (windFresh(now)) {
    printLine(marginX, y, TFT_YELLOW, "Adj " + String(adjustedWindAngleDeg(), 1) + " deg");
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

void sendWindStatus() {
  if (!canBusEnabled) {
    return;
  }

  tN2kMsg message;
  const uint32_t now = millis();
  const double windAngle = windFresh(now) ? adjustedWindAngleDeg() * M_PI / 180.0 : N2kDoubleNA;

  SetN2kWindSpeed(message, wind.sid++, N2kDoubleNA, windAngle, N2kWind_Apparent);
  NMEA2000.SendMsg(message);
}

void handleButtons() {
  bool changed = false;

  if (M5.BtnA.wasClicked()) {
    wind.offsetDeg = wrapDegrees(wind.offsetDeg + kOffsetStepDeg);
    saveWindOffset();
    Serial.printf("Wind offset %.1f deg\n", wind.offsetDeg);
    changed = true;
  }

  if (M5.BtnB.wasClicked()) {
    wind.offsetDeg = wrapDegrees(wind.offsetDeg - kOffsetStepDeg);
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

void setupSerial() {
  Serial.begin(kSerialBaud);
  const uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < kSerialWaitMs) {
    delay(10);
  }
  delay(kBootSettleMs);
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
