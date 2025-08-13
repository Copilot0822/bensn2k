#include <due_can.h>

#define CAN_BUS_SPEED 250000
CAN_FRAME frame;
CAN_FRAME frame1;
CAN_FRAME frame2;
CAN_FRAME frame3;

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

  Serial.begin(460800);
  Serial.println("2");
  //frame.id = 0x18FEF100;

  
  //frame.id = 0b11001111100100001010000100011;
  //frame.id = 0b110 01111100100000000000100011;
  frame.id = 0b01001111011110000000000000000;
  frame.extended = true;
  frame.length = 8;
  frame.data.byte[0] = 0x00;
  frame.data.byte[1] = 0x16;

  frame.data.byte[2] = 0b11100111;
  frame.data.byte[3] = 0b01100100;
  frame.data.byte[4] = 0b10000001;//propid part 1
  frame.data.byte[5] = 0b11110000;//propid part 2
  frame.data.byte[6] = 0b10000110;//command
  frame.data.byte[7] = 0b00000011;//device

  frame1.id = 0b01001111011110000000000000000;
  frame1.extended = true;
  frame1.length = 8;
  frame1.data.byte[0] = 0b00000001;

  frame1.data.byte[1] = 0b00000001;//keystroke
  frame1.data.byte[2] = 0b11111110;//inverted
  frame1.data.byte[3] = 0b00000000;//1
  frame1.data.byte[4] = 0b00000000;//2
  frame1.data.byte[5] = 0b00000000;//3
  frame1.data.byte[6] = 0b00000000;//4
  frame1.data.byte[7] = 0b00000000;//5

  frame2.id = 0b01001111011110000000000000000;
  frame2.extended = true;
  frame2.length = 8;

  frame2.data.byte[0] = 0b00000010;

  frame2.data.byte[1] = 0b00000000;//6
  frame2.data.byte[2] = 0b00000000;//7
  frame2.data.byte[3] = 0b00000000;//8
  frame2.data.byte[4] = 0b00000000;//9
  frame2.data.byte[5] = 0b00000000;//10
  frame2.data.byte[6] = 0b00000000;//11
  frame2.data.byte[7] = 0b00000000;//12

  frame3.id = 0b01001111011110000000000000000;
  frame3.extended = true;
  frame3.length = 8;

  frame3.data.byte[0] = 0b00000011;

  frame3.data.byte[1] = 0b00000000;//13
  frame3.data.byte[2] = 0b00000000;//14
  frame3.data.byte[3] = 0x00;
  frame3.data.byte[4] = 0x00;
  frame3.data.byte[5] = 0x00;
  frame3.data.byte[6] = 0x00;
  frame3.data.byte[7] = 0x00;






  


  

  
  

  Can0.begin(CAN_BUS_SPEED);

  delay(1000);

  Can0.sendFrame(frame);
  Serial.println("out0");
  Can0.sendFrame(frame1);
  
  Serial.println("out1");

  Can0.sendFrame(frame2);
  
  Serial.println("out2");
  Can0.sendFrame(frame3);
  
  Serial.println("out3");

  // put your setup code here, to run once:

}
int i = 0;

void loop() {
  i++;
  //Serial.println(i);
  

  //Can0.sendFrame(frame);
  

  //Serial.println(frame.data.byte[3]);


  delay(1000);


  frame3.data.byte[0] = incrementTop3Bits(frame3.data.byte[0]);
  Serial.println(frame3.data.byte[0]);
  // put your main code here, to run repeatedly:

}




uint8_t incrementTop3Bits(uint8_t byte) {
    // Extract top 3 bits (bits 7,6,5)
    uint8_t top3 = (byte & 0b11100000) >> 5;

    // Increment them with wrapping (modulo 8)
    top3 = (top3 + 1) % 8;

    // Clear the original top 3 bits in byte
    byte &= 0b00011111;

    // Set the new top 3 bits
    byte |= (top3 << 5);

    return byte;
}