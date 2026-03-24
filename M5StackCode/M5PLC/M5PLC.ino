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

twai_message_t apparentWindFrame(double direction, double speed){
  twai_message_t n2kdata;
  n2kdata.extd = true;
  n2kdata.data_length_code = 8;


  uint8_t priority = 2;
  uint8_t deviceId = 0x23;
  uint32_t pgn = 130306;//wind frame id;

  uint32_t id = calcFrameId(priority, pgn, deviceId); //get frame_id

  n2kdata.identifier = id;

  uint16_t direction_rads = (direction*(M_PI/180))*10000;//to 0.0001 rads

  n2kdata.data[3] = direction_rads & 0xFF; //direction LSB
  n2kdata.data[4] = (direction_rads >> 8) & 0xFF; //direction MSB

  uint16_t mps = (speed*0.514444444)*100;//to 0.01m/s

  n2kdata.data[1] = mps & 0xFF;//speed LSB
  n2kdata.data[2] = (mps >> 8) & 0xFF;//speed MSB

  n2kdata.data[5] = 2;//for apparent

  return n2kdata;
}

twai_message_t batteryStatusFrame(uint8_t instance, double voltage, double current = 0){ //voltage, current in amps
  twai_message_t n2kdata;
  n2kdata.extd = true;
  n2kdata.data_length_code = 8;


  uint8_t priority = 6;
  uint8_t deviceId = 0x23;
  uint32_t pgn = 127508;//battery

  uint32_t id = calcFrameId(priority, pgn, deviceId); //get frame_id

  n2kdata.identifier = id;

  uint16_t voltN2K = voltage*100;
  uint16_t ampN2K = current*10;

  n2kdata.data[1] = voltN2K & 0xFF;//voltage LSB
  n2kdata.data[2] = (voltN2K >> 8) & 0xFF;//voltage MSB

  n2kdata.data[3] = ampN2K & 0xFF;//current LSB
  n2kdata.data[4] = (ampN2K >> 8) & 0xFF;//current MSB

  n2kdata.data[0] = instance;

  return n2kdata;
}
esp_err_t postCanMessage(twai_message_t& message, TickType_t timeout_ticks = pdMS_TO_TICKS(1000)) {
    return twai_transmit(&message, timeout_ticks);
}
float getAngleDegrees(AS5600 encoder) {
    float angle = encoder.rawAngle() * 360.0f / 4096.0f;
    angle += 0.0f;

    if (angle >= 360.0f) angle -= 360.0f;

    return angle;
}// important offset is in here

// PORT.A on StamPLC: SDA=G2, SCL=G1
TwoWire i2cA = TwoWire(0);

// PORT.C on StamPLC: SDA=G5, SCL=G4
TwoWire i2cC = TwoWire(1);

// Both M5 INA226 units use 0x41
INA226 batt1(0x41, &i2cA);
INA226 batt2(0x41, &i2cC);
AS5600 encoder(&i2cA);



void setup() {
  // put your setup code here, to run once:


  Serial.begin(9600);
  /* Enable CAN */
    auto config        = M5StamPLC.config();
    config.enableCan   = true;
    config.canBaudRate = 250000;//1000000;
    M5StamPLC.config(config);
    
    i2cA.setPins(2, 1);   // SDA, SCL
    i2cC.setPins(5, 4);   // SDA, SCL

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

    /* Init M5StamPLC */
    M5StamPLC.begin();
}
int timebatt0 = 0;
int timebatt1 = 0;
int timeWind = 0;
void loop() {
  if(millis() - timebatt0 > 1500){
    double voltage0 = batt1.getBusVoltage();
    twai_message_t battery0msg = batteryStatusFrame(0, voltage0);
    esp_err_t err = postCanMessage(battery0msg);
    if(err!= ESP_OK){
      Serial.println("Failed CAN Send");
    }
    timebatt0 = millis();
  }
  
  if(millis() - timebatt1 > 1500){
    double voltage1 = batt2.getBusVoltage();
    twai_message_t battery1msg = batteryStatusFrame(1, voltage1);
    esp_err_t err = postCanMessage(battery1msg);
    if(err!= ESP_OK){
      Serial.println("Failed CAN Send");
    }
    timebatt1 = millis();
  }
  if(millis() - timeWind > 100){
    double wind = getAngleDegrees(encoder);
    twai_message_t windMsg = apparentWindFrame(wind, 10);
    esp_err_t err = postCanMessage(windMsg);
    if(err!= ESP_OK){
      Serial.println("Failed CAN Send");
    }
    timeWind = millis();
  }



  // put your main code here, to run repeatedly:

}
