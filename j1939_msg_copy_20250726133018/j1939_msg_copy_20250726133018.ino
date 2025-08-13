#include <due_can.h>

#define CAN_BUS_SPEED 250000
CAN_FRAME frame;
CAN_FRAME frame1;

uint32_t calculateJ1939CanId(uint32_t pgn, uint8_t priority, uint8_t sourceAddress) {
  uint32_t canId = 0;
  if (priority > 7) {
    priority = 7;
  }
  canId |= ( (uint32_t)(priority & 0x07) << 26 );
  canId |= ( (pgn & 0x3FFFFUL) << 8 ); // This line correctly places your 18-bit PGN value
  canId |= ( (uint32_t)(sourceAddress & 0xFF) );
  return canId;
}


uint32_t theId;

//theId = makeJ1939Id(127508, 6, 0x23);

void setup() {

  //theId = makeJ1939Id(127508, 6, 0x23);






  Serial.begin(115200);
  //frame.id = 0x18FEF100;

  uint8_t priority     = 6;        // 0–7
  uint8_t dataPage     = 0;        // typically 0 or 1
  uint8_t pduFormat    = 0xEF;     // PF ≥ 0xF0 for broadcast
  uint8_t pduSpecific  = 0x00;     // PS = group extension (broadcast)
  uint8_t sourceAddr   = 0x23;     // your node’s address

  uint32_t frameId = ((uint32_t)priority << 26)
                  | ((uint32_t)dataPage  << 24)
                  | ((uint32_t)pduFormat << 16)
                  | ((uint32_t)pduSpecific << 8)
                  | sourceAddr;

  //frame.id = 0b11001111100100001010000100011;
  frame.id = 0b11001111100100000000000100011;
  frame1.id = 0b11001111100100001010000100011;

  frame.extended = true;
  frame1.extended = true;
  frame.length = 8;
  frame1.length = 8; 


  frame.data.byte[0] = 0b0000;
  frame.data.byte[1] = 0x05;
  frame.data.byte[2] = 0x00;
  frame.data.byte[3] = 0x00;
  frame.data.byte[4] = 0x00;
  frame.data.byte[5] = 0x00;
  frame.data.byte[6] = 0x00;
  frame.data.byte[7] = 0x00;

  frame1.data.byte[0] = 0b0000;
  frame1.data.byte[1] = 0xce;
  frame1.data.byte[2] = 0x04;
  frame1.data.byte[3] = 0x7e;
  frame1.data.byte[4] = 0x00;
  frame1.data.byte[5] = 0x00;
  frame1.data.byte[6] = 0x00;
  frame1.data.byte[7] = 0x00;
  

  Can0.begin(CAN_BUS_SPEED);
  // put your setup code here, to run once:

}
int i = 0;

void loop() {
  i++;
  //frame.data.byte[1] ++;
  //frame.data.


  //theId = calculateJ1939CanId(127509, 6, 0x23);

  frame1.data.byte[4] ++;
  frame.data.byte[2] += 1;


  Can0.sendFrame(frame);
  
  Can0.sendFrame(frame1);
  Serial.println(i);


  delay(1000);
  // put your main code here, to run repeatedly:

}
