#include <Wire.h>
#include <due_can.h>

#include "N2KFrames.h"

#define CAN_BUS_SPEED 250000
#define SEND_PERIOD_MS 200
#define I2C_CLOCK_HZ 400000

enum EncoderProfile {
  ENCODER_AS5600,
  ENCODER_CUSTOM
};

const EncoderProfile ENCODER_PROFILE = ENCODER_AS5600;

const float OFFSET_DEG = 0.0f;
const float WIND_SPEED_KNOTS = 8.0f;

const bool INVERT_DIRECTION = false;

// Custom profile values used when ENCODER_PROFILE == ENCODER_CUSTOM.
const uint8_t CUSTOM_I2C_ADDR = 0x36;
const uint8_t CUSTOM_ANGLE_REG_MSB = 0x0C;
const uint8_t CUSTOM_ANGLE_REG_LSB = 0x0D;
const uint8_t CUSTOM_ANGLE_BYTES = 2;
const bool CUSTOM_MSB_FIRST = true;
const uint16_t CUSTOM_ANGLE_MASK = 0x0FFF;
const uint8_t CUSTOM_ANGLE_SHIFT = 0;
const uint16_t CUSTOM_COUNTS_PER_REV = 4096;

struct EncoderConfig {
  uint8_t address;
  uint8_t reg_msb;
  uint8_t reg_lsb;
  uint8_t bytes;
  bool msb_first;
  uint16_t mask;
  uint8_t shift;
  uint16_t counts_per_rev;
};

EncoderConfig encoder;

EncoderConfig getEncoderConfig() {
  switch (ENCODER_PROFILE) {
    case ENCODER_AS5600:
      return EncoderConfig{
        0x36,
        0x0C,
        0x0D,
        2,
        true,
        0x0FFF,
        0,
        4096
      };
    case ENCODER_CUSTOM:
    default:
      return EncoderConfig{
        CUSTOM_I2C_ADDR,
        CUSTOM_ANGLE_REG_MSB,
        CUSTOM_ANGLE_REG_LSB,
        CUSTOM_ANGLE_BYTES,
        CUSTOM_MSB_FIRST,
        CUSTOM_ANGLE_MASK,
        CUSTOM_ANGLE_SHIFT,
        CUSTOM_COUNTS_PER_REV
      };
  }
}

float wrapDegrees(float degrees) {
  while (degrees < 0.0f) {
    degrees += 360.0f;
  }
  while (degrees >= 360.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

bool readEncoderRaw(uint16_t &raw) {
  uint8_t bytes = encoder.bytes == 0 ? 2 : encoder.bytes;
  uint16_t value = 0;
  if (bytes == 1) {
    Wire.beginTransmission(encoder.address);
    Wire.write(encoder.reg_msb);
    if (Wire.endTransmission(false) != 0) {
      return false;
    }
    uint8_t received = Wire.requestFrom((int)encoder.address, 1);
    if (received != 1) {
      return false;
    }
    value = Wire.read();
  } else {
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    if (encoder.reg_lsb != encoder.reg_msb + 1) {
      Wire.beginTransmission(encoder.address);
      Wire.write(encoder.reg_msb);
      if (Wire.endTransmission(false) != 0) {
        return false;
      }
      if (Wire.requestFrom((int)encoder.address, 1) != 1) {
        return false;
      }
      b0 = Wire.read();

      Wire.beginTransmission(encoder.address);
      Wire.write(encoder.reg_lsb);
      if (Wire.endTransmission(false) != 0) {
        return false;
      }
      if (Wire.requestFrom((int)encoder.address, 1) != 1) {
        return false;
      }
      b1 = Wire.read();
    } else {
      Wire.beginTransmission(encoder.address);
      Wire.write(encoder.reg_msb);
      if (Wire.endTransmission(false) != 0) {
        return false;
      }
      uint8_t received = Wire.requestFrom((int)encoder.address, 2);
      if (received != 2) {
        return false;
      }
      b0 = Wire.read();
      b1 = Wire.read();
    }

    value = encoder.msb_first ? (static_cast<uint16_t>(b0) << 8) | b1
                              : (static_cast<uint16_t>(b1) << 8) | b0;
  }

  value = static_cast<uint16_t>((value & encoder.mask) >> encoder.shift);
  raw = value;
  return true;
}

float encoderDegrees(uint16_t raw) {
  if (encoder.counts_per_rev == 0) {
    return 0.0f;
  }
  return (static_cast<float>(raw) * 360.0f) / static_cast<float>(encoder.counts_per_rev);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(I2C_CLOCK_HZ);
  encoder = getEncoderConfig();

  Can0.begin(CAN_BUS_SPEED);
}

void loop() {
  static uint32_t lastSendMs = 0;
  uint32_t now = millis();
  if (now - lastSendMs < SEND_PERIOD_MS) {
    return;
  }
  lastSendMs = now;

  uint16_t raw = 0;
  if (!readEncoderRaw(raw)) {
    Serial.println("Encoder read failed");
    return;
  }

  float direction = encoderDegrees(raw);
  if (INVERT_DIRECTION) {
    direction = 360.0f - direction;
  }
  direction = wrapDegrees(direction + OFFSET_DEG);

  CAN_FRAME wind = apparentWindFrame(direction, WIND_SPEED_KNOTS);
  Can0.sendFrame(wind);

  Serial.print("Wind direction deg: ");
  Serial.println(direction, 2);
}
