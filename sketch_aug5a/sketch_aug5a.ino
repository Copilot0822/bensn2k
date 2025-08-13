#include <due_can.h>

#define CAN_BUS_SPEED 250000

CAN_FRAME canned;

struct GamepadState {
  bool buttonA;       // Bit 0
  bool buttonB;       // Bit 1
  bool buttonX;       // Bit 2
  bool buttonY;       // Bit 3
  bool leftBumper;    // Bit 4
  bool rightBumper;   // Bit 5
  bool view;          // Bit 6
  bool menu;          // Bit 7
  bool leftst;        // Bit 8
  bool rightst;       // Bit 9
  bool dpadUp;        // Bit 10
  bool dpadDown;      // Bit 11
  bool dpadLeft;      // Bit 12
  bool dpadRight;     // Bit 13
  int leftX;          // Left stick X, 0-1023
  int leftY;          // Left stick Y, 0-1023
  int rightX;         // Right stick X, 0-1023
  int rightY;         // Right stick Y, 0-1023
  int leftTrigger;    // Left trigger, 0-1023
  int rightTrigger;   // Right trigger, 0-1023
};

uint32_t calcFrameId(uint8_t priority, uint32_t pgn, uint8_t device) {
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

void calcFrame(GamepadState &state) {
  bool b00 = state.buttonA;
  bool b10 = state.buttonB;
  bool b20 = state.buttonX;
  bool b30 = state.buttonY;
  bool b40 = state.dpadUp;
  bool b50 = state.dpadLeft;
  bool b60 = state.dpadDown;
  bool b70 = state.dpadRight;

  // Pack into a byte
  uint8_t byte0 = (b70 << 7) |
                  (b60 << 6) |
                  (b50 << 5) |
                  (b40 << 4) |
                  (b30 << 3) |
                  (b20 << 2) |
                  (b10 << 1) |
                  (b00 << 0);

  canned.data.byte[0] = byte0;

  bool b01 = state.leftBumper;
  bool b11 = state.rightBumper;
  bool b21 = state.menu;
  bool b31 = state.view;
  bool b41 = state.leftst;
  bool b51 = state.rightst;
  bool b61 = true;
  bool b71 = true;

  // Pack into a byte
  uint8_t byte1 = (b71 << 7) |
                  (b61 << 6) |
                  (b51 << 5) |
                  (b41 << 4) |
                  (b31 << 3) |
                  (b21 << 2) |
                  (b11 << 1) |
                  (b01 << 0);

  canned.data.byte[1] = byte1;

  double x = 0.1245;
  double y = 1023;
  canned.data.byte[2] = (state.leftX + 0) * 2 * x;
  canned.data.byte[3] = (state.leftY + 0) * 2 * x;
  canned.data.byte[4] = (state.rightX + y) * x;
  canned.data.byte[5] = (state.rightY + y) * x;
  canned.data.byte[6] = (state.leftTrigger) * 2 * x;
  canned.data.byte[7] = (state.rightTrigger) * 2 * x;
}

GamepadState gamepad = {false, false, false, false, false, false, false, false, false, false, false, false, false, false, 512, 512, 512, 512, 0, 0};

void setup() {
  Serial.begin(115200);
  SerialUSB.begin(115200);
  Can0.begin(CAN_BUS_SPEED);

  canned.id = calcFrameId(6, 126767, 0x23);
  canned.length = 8;

  while (!SerialUSB) {
    ; // Wait for serial port to connect
  }
}

bool updateGamepadState(String input, GamepadState &state) {
  if (input.length() == 0) {
    return false; // No valid input
  }

  char buf[32];
  input.toCharArray(buf, sizeof(buf));

  // Parse buttons, leftX, leftY, rightX, rightY, leftTrigger, rightTrigger
  char* token = strtok(buf, ",");
  if (token == NULL) {
    return false; // Invalid format
  }
  int buttons = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) {
    return false; // Missing leftX
  }
  int leftX = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) {
    return false; // Missing leftY
  }
  int leftY = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) {
    return false; // Missing rightX
  }
  int leftTrigger = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) {
    return false; // Missing rightY
  }
  int rightTrigger = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) {
    return false; // Missing leftTrigger
  }
  int rightX = atoi(token);

  token = strtok(NULL, ",");
  if (token == NULL) {
    return false; // Missing rightTrigger
  }
  int rightY = atoi(token);

  // Update state
  state.buttonA = (buttons & (1 << 0)) != 0;
  state.buttonB = (buttons & (1 << 1)) != 0;
  state.buttonX = (buttons & (1 << 2)) != 0;
  state.buttonY = (buttons & (1 << 3)) != 0;
  state.leftBumper = (buttons & (1 << 4)) != 0;
  state.rightBumper = (buttons & (1 << 5)) != 0;
  state.view = (buttons & (1 << 6)) != 0;
  state.menu = (buttons & (1 << 7)) != 0;
  state.leftst = (buttons & (1 << 8)) != 0;
  state.rightst = (buttons & (1 << 9)) != 0;
  state.dpadUp = (buttons & (1 << 10)) != 0;
  state.dpadDown = (buttons & (1 << 11)) != 0;
  state.dpadLeft = (buttons & (1 << 12)) != 0;
  state.dpadRight = (buttons & (1 << 13)) != 0;
  state.leftX = leftX;
  state.leftY = leftY;
  state.rightX = rightX;
  state.rightY = rightY;
  state.leftTrigger = leftTrigger;
  state.rightTrigger = rightTrigger;

  return true; // Successful update
}
int count = 0;
void loop() {
  if (SerialUSB.available()) {
    String input = SerialUSB.readStringUntil('\n');
    if (updateGamepadState(input, gamepad)) {
      count = 0;
      // Control LEDs
      digitalWrite(13, gamepad.buttonA ? HIGH : LOW); // LED on for A pressed
    }else{
    canned.data.value = 0;
    count ++;
    }
  }else{
    canned.data.value = 0;
    count++;
  }

  if(count < 3){
    calcFrame(gamepad);
  }

  
  
  Can0.sendFrame(canned);

  printCan(canned);

  delay(50);
}

void printCan(CAN_FRAME &frame) {
  for (int i = 0; i < frame.length; i++) {
    Serial.print(" Byte");
    Serial.print(i);
    Serial.print(": ");

    if (i < 2) {
      // Print binary with leading 0s
      for (int bit = 7; bit >= 0; bit--) {
        Serial.print(bitRead(frame.data.byte[i], bit));
      }
    } else {
      // Print decimal, padded to 3 digits
      if (frame.data.byte[i] < 10) {
        Serial.print("00");
      } else if (frame.data.byte[i] < 100) {
        Serial.print("0");
      }
      Serial.print(frame.data.byte[i]);
    }
  }
  Serial.println();
}