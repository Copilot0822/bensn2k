// Arduino Due - Displays all traffic found on either canbus port
// By Thibaut Viard/Wilfredo Molina/Collin Kidder 2013-2014

// Required libraries
#include "variant.h"
#include <due_can.h>

#include <vector>
#include <array>

std::vector<std::array<uint32_t, 2>> valueCounts;

//Leave defined if you use native port, comment if using programming port
//This sketch could provide a lot of traffic so it might be best to use the
//native port
//#define Serial SerialUSB

void setup()
{

  addOrIncrement(0);
  addOrIncrement(0);

  //addOrIncrement(127306);

  


  

  Serial.begin(9600);
  
  // Initialize CAN0 and CAN1, Set the proper baud rates here
  Can0.begin(CAN_BPS_250K);
  Can1.begin(CAN_BPS_250K);
  
  //By default there are 7 mailboxes for each device that are RX boxes
  //This sets each mailbox to have an open filter that will accept extended
  //or standard frames
  int filter;
  //extended
  for (filter = 0; filter < 3; filter++) {
	Can0.setRXFilter(filter, 0, 0, true);
	Can1.setRXFilter(filter, 0, 0, true);
  }  
  //standard
  for (int filter = 3; filter < 7; filter++) {
	Can0.setRXFilter(filter, 0, 0, false);
	Can1.setRXFilter(filter, 0, 0, false);
  }  
  
}

/*void printFrame(CAN_FRAME &frame) {
   Serial.print("ID: ");
   Serial.print((frame.id >> 8) & 0x3FFFF);
   //Serial.print(" Len: ");
   //Serial.print(frame.length);
   //Serial.print(" Data: 0x");
   for (int count = 0; count < frame.length; count++) {
       Serial.print(frame.data.bytes[count], HEX);
       Serial.print(" ");
   }
   Serial.print("\r\n");
}*/

//uint8_t PGNArr[] = new uint8_t[3];


void sort(CAN_FRAME &frame) {
  uint32_t pgn = (frame.id >> 8) & 0x3FFFF;
  
  addOrIncrement(pgn);
}



void addOrIncrement(uint32_t val18) {
    // Make sure it's 18 bits
    val18 &= 0x3FFFF;

    for (auto& pair : valueCounts) {
        if (pair[0] == val18) {
            pair[1]++;  // increment count
            return;
        }
    }

    // Not found, add new entry
    valueCounts.push_back({val18, 1});
}




void printValueCounts() {
    for (const auto& pair : valueCounts) {
        Serial.print(pair[0]);  // 18-bit value in decimal
        Serial.print(": ");
        Serial.print(pair[1]);  // count
        Serial.println("x");
    }
}



void loop(){
  CAN_FRAME incoming;

  if (Can0.available() > 0) {
	Can0.read(incoming); 
  sort(incoming);
  //addOrIncrement()
	//printFrame(incoming);
  //sort(incoming);

  }
  if (Can1.available() > 0) {
	Can1.read(incoming); 
	//printFrame(incoming);
  //sort(incoming);
  }
  //

  

  Serial.print("\r\n\r\n\r\n\r\n\r\n\r\n");

  printValueCounts();
  delay(3);

}


