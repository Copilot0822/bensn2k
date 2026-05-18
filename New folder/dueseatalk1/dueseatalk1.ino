#include <Arduino.h>

// ============================================================
// Arduino Due SeaTalk 1 Controller
//
// USB serial from Python GUI:
//   BTN:STBY
//   BTN:AUTO
//   BTN:TRACK
//   BTN:WIND
//   BTN:M1
//   BTN:M10
//   BTN:P1
//   BTN:P10
//   BTN:TACK_PORT
//   BTN:TACK_STBD
//
// SeaTalk hardware:
//   Due TX1 pin 18 -> SN74LS07 -> SeaTalk DATA
//   SeaTalk DATA -> PC817 non-inverting RX -> Due RX1 pin 19
//
// Extra idle sense wire:
//   Due RX node / pin 19 / PC817 pin 3 -> Due digital pin 22
//
// Correct RX polarity:
//   SeaTalk bus HIGH -> Due RX pin 19 HIGH and pin 22 HIGH
//   SeaTalk bus LOW  -> Due RX pin 19 LOW and pin 22 LOW
// ============================================================


// ===================== USER SETTINGS =====================

// 0 = do not actually transmit. It only prints WOULD_TX.
// 1 = transmit SeaTalk datagrams.
#define ENABLE_SEATALK_TX 1

// 1 = print idle debug every 500 ms.
#define DEBUG_IDLE_PIN 1

// Should be 1 with the non-inverting opto circuit.
#define RX_IDLE_IS_HIGH 1

#define SEATALK_BAUD 4800

// Dedicated digital input used only for idle checking.
const int SEATALK_IDLE_SENSE_PIN = 22;

// Thomas Knauf 0x86 keystroke source/type byte.
// If you sniff your actual ST6002 and it differs, change this.
const uint8_t ST_KEY_SOURCE = 0x11;

// SeaTalk idle wait before transmit.
// 10 bit-times at 4800 baud ≈ 2.08 ms. Use 2.5 ms.
const uint32_t SEATALK_IDLE_US = 2500;
const uint32_t SEATALK_TX_TIMEOUT_MS = 300;


// ===================== FORWARD DECLARATIONS =====================

void setupUSART0_9bit_TXRX();

void serviceUSBSerial();
void serviceSeaTalkRX();

bool read9NonBlocking(uint16_t &value);
bool read9EchoBlocking(uint16_t &value, uint32_t timeoutMs);
void send9Raw(uint16_t value);
void flushSeaTalkRX();

bool rawSeaTalkIdleSenseHigh();
bool seaTalkBusIdle();
void debugIdlePin();
bool waitForSeaTalkIdle(uint32_t idleUs, uint32_t timeoutMs);

bool send9WithEchoCheck(uint16_t value);
bool sendSeaTalkDatagram(const uint8_t *bytes, uint8_t len);
void sendKeyDatagram(uint8_t keyCode);

void handleButtonCommand(const char *cmd);
void trimLine(char *s);
void handleUSBLine(char *line);

void handleSeaTalkWord(uint16_t word);
void processSeaTalkDatagram(const uint8_t *d, uint8_t len);
void printRawSeaTalkDatagram(const uint8_t *d, uint8_t len);

void parseAutopilot84(const uint8_t *d, uint8_t len);
void parseWindAngle10(const uint8_t *d, uint8_t len);
void parseKey86(const uint8_t *d, uint8_t len);

const char *decodePilotMode(uint8_t Z);
float headingFineCorrection(uint8_t U);
float normalize360(float deg);

void printStatusLine();
void printHex2(uint8_t b);


// ===================== USB SERIAL LINE PARSER =====================

char usbLine[96];
uint8_t usbLineLen = 0;


// ===================== SEATALK DATAGRAM PARSER =====================

const uint8_t ST_MAX_DGRAM = 18;

uint8_t stBuf[ST_MAX_DGRAM];
uint8_t stLen = 0;
uint8_t stExpectedLen = 0;
uint32_t stLastByteUs = 0;


// ===================== PILOT STATUS =====================

float lastRudder = 0.0f;
float lastSetpoint = 0.0f;
char lastMode[16] = "--";

uint32_t lastStatusPrintMs = 0;
uint32_t lastIdleDebugMs = 0;


