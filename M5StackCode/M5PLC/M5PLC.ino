#include <Arduino.h>
#include <M5StamPLC.h>
#include <INA226.h>
#include <Wire.h>
#include <AS5600.h>
#include <math.h>

// -------------------------
// user settings
// -------------------------
static const bool CAN_TX_ENABLED = false;   // set true only when real CAN bus is connected
static const uint32_t CAN_BAUD = 250000;
static const float WIND_SPEED_KNOTS = 10.0f;
static const float ANGLE_OFFSET_DEG = 0.0f;

// -------------------------
// helpers
// -------------------------
void scanBus(TwoWire& bus, const char* name) {
  Serial.printf("\nScanning %s\n", name);
  for (uint8_t addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Serial.printf("Found device at 0x%02X on %s\n", addr, name);
    }
  }
}

bool isI2CDevicePresent(TwoWire& bus, uint8_t addr) {
  bus.beginTransmission(addr);
  return (bus.endTransmission() == 0);
}

uint32_t calcFrameId(uint8_t priority, uint32_t pgn, uint8_t source) {
  priority &= 0x07;
  pgn &= 0x3FFFF;
  return ((uint32_t)priority << 26) | (pgn << 8) | source;
}

twai_message_t apparentWindFrame(double directionDeg, double speedKnots) {
  twai_message_t n2kdata = {};
  n2kdata.extd = 1;
  n2kdata.data_length_code = 8;

  const uint8_t priority = 2;
  const uint8_t source = 0x23;
  const uint32_t pgn = 130306;

  n2kdata.identifier = calcFrameId(priority, pgn, source);

  n2kdata.data[0] = 0xFF;  // SID

  uint16_t speedN2K = (uint16_t)lround(speedKnots * 0.514444444 * 100.0);
  n2kdata.data[1] = speedN2K & 0xFF;
  n2kdata.data[2] = (speedN2K >> 8) & 0xFF;

  uint16_t angleN2K = (uint16_t)lround((directionDeg * M_PI / 180.0) * 10000.0);
  n2kdata.data[3] = angleN2K & 0xFF;
  n2kdata.data[4] = (angleN2K >> 8) & 0xFF;

  n2kdata.data[5] = 2;     // apparent
  n2kdata.data[6] = 0xFF;
  n2kdata.data[7] = 0xFF;

  return n2kdata;
}

twai_message_t batteryStatusFrame(uint8_t instance, double voltage, double current = 0.0) {
  twai_message_t n2kdata = {};
  n2kdata.extd = 1;
  n2kdata.data_length_code = 8;

  const uint8_t priority = 6;
  const uint8_t source = 0x23;
  const uint32_t pgn = 127508;

  n2kdata.identifier = calcFrameId(priority, pgn, source);

  int16_t voltN2K = (int16_t)lround(voltage * 100.0);
  int16_t ampN2K  = (int16_t)lround(current * 10.0);

  n2kdata.data[0] = instance;
  n2kdata.data[1] = voltN2K & 0xFF;
  n2kdata.data[2] = (voltN2K >> 8) & 0xFF;
  n2kdata.data[3] = ampN2K & 0xFF;
  n2kdata.data[4] = (ampN2K >> 8) & 0xFF;
  n2kdata.data[5] = 0xFF;
  n2kdata.data[6] = 0xFF;
  n2kdata.data[7] = 0xFF;

  return n2kdata;
}

esp_err_t postCanMessage(twai_message_t& message, TickType_t timeout_ticks = pdMS_TO_TICKS(50)) {
  if (!CAN_TX_ENABLED) {
    return ESP_OK;
  }
  return twai_transmit(&message, timeout_ticks);
}

void printCanStatus(const char* tag) {
  if (!CAN_TX_ENABLED) return;

  twai_status_info_t status;
  esp_err_t err = twai_get_status_info(&status);
  if (err == ESP_OK) {
    Serial.printf(
      "[%s] CAN state=%d tx_err=%d rx_err=%d tx_failed=%d tx_pending=%d rx_missed=%d rx_overrun=%d\n",
      tag,
      status.state,
      status.tx_error_counter,
      status.rx_error_counter,
      status.tx_failed_count,
      status.msgs_to_tx,
      status.rx_missed_count,
      status.rx_overrun_count
    );
  } else {
    Serial.printf("[%s] twai_get_status_info failed: %d\n", tag, (int)err);
  }
}

float getAngleDegrees(AS5600& enc) {
  float angle = enc.rawAngle() * 360.0f / 4096.0f;
  angle += ANGLE_OFFSET_DEG;

  while (angle < 0.0f) angle += 360.0f;
  while (angle >= 360.0f) angle -= 360.0f;

  return angle;
}

// -------------------------
// I2C buses / devices
// -------------------------
TwoWire i2cA = TwoWire(0);   // PORT.A: SDA=G2, SCL=G1
TwoWire i2cC = TwoWire(1);   // PORT.C: SDA=G5, SCL=G4

INA226 batt1(0x41, &i2cA);
INA226 batt2(0x41, &i2cC);
AS5600 encoder(&i2cA);

// -------------------------
// runtime state
// -------------------------
bool batt1Present = false;
bool batt2Present = false;
bool encoderPresent = false;

double latestBatt0 = 0.0;
double latestBatt1 = 0.0;
double latestWindDeg = 0.0;

