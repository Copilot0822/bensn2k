#ifndef N2K_FRAMES_ESP32_H
#define N2K_FRAMES_ESP32_H

#include <ESP32CAN.h>
#include <cmath>

inline uint32_t calcFrameId(uint8_t priority, uint32_t pgn, uint8_t device) {
  uint8_t x = priority & 0x07;   // 3 bits
  uint32_t y = pgn & 0x3FFFF;    // 18 bits
  uint8_t z = device & 0xFF;     // 8 bits

  return (static_cast<uint32_t>(x) << 26) |
         (static_cast<uint32_t>(y) << 8) |
         static_cast<uint32_t>(z);
}

inline CAN_frame_t apparentWindFrame(double direction_deg, double speed_knots) {
  CAN_frame_t n2kdata = {};
  n2kdata.FIR.B.FF = CAN_frame_ext;
  n2kdata.FIR.B.RTR = CAN_no_RTR;
  n2kdata.FIR.B.DLC = 8;

  const uint8_t priority = 2;
  const uint8_t deviceId = 0x23;
  const uint32_t pgn = 130306; // wind

  n2kdata.MsgID = calcFrameId(priority, pgn, deviceId);

  const uint16_t direction_rads = static_cast<uint16_t>((direction_deg * (M_PI / 180.0)) * 10000.0);
  n2kdata.data.u8[3] = direction_rads & 0xFF;
  n2kdata.data.u8[4] = (direction_rads >> 8) & 0xFF;

  const uint16_t mps = static_cast<uint16_t>((speed_knots * 0.514444444) * 100.0);
  n2kdata.data.u8[1] = mps & 0xFF;
  n2kdata.data.u8[2] = (mps >> 8) & 0xFF;

  n2kdata.data.u8[5] = 2; // apparent

  return n2kdata;
}

inline CAN_frame_t batteryStatusFrame(uint8_t instance, double voltage, double current) {
  CAN_frame_t n2kdata = {};
  n2kdata.FIR.B.FF = CAN_frame_ext;
  n2kdata.FIR.B.RTR = CAN_no_RTR;
  n2kdata.FIR.B.DLC = 8;

  const uint8_t priority = 6;
  const uint8_t deviceId = 0x23;
  const uint32_t pgn = 127508; // battery status

  n2kdata.MsgID = calcFrameId(priority, pgn, deviceId);

  const uint16_t voltN2K = static_cast<uint16_t>(voltage * 100.0);
  const uint16_t ampN2K = static_cast<uint16_t>(current * 10.0);

  n2kdata.data.u8[1] = voltN2K & 0xFF;
  n2kdata.data.u8[2] = (voltN2K >> 8) & 0xFF;

  n2kdata.data.u8[3] = ampN2K & 0xFF;
  n2kdata.data.u8[4] = (ampN2K >> 8) & 0xFF;

  n2kdata.data.u8[0] = instance;

  return n2kdata;
}

inline CAN_frame_t engineParametersRapid(uint8_t instance, double rpm, double trim) {
  CAN_frame_t n2kdata = {};
  n2kdata.FIR.B.FF = CAN_frame_ext;
  n2kdata.FIR.B.RTR = CAN_no_RTR;
  n2kdata.FIR.B.DLC = 8;

  const uint8_t priority = 2;
  const uint8_t deviceId = 0x23;
  const uint32_t pgn = 127488; // engine rapid update

  n2kdata.MsgID = calcFrameId(priority, pgn, deviceId);

  const uint16_t rpmN2K = static_cast<uint16_t>(rpm * 4.0);
  n2kdata.data.u8[0] = instance;
  n2kdata.data.u8[1] = rpmN2K & 0xFF;
  n2kdata.data.u8[2] = (rpmN2K >> 8) & 0xFF;

  n2kdata.data.u8[5] = static_cast<uint8_t>(trim);

  return n2kdata;
}

#endif