// ===================== USART0 9-BIT SETUP =====================

void setupUSART0_9bit_TXRX() {
  pmc_enable_periph_clk(ID_USART0);

  // Arduino Due:
  // RX1 pin 19 = PA10 = RXD0 = USART0 RX
  // TX1 pin 18 = PA11 = TXD0 = USART0 TX
  //
  // Do not call pinMode(18/19) after this.
  PIOA->PIO_PDR = PIO_PA10A_RXD0 | PIO_PA11A_TXD0;
  PIOA->PIO_ABSR &= ~(PIO_PA10A_RXD0 | PIO_PA11A_TXD0);

  USART0->US_CR = US_CR_RSTRX | US_CR_RSTTX | US_CR_RXDIS | US_CR_TXDIS;

  USART0->US_MR =
      US_MR_USART_MODE_NORMAL |
      US_MR_USCLKS_MCK |
      US_MR_CHRL_8_BIT |
      US_MR_PAR_NO |
      US_MR_NBSTOP_1_BIT |
      US_MR_CHMODE_NORMAL |
      US_MR_MODE9;

  USART0->US_BRGR = (VARIANT_MCK + (8 * SEATALK_BAUD)) / (16 * SEATALK_BAUD);

  USART0->US_CR = US_CR_RSTSTA;
  USART0->US_CR = US_CR_RXEN | US_CR_TXEN;
}


bool read9NonBlocking(uint16_t &value) {
  if (!(USART0->US_CSR & US_CSR_RXRDY)) {
    return false;
  }

  uint32_t status = USART0->US_CSR;

  if (status & (US_CSR_OVRE | US_CSR_FRAME | US_CSR_PARE)) {
    USART0->US_CR = US_CR_RSTSTA;
    Serial.println("ST_RX_ERROR:USART_STATUS");
    return false;
  }

  value = USART0->US_RHR & 0x01FF;
  return true;
}


// Used only for TX echo/collision checking.
// Do not call serviceSeaTalkRX() here, or it may consume the echo byte.
bool read9EchoBlocking(uint16_t &value, uint32_t timeoutMs) {
  uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    if (read9NonBlocking(value)) {
      return true;
    }
  }

  return false;
}


void send9Raw(uint16_t value) {
  value &= 0x01FF;

  while (!(USART0->US_CSR & US_CSR_TXRDY)) {
    serviceSeaTalkRX();
  }

  USART0->US_THR = value;
}


void flushSeaTalkRX() {
  uint16_t ignored;

  while (read9NonBlocking(ignored)) {
  }
}


// ===================== BUS IDLE CHECK =====================

bool rawSeaTalkIdleSenseHigh() {
  return digitalRead(SEATALK_IDLE_SENSE_PIN) == HIGH;
}


bool seaTalkBusIdle() {
#if RX_IDLE_IS_HIGH
  return rawSeaTalkIdleSenseHigh();
#else
  return !rawSeaTalkIdleSenseHigh();
#endif
}


void debugIdlePin() {
#if DEBUG_IDLE_PIN
  bool pinHigh = rawSeaTalkIdleSenseHigh();
  bool idle = seaTalkBusIdle();

  Serial.print("IDLE_DEBUG:SENSE_PIN_22=");
  Serial.print(pinHigh ? "HIGH" : "LOW");
  Serial.print(";BUS_IDLE=");
  Serial.println(idle ? "YES" : "NO");
#endif
}


bool waitForSeaTalkIdle(uint32_t idleUs, uint32_t timeoutMs) {
  uint32_t startMs = millis();
  uint32_t idleStartUs = 0;
  bool wasIdle = false;

  while (millis() - startMs < timeoutMs) {
    serviceSeaTalkRX();

    bool idleNow = seaTalkBusIdle();

    if (idleNow) {
      if (!wasIdle) {
        wasIdle = true;
        idleStartUs = micros();
      }

      if ((uint32_t)(micros() - idleStartUs) >= idleUs) {
        return true;
      }
    } else {
      wasIdle = false;
    }
  }

  Serial.print("ERR:BUS_NOT_IDLE;SENSE_PIN_22=");
  Serial.print(rawSeaTalkIdleSenseHigh() ? "HIGH" : "LOW");
  Serial.print(";BUS_IDLE=");
  Serial.println(seaTalkBusIdle() ? "YES" : "NO");

  return false;
}


