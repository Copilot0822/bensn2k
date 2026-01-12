#include <due_can.h>

#include "arduino/N2KFrames.h"

#define CAN_BUS_SPEED 250000

void setup() {
  Serial.begin(9600);
  Can0.begin(CAN_BUS_SPEED);
}

void loop() {
  CAN_FRAME wind = apparentWindFrame(90.0, 8.0);
  CAN_FRAME battery1 = batteryStatusFrame(0, 12.3, 1.2);
  CAN_FRAME battery2 = batteryStatusFrame(1, 12.4, 1.3);
  CAN_FRAME engine = engineParametersRapid(0, 500.0, 50.0);

  Can0.sendFrame(wind);
  Can0.sendFrame(battery1);
  Can0.sendFrame(battery2);
  Can0.sendFrame(engine);

  delay(1000);
}
