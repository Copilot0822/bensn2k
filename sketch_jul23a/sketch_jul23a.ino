#include <due_can.h>

#define CAN_BUS_SPEED 250000
CAN_FRAME frame;

void setup() {
  Serial.begin(115200);
  frame.id = 0x18FEF100;

  frame.extended = true;
  frame.length = 8;


  frame.data.byte[0] = 0000;
  frame.data.byte[1] = 0xBB;
  frame.data.byte[2] = 0xCC;
  frame.data.byte[3] = 0xDD;
  frame.data.byte[4] = 0x11;
  frame.data.byte[5] = 0x22;
  frame.data.byte[6] = 0x33;
  frame.data.byte[7] = 0x44;
  

  Can0.begin(CAN_BUS_SPEED);
  // put your setup code here, to run once:

}

void loop() {

  Can0.sendFrame(frame);
  // put your main code here, to run repeatedly:

}