// ===================== SEATALK TX =====================

bool send9WithEchoCheck(uint16_t value) {
  send9Raw(value);

  uint16_t echo = 0;

  if (!read9EchoBlocking(echo, 30)) {
    Serial.print("ERR:NO_ECHO TX=0x");
    Serial.println(value, HEX);
    return false;
  }

  if ((echo & 0x01FF) != (value & 0x01FF)) {
    Serial.print("ERR:ECHO_MISMATCH TX=0x");
    Serial.print(value, HEX);
    Serial.print(" RX=0x");
    Serial.println(echo, HEX);
    return false;
  }

  return true;
}


bool sendSeaTalkDatagram(const uint8_t *bytes, uint8_t len) {
  if (len < 3 || len > ST_MAX_DGRAM) {
    Serial.println("ERR:BAD_TX_LEN");
    return false;
  }

  Serial.print("ST_TX:");
  for (uint8_t i = 0; i < len; i++) {
    Serial.print(" ");
    printHex2(bytes[i]);
  }
  Serial.println();

#if ENABLE_SEATALK_TX == 0
  Serial.println("WOULD_TX:ENABLE_SEATALK_TX_IS_0");
  return false;
#else
  if (!waitForSeaTalkIdle(SEATALK_IDLE_US, SEATALK_TX_TIMEOUT_MS)) {
    return false;
  }

  flushSeaTalkRX();

  // First byte has 9th/command bit set.
  if (!send9WithEchoCheck(0x100 | bytes[0])) {
    return false;
  }

  // Remaining bytes have 9th/command bit clear.
  for (uint8_t i = 1; i < len; i++) {
    if (!send9WithEchoCheck(bytes[i])) {
      return false;
    }
  }

  Serial.println("TX_OK");
  return true;
#endif
}


void sendKeyDatagram(uint8_t keyCode) {
  // Thomas Knauf 0x86 keystroke datagram:
  //   86 X1 YY yy
  //
  // X1 = source/type byte.
  // YY = key code.
  // yy = bitwise complement of YY.

  uint8_t bytes[4];

  bytes[0] = 0x86;
  bytes[1] = ST_KEY_SOURCE;
  bytes[2] = keyCode;
  bytes[3] = 0xFF - keyCode;

  sendSeaTalkDatagram(bytes, 4);
}


// ===================== GUI BUTTON COMMANDS =====================

void handleButtonCommand(const char *cmd) {
  Serial.print("BUTTON:");
  Serial.println(cmd);

  if (strcmp(cmd, "AUTO") == 0) {
    sendKeyDatagram(0x01);
  } else if (strcmp(cmd, "STBY") == 0) {
    sendKeyDatagram(0x02);
  } else if (strcmp(cmd, "TRACK") == 0) {
    sendKeyDatagram(0x03);
  } else if (strcmp(cmd, "M1") == 0) {
    sendKeyDatagram(0x05);
  } else if (strcmp(cmd, "M10") == 0) {
    sendKeyDatagram(0x06);
  } else if (strcmp(cmd, "P1") == 0) {
    sendKeyDatagram(0x07);
  } else if (strcmp(cmd, "P10") == 0) {
    sendKeyDatagram(0x08);
  } else if (strcmp(cmd, "TACK_PORT") == 0) {
    sendKeyDatagram(0x21);
  } else if (strcmp(cmd, "TACK_STBD") == 0) {
    sendKeyDatagram(0x22);
  } else if (strcmp(cmd, "WIND") == 0) {
    // Verify by sniffing your real control head before live use.
    sendKeyDatagram(0x23);
  } else {
    Serial.print("ERR:UNKNOWN_BTN:");
    Serial.println(cmd);
  }
}


// ===================== USB SERIAL HANDLING =====================

void trimLine(char *s) {
  char *start = s;

  while (*start == ' ' || *start == '\t') {
    start++;
  }

  if (start != s) {
    memmove(s, start, strlen(start) + 1);
  }

  int len = strlen(s);

  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
    s[len - 1] = '\0';
    len--;
  }
}


