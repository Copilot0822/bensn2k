#include <Arduino.h>
#include <Wire.h>
#include <M5Unified.hpp>
#include <NMEA2000.h>
#include <N2kMessages.h>
#include <NMEA2000_esp32_twai.h>

namespace {

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kI2CFrequency = 100000;
constexpr uint32_t kBootSettleMs = 500;
constexpr uint32_t kSerialWaitMs = 15000;
constexpr uint32_t kSamplePeriodMs = 500;
constexpr uint32_t kSendPeriodMs = 1500;
constexpr uint32_t kDisplayPeriodMs = 500;
constexpr uint8_t kInaAddress = 0x41;
constexpr gpio_num_t kCanTxPin = GPIO_NUM_42;
constexpr gpio_num_t kCanRxPin = GPIO_NUM_43;

TwoWire& externalBus = Wire;
NMEA2000_esp32_twai NMEA2000(kCanTxPin, kCanRxPin);

struct BatteryChannel {
  const char* name;
  int sdaPin;
  int sclPin;
  uint8_t instance;
  bool present;
  float voltage;
  uint8_t sid;
};

BatteryChannel batteries[] = {
    {"Port A", 2, 1, 0, false, NAN, 0},
    {"Port C", 5, 4, 1, false, NAN, 0},
};

uint32_t lastSampleMs = 0;
uint32_t lastSendMs = 0;
uint32_t lastDisplayMs = 0;
int activeSdaPin = -1;
int activeSclPin = -1;

const unsigned long kTransmitMessages[] PROGMEM = {127508L, 0};

const tNMEA2000::tProductInformation kProductInformation PROGMEM = {
    2100,
    100,
    "M5StampPLC Battery Monitor",
    "1.0.0",
    "1.0.0",
    "M5STAMPLC-BAT",
    1,
    1,
};

const char kManufacturerInformation[] PROGMEM = "Copilot0822";
const char kInstallationDescription1[] PROGMEM = "Port A=Batt 0, Port C=Batt 1";
const char kInstallationDescription2[] PROGMEM = "INA226 to NMEA2000 PGN 127508";

void updateDisplay() {
  const int width = M5.Display.width();
  const int height = M5.Display.height();
  const int marginX = 8;
  const int marginY = 6;
  const int headerHeight = 18;
  const int footerHeight = 14;
  const int contentTop = marginY + headerHeight + 4;
  const int contentHeight = height - contentTop - footerHeight - marginY;
  const int rowHeight = contentHeight / 2;

  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextFont(2);
  M5.Display.setTextSize(1);
  M5.Display.setTextWrap(false);
  M5.Display.setCursor(marginX, marginY);
  M5.Display.println("Battery Monitor");

  for (size_t i = 0; i < (sizeof(batteries) / sizeof(batteries[0])); ++i) {
    const auto& battery = batteries[i];
    const int rowTop = contentTop + static_cast<int>(i) * rowHeight;
    const int valueY = rowTop + 16;
    const int statusY = rowTop + 32;

    M5.Display.drawFastHLine(0, rowTop - 3, width, TFT_DARKGREY);

    M5.Display.setCursor(marginX, rowTop);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.printf("%s  inst %u", battery.name, battery.instance);

    M5.Display.setCursor(marginX, valueY);
    if (battery.present && !isnan(battery.voltage)) {
      M5.Display.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
      M5.Display.printf("Voltage %.3f V", battery.voltage);
    } else {
      M5.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
      M5.Display.print("Voltage not found");
    }

    M5.Display.setCursor(marginX, statusY);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.printf("N2K instance %u", battery.instance);
  }

  M5.Display.drawFastHLine(0, height - footerHeight - 2, width, TFT_DARKGREY);
  M5.Display.setCursor(marginX, height - footerHeight);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Display.println("PGN 127508");
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
  return readRegister16(kInaAddress, 0xFE, manufacturerId)
      && readRegister16(kInaAddress, 0xFF, dieId)
      && manufacturerId == 0x5449
      && dieId == 0x2260;
}

bool configureIna226() {
  constexpr uint16_t kConfig = 0x4527;
  constexpr uint16_t kCalibration = 0x0800;
  return writeRegister16(kInaAddress, 0x00, kConfig)
      && writeRegister16(kInaAddress, 0x05, kCalibration);
}

bool readBusVoltage(float& voltage) {
  uint16_t raw = 0;
  if (!readRegister16(kInaAddress, 0x02, raw)) {
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

void sendBatteryStatus(BatteryChannel& battery) {
  tN2kMsg message;
  const double voltage = battery.present ? battery.voltage : N2kDoubleNA;

  SetN2kPGN127508(message, battery.instance, voltage, N2kDoubleNA, N2kDoubleNA, battery.sid++);
  NMEA2000.SendMsg(message);

  Serial.printf("%s -> instance %u voltage=", battery.name, battery.instance);
  if (battery.present && !isnan(battery.voltage)) {
    Serial.printf("%.3f V\n", battery.voltage);
  } else {
    Serial.println("NA");
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
  NMEA2000.Open();
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
  cfg.serial_baudrate = 0;
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
  ioexp.digitalWrite(7, false);  // StampPLC backlight is active low

  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }
  M5.Display.setTextFont(2);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(TFT_BLACK);
}

}  // namespace

void setup() {
  setupSerial();
  setupDisplay();

  Serial.println();
  Serial.println("M5StampPLC dual INA226 battery monitor");

  for (auto& battery : batteries) {
    initBattery(battery);
  }

  setupNmea2000();
  updateDisplay();
  lastSampleMs = millis();
  lastSendMs = millis();
  lastDisplayMs = millis();
}

void loop() {
  const uint32_t now = millis();

  if (now - lastSampleMs >= kSamplePeriodMs) {
    for (auto& battery : batteries) {
      sampleBattery(battery);
    }
    lastSampleMs = now;
  }

  if (now - lastSendMs >= kSendPeriodMs) {
    for (auto& battery : batteries) {
      sendBatteryStatus(battery);
    }
    lastSendMs = now;
  }

  if (now - lastDisplayMs >= kDisplayPeriodMs) {
    updateDisplay();
    lastDisplayMs = now;
  }

  NMEA2000.ParseMessages();
  M5.update();
}
