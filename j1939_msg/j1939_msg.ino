#include <due_can.h>

//#include <due_can.h>
#include <cmath>

#define CAN_BUS_SPEED 250000
CAN_FRAME frame;
CAN_FRAME frame1;


//typedef unsigned _BitInt(3) uint3_t;



class NMEA_2000_Data {
  public:
    uint8_t priority = 6;
    uint8_t deviceId = 0x23; //your device ID
    uint32_t pgn;
    uint32_t frameId;
    CAN_FRAME NMEA2000_Frame;
    



};

uint32_t calcFrameId(uint8_t priority, uint32_t pgn, uint8_t device){
  uint8_t x = priority;
  uint32_t y = pgn;
  uint8_t z = device;

 

  // Mask to exact sizes
  x &= 0x07;        // 3 bits
  y &= 0x3FFFF;     // 18 bits
  z &= 0xFF;        // 8 bits

  // Shift into place: x in bits 28–26, y in 25–8, z in 7–0
  uint32_t packed = (x << 26) | (y << 8) | z;

  return packed;
}



CAN_FRAME apparentWindFrame(double direction, double speed){
  CAN_FRAME n2kdata;
  n2kdata.extended = true;
  n2kdata.length = 8;


  uint8_t priority = 2;
  uint8_t deviceId = 0x23;
  uint32_t pgn = 130306;//wind frame id;

  uint32_t id = calcFrameId(priority, pgn, deviceId); //get frame_id

  n2kdata.id = id;

  uint16_t direction_rads = (direction*(M_PI/180))*10000;//to 0.0001 rads

  n2kdata.data.byte[3] = direction_rads & 0xFF; //direction LSB
  n2kdata.data.byte[4] = (direction_rads >> 8) & 0xFF; //direction MSB

  uint16_t mps = (speed*0.514444444)*100;//to 0.01m/s

  n2kdata.data.byte[1] = mps & 0xFF;//speed LSB
  n2kdata.data.byte[2] = (mps >> 8) & 0xFF;//speed MSB

  n2kdata.data.byte[5] = 2;//for apparent

  return n2kdata;
}
CAN_FRAME batteryStatusFrame(uint8_t instance, double voltage, double current){ //voltage, current in amps
  CAN_FRAME n2kdata;
  n2kdata.extended = true;
  n2kdata.length = 8;


  uint8_t priority = 6;
  uint8_t deviceId = 0x23;
  uint32_t pgn = 127508;//battery

  uint32_t id = calcFrameId(priority, pgn, deviceId); //get frame_id

  n2kdata.id = id;

  uint16_t voltN2K = voltage*100;
  uint16_t ampN2K = current*10;

  n2kdata.data.byte[1] = voltN2K & 0xFF;//voltage LSB
  n2kdata.data.byte[2] = (voltN2K >> 8) & 0xFF;//voltage MSB

  n2kdata.data.byte[3] = ampN2K & 0xFF;//current LSB
  n2kdata.data.byte[4] = (ampN2K >> 8) & 0xFF;//current MSB

  n2kdata.data.byte[0] = instance;

  return n2kdata;
}
CAN_FRAME engineParametersRapid(uint8_t instance, double RPM, double trim){
  CAN_FRAME n2kdata;
  n2kdata.extended = true;
  n2kdata.length = 8;


  uint8_t priority = 2;
  uint8_t deviceId = 0x23;
  uint32_t pgn = 127488;//engine rapid update

  uint32_t id = calcFrameId(priority, pgn, deviceId); //get frame_id

  n2kdata.id = id;

  uint8_t rpmN2K = RPM*4;

  n2kdata.data.byte[0] = instance;

  n2kdata.data.byte[1] = rpmN2K & 0xFF;//RPM LSB
  n2kdata.data.byte[2] = (rpmN2K >> 8) & 0xFF;//RPM MSB

  n2kdata.data.byte[5] = trim;

  return n2kdata;

}


//uint32_t theId;

//theId = makeJ1939Id(127508, 6, 0x23);

void setup() {
  Serial.begin(9600);
  Can0.begin(CAN_BUS_SPEED);
  pinMode(LED_BUILTIN, OUTPUT);
  // put your setup code here, to run once:

}
//int i = 0;

void loop() {
  
  CAN_FRAME Wind = apparentWindFrame(90, 8);
  CAN_FRAME Battery1 = batteryStatusFrame(0, 12.3, 1.2);
  CAN_FRAME Battery2 = batteryStatusFrame(1, 12.4, 1.3);
  CAN_FRAME Engine = engineParametersRapid(0, 500, 50);

  
  //digitalWrite(LED_BUILTIN, HIGH);
  Serial.println("start");
  Can0.sendFrame(Wind);
  Serial.println("send");
  Can0.sendFrame(Battery1);
  Serial.println("send");
  Can0.sendFrame(Battery2);
  Serial.println("send");
  Can0.sendFrame(Engine);
  Serial.println("send");
  //digitalWrite(LED_BUILTIN, LOW);





  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, LOW);
  // put your main code here, to run repeatedly:

}