void handleUSBLine(char *line) {
  trimLine(line);

  if (line[0] == '\0') {
    return;
  }

  if (strcmp(line, "HELLO") == 0) {
    Serial.println("ACK:HELLO");
    return;
  }

  if (strcmp(line, "IDLE?") == 0) {
    debugIdlePin();
    return;
  }

  if (strncmp(line, "BTN:", 4) == 0) {
    handleButtonCommand(line + 4);
    return;
  }

  Serial.print("ERR:UNKNOWN_USB:");
  Serial.println(line);
}


void serviceUSBSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      usbLine[usbLineLen] = '\0';
      handleUSBLine(usbLine);
      usbLineLen = 0;
    } else if (c != '\r') {
      if (usbLineLen < sizeof(usbLine) - 1) {
        usbLine[usbLineLen++] = c;
      } else {
        usbLineLen = 0;
        Serial.println("ERR:USB_LINE_TOO_LONG");
      }
    }
  }
}


// ===================== SEATALK RX DATAGRAM PARSING =====================

void serviceSeaTalkRX() {
  uint16_t word = 0;

  while (read9NonBlocking(word)) {
    handleSeaTalkWord(word);
  }

  // If a datagram stalls halfway, discard it.
  if (stLen > 0 && (uint32_t)(micros() - stLastByteUs) > 15000) {
    Serial.println("ST_RX_ABORT:TIMEOUT");
    stLen = 0;
    stExpectedLen = 0;
  }
}


void handleSeaTalkWord(uint16_t word) {
  bool commandBit = (word & 0x100) != 0;
  uint8_t b = word & 0xFF;

  stLastByteUs = micros();

  if (commandBit) {
    stLen = 0;
    stExpectedLen = 0;
    stBuf[stLen++] = b;
    return;
  }

  if (stLen == 0) {
    // Data byte without command byte.
    return;
  }

  if (stLen >= ST_MAX_DGRAM) {
    stLen = 0;
    stExpectedLen = 0;
    return;
  }

  stBuf[stLen++] = b;

  if (stLen == 2) {
    uint8_t n = stBuf[1] & 0x0F;

    // SeaTalk total datagram length:
    // command byte + attribute byte + N+1 data bytes = 3 + N
    stExpectedLen = 3 + n;

    if (stExpectedLen > ST_MAX_DGRAM) {
      Serial.println("ST_RX_ABORT:BAD_LEN");
      stLen = 0;
      stExpectedLen = 0;
      return;
    }
  }

  if (stExpectedLen > 0 && stLen >= stExpectedLen) {
    processSeaTalkDatagram(stBuf, stLen);
    stLen = 0;
    stExpectedLen = 0;
  }
}


void processSeaTalkDatagram(const uint8_t *d, uint8_t len) {
  printRawSeaTalkDatagram(d, len);

  switch (d[0]) {
    case 0x84:
      parseAutopilot84(d, len);
      break;

    case 0x10:
      parseWindAngle10(d, len);
      break;

    case 0x86:
      parseKey86(d, len);
      break;

    default:
      break;
  }
}


void printRawSeaTalkDatagram(const uint8_t *d, uint8_t len) {
  Serial.print("ST_RX:");
  for (uint8_t i = 0; i < len; i++) {
    Serial.print(" ");
    printHex2(d[i]);
  }
  Serial.println();
}


// ===================== DATAGRAM DECODERS =====================

void parseAutopilot84(const uint8_t *d, uint8_t len) {
  // Thomas Knauf:
  // 84 U6 VW XY 0Z 0M RR SS TT
  //
  // Contains:
  // - compass heading
  // - autopilot setpoint/course
  // - pilot mode bits
  // - rudder angle

  if (len < 9) {
    return;
  }

  uint8_t U = d[1] >> 4;
  uint8_t VW = d[2];
  uint8_t XY = d[3];
  uint8_t Z = d[4] & 0x0F;
  int8_t RR = (int8_t)d[6];

  float compassHeading =
      ((U & 0x03) * 90.0f) +
      ((VW & 0x3F) * 2.0f) +
      headingFineCorrection(U);

  float autopilotSetpoint =
      (((VW >> 6) & 0x03) * 90.0f) +
      (XY / 2.0f);

  compassHeading = normalize360(compassHeading);
  autopilotSetpoint = normalize360(autopilotSetpoint);

  const char *mode = decodePilotMode(Z);

  strncpy(lastMode, mode, sizeof(lastMode));
  lastMode[sizeof(lastMode) - 1] = '\0';

  lastRudder = RR;
  lastSetpoint = autopilotSetpoint;

  printStatusLine();

  Serial.print("INFO:COMPASS=");
  Serial.println(compassHeading, 1);
}


