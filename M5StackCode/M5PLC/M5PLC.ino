#include <Arduino.h>
#include <M5StamPLC.h>
#include <INA226.h>
#include <Wire.h>
#include <AS5600.h>

void scanBus(TwoWire& bus, const char* name) {
  Serial.printf("\nScanning %s\n", name);
  for (uint8_t addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Serial.printf("Found device at 0x%02X on %s\n", addr, name);
    }
  }
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

  uint8_t priority = 2;
  uint8_t source = 0x23;
  uint32_t pgn = 130306;

  n2kdata.identifier = calcFrameId(priority, pgn, source);

  // SID
  n2kdata.data[0] = 0xFF;

  // speed in 0.01 m/s
  uint16_t speedN2K = (uint16_t)lround(speedKnots * 0.514444444 * 100.0);
  n2kdata.data[1] = speedN2K & 0xFF;
  n2kdata.data[2] = (speedN2K >> 8) & 0xFF;

  // angle in 0.0001 rad
  uint16_t angleN2K = (uint16_t)lround((directionDeg * M_PI / 180.0) * 10000.0);
  n2kdata.data[3] = angleN2K & 0xFF;
  n2kdata.data[4] = (angleN2K >> 8) & 0xFF;

  // ref = 2 apparent, rest reserved
  n2kdata.data[5] = 2;
  n2kdata.data[6] = 0xFF;
  n2kdata.data[7] = 0xFF;

  return n2kdata;
}

twai_message_t batteryStatusFrame(uint8_t instance, double voltage, double current = 0.0) {
  twai_message_t n2kdata = {};
  n2kdata.extd = 1;
  n2kdata.data_length_code = 8;

  uint8_t priority = 6;
  uint8_t source = 0x23;
  uint32_t pgn = 127508;

  n2kdata.identifier = calcFrameId(priority, pgn, source);

  int16_t voltN2K = (int16_t)lround(voltage * 100.0);  // 0.01 V
  int16_t ampN2K  = (int16_t)lround(current * 10.0);   // 0.1 A

  n2kdata.data[0] = instance;

  n2kdata.data[1] = voltN2K & 0xFF;
  n2kdata.data[2] = (voltN2K >> 8) & 0xFF;

  n2kdata.data[3] = ampN2K & 0xFF;
  n2kdata.data[4] = (ampN2K >> 8) & 0xFF;

  // temp + SID not available yet
  n2kdata.data[5] = 0xFF;
  n2kdata.data[6] = 0xFF;
  n2kdata.data[7] = 0xFF;

  return n2kdata;
}

esp_err_t postCanMessage(twai_message_t& message, TickType_t timeout_ticks = pdMS_TO_TICKS(1000)) {
  return twai_transmit(&message, timeout_ticks);
}

float getAngleDegrees(AS5600& encoder) {
  float angle = encoder.rawAngle() * 360.0f / 4096.0f;
  angle += 0.0f;  // offset here

  while (angle < 0.0f) angle += 360.0f;
  while (angle >= 360.0f) angle -= 360.0f;

  return angle;
}

TwoWire i2cA = TwoWire(0);
TwoWire i2cC = TwoWire(1);

INA226 batt1(0x41, &i2cA);
INA226 batt2(0x41, &i2cC);
AS5600 encoder(&i2cA);

uint32_t timebatt0 = 0;
uint32_t timebatt1 = 0;
uint32_t timeWind  = 0;

void setup() {
  Serial.begin(115200);

  auto config = M5StamPLC.config();
  config.enableCan = true;
  config.canBaudRate = 250000;
  M5StamPLC.config(config);
  M5StamPLC.begin();

  i2cA.setPins(2, 1);
  i2cC.setPins(5, 4);

  i2cA.begin();
  i2cC.begin();

  scanBus(i2cA, "PORT.A");
  scanBus(i2cC, "PORT.C");

  encoder.begin();

  if (!batt1.begin()) {
    Serial.println("Battery sensor 1 not found");
  } else {
    Serial.println("Battery sensor 1 OK");
  }

  if (!batt2.begin()) {
    Serial.println("Battery sensor 2 not found");
  } else {
    Serial.println("Battery sensor 2 OK");
  }
}

void loop() {
  if (millis() - timebatt0 > 1500) {
    double voltage0 = batt1.getBusVoltage();
    twai_message_t msg = batteryStatusFrame(0, voltage0);
    if (postCanMessage(msg) != ESP_OK) {
      Serial.println("Failed CAN Send batt0");
    }
    timebatt0 = millis();
  }

  if (millis() - timebatt1 > 1500) {
    double voltage1 = batt2.getBusVoltage();
    twai_message_t msg = batteryStatusFrame(1, voltage1);
    if (postCanMessage(msg) != ESP_OK) {
      Serial.println("Failed CAN Send batt1");
    }
    timebatt1 = millis();
  }

  if (millis() - timeWind > 100) {
    double windDeg = getAngleDegrees(encoder);
    twai_message_t msg = apparentWindFrame(windDeg, 10.0);
    if (postCanMessage(msg) != ESP_OK) {
      Serial.println("Failed CAN Send wind");
    }
    timeWind = millis();
  }
}