uint32_t timeBatt0 = 0;
uint32_t timeBatt1 = 0;
uint32_t timeWind = 0;
uint32_t timeScreen = 0;

// -------------------------
// display
// -------------------------
void initDisplay() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);
  M5StamPLC.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5StamPLC.Display.setTextSize(2);
}

void drawDisplay() {
  M5StamPLC.Display.fillScreen(TFT_BLACK);

  M5StamPLC.Display.setTextSize(2);
  M5StamPLC.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  M5StamPLC.Display.setCursor(10, 10);
  M5StamPLC.Display.println("BF50 Monitor");

  M5StamPLC.Display.setCursor(10, 40);
  M5StamPLC.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  if (batt1Present) {
    M5StamPLC.Display.printf("Batt0: %.2f V\n", latestBatt0);
  } else {
    M5StamPLC.Display.printf("Batt0: not found\n");
  }

  M5StamPLC.Display.setCursor(10, 70);
  M5StamPLC.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  if (batt2Present) {
    M5StamPLC.Display.printf("Batt1: %.2f V\n", latestBatt1);
  } else {
    M5StamPLC.Display.printf("Batt1: not found\n");
  }

  M5StamPLC.Display.setCursor(10, 100);
  M5StamPLC.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  if (encoderPresent) {
    M5StamPLC.Display.printf("Angle: %.1f deg\n", latestWindDeg);
  } else {
    M5StamPLC.Display.printf("Angle: not found\n");
  }

  M5StamPLC.Display.setCursor(10, 130);
  M5StamPLC.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5StamPLC.Display.printf("CAN: %s %luk\n", CAN_TX_ENABLED ? "ON" : "OFF", CAN_BAUD / 1000);

  M5StamPLC.Display.setCursor(10, 160);
  M5StamPLC.Display.printf("A:0x41 %s 0x36 %s\n",
                           batt1Present ? "OK" : "--",
                           encoderPresent ? "OK" : "--");

  M5StamPLC.Display.setCursor(10, 190);
  M5StamPLC.Display.printf("C:0x41 %s\n", batt2Present ? "OK" : "--");
}

// -------------------------
// setup
// -------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  auto config = M5StamPLC.config();
  config.enableCan = CAN_TX_ENABLED;
  config.canBaudRate = CAN_BAUD;
  M5StamPLC.config(config);
  M5StamPLC.begin();

  initDisplay();

  i2cA.setPins(2, 1);
  i2cC.setPins(5, 4);
  i2cA.begin();
  i2cC.begin();

  scanBus(i2cA, "PORT.A");
  scanBus(i2cC, "PORT.C");

  batt1Present = isI2CDevicePresent(i2cA, 0x41);
  batt2Present = isI2CDevicePresent(i2cC, 0x41);
  encoderPresent = isI2CDevicePresent(i2cA, 0x36);

  if (batt1Present) {
    if (batt1.begin()) {
      Serial.println("Battery sensor 1 OK");
      latestBatt0 = batt1.getBusVoltage();
    } else {
      Serial.println("Battery sensor 1 detected but begin() failed");
      batt1Present = false;
    }
  } else {
    Serial.println("Battery sensor 1 not found");
  }

  if (batt2Present) {
    if (batt2.begin()) {
      Serial.println("Battery sensor 2 OK");
      latestBatt1 = batt2.getBusVoltage();
    } else {
      Serial.println("Battery sensor 2 detected but begin() failed");
      batt2Present = false;
    }
  } else {
    Serial.println("Battery sensor 2 not found");
  }

  if (encoderPresent) {
    encoder.begin();
    latestWindDeg = getAngleDegrees(encoder);
    Serial.println("AS5600 encoder OK");
  } else {
    Serial.println("AS5600 encoder not found");
  }

  if (CAN_TX_ENABLED) {
    Serial.printf("CAN TX enabled at %lu bps\n", CAN_BAUD);
  } else {
    Serial.println("CAN TX disabled in software");
  }

  drawDisplay();
}

// -------------------------
// loop
// -------------------------
void loop() {
  if (batt1Present && (millis() - timeBatt0 > 1500)) {
    latestBatt0 = batt1.getBusVoltage();

    if (CAN_TX_ENABLED) {
      twai_message_t msg = batteryStatusFrame(0, latestBatt0);
      if (postCanMessage(msg) != ESP_OK) {
        Serial.println("Failed CAN Send batt0");
        printCanStatus("batt0");
      }
    }

    timeBatt0 = millis();
  }

  if (batt2Present && (millis() - timeBatt1 > 1500)) {
    latestBatt1 = batt2.getBusVoltage();

    if (CAN_TX_ENABLED) {
      twai_message_t msg = batteryStatusFrame(1, latestBatt1);
      if (postCanMessage(msg) != ESP_OK) {
        Serial.println("Failed CAN Send batt1");
        printCanStatus("batt1");
      }
    }

    timeBatt1 = millis();
  }

  if (encoderPresent && (millis() - timeWind > 100)) {
    latestWindDeg = getAngleDegrees(encoder);

    if (CAN_TX_ENABLED) {
      twai_message_t msg = apparentWindFrame(latestWindDeg, WIND_SPEED_KNOTS);
      if (postCanMessage(msg) != ESP_OK) {
        Serial.println("Failed CAN Send wind");
        printCanStatus("wind");
      }
    }

    timeWind = millis();
  }

  if (millis() - timeScreen > 200) {
    drawDisplay();
    timeScreen = millis();
  }
}