void parseWindAngle10(const uint8_t *d, uint8_t len) {
  // 10 01 XX YY
  // Apparent wind angle = XXYY / 2 degrees.

  if (len < 4) {
    return;
  }

  uint16_t raw = ((uint16_t)d[2] << 8) | d[3];
  float windAngle = raw / 2.0f;

  Serial.print("WIND_ANGLE:");
  Serial.println(windAngle, 1);
}


void parseKey86(const uint8_t *d, uint8_t len) {
  if (len < 4) {
    return;
  }

  Serial.print("KEY86:SRC=0x");
  printHex2(d[1]);
  Serial.print(";KEY=0x");
  printHex2(d[2]);
  Serial.print(";INV=0x");
  printHex2(d[3]);
  Serial.println();
}


const char *decodePilotMode(uint8_t Z) {
  if (Z & 0x08) {
    return "TRACK";
  }

  if (Z & 0x04) {
    return "WIND";
  }

  if (Z & 0x02) {
    return "AUTO";
  }

  return "STANDBY";
}


float headingFineCorrection(uint8_t U) {
  uint8_t highBits = U & 0x0C;

  if (highBits == 0x0C) {
    return 2.0f;
  }

  if (highBits != 0x00) {
    return 1.0f;
  }

  return 0.0f;
}


float normalize360(float deg) {
  while (deg >= 360.0f) {
    deg -= 360.0f;
  }

  while (deg < 0.0f) {
    deg += 360.0f;
  }

  return deg;
}


// ===================== STATUS OUTPUT =====================

void printStatusLine() {
  Serial.print("STATUS:MODE=");
  Serial.print(lastMode);
  Serial.print(";RUDDER=");
  Serial.print(lastRudder, 1);
  Serial.print(";SETPOINT=");
  Serial.println(lastSetpoint, 0);
}


// ===================== UTILS =====================

void printHex2(uint8_t b) {
  if (b < 0x10) {
    Serial.print("0");
  }

  Serial.print(b, HEX);
}


// ===================== ARDUINO SETUP/LOOP =====================

void setup() {
  Serial.begin(115200);

  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
  }

  pinMode(SEATALK_IDLE_SENSE_PIN, INPUT);

  setupUSART0_9bit_TXRX();

  Serial.println();
  Serial.println("Due SeaTalk 1 controller starting");
  Serial.println("USB protocol: BTN:<COMMAND>");
  Serial.println("GUI status: STATUS:MODE=<MODE>;RUDDER=<deg>;SETPOINT=<deg>");
  Serial.println("Idle check uses dedicated sense pin 22.");
  Serial.println("Wire pin 22 to the 3.3V RX node, same node as Due RX1 pin 19.");
  Serial.println("Expected polarity:");
  Serial.println("SeaTalk HIGH -> pin 22 HIGH");
  Serial.println("SeaTalk LOW  -> pin 22 LOW");

#if ENABLE_SEATALK_TX == 0
  Serial.println("SeaTalk TX is DISABLED. Set ENABLE_SEATALK_TX to 1 to transmit.");
#else
  Serial.println("SeaTalk TX is ENABLED.");
#endif

#if RX_IDLE_IS_HIGH
  Serial.println("RX_IDLE_IS_HIGH = 1");
#else
  Serial.println("RX_IDLE_IS_HIGH = 0");
#endif

  debugIdlePin();
}


void loop() {
  serviceUSBSerial();
  serviceSeaTalkRX();

#if DEBUG_IDLE_PIN
  if (millis() - lastIdleDebugMs > 500) {
    lastIdleDebugMs = millis();
    debugIdlePin();
  }
#endif

  // Heartbeat status for GUI.
  if (millis() - lastStatusPrintMs > 3000) {
    lastStatusPrintMs = millis();
    printStatusLine();
  }
}