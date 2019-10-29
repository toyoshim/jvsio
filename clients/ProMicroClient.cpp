// JVSIO Client for SparkFun ProMicro
// You must add Additional Board Manager to Arduino IDE.
// See : https://learn.sparkfun.com/tutorials/pro-micro--fio-v3-hookup-guide


#include "ProMicroClient.h"

#include "Arduino.h"

// On ProMicro:
// Serial = USB CDC serial
// Serial1 = hardware serial port on PD2,PD3

int ProMicroDataClient::available() {
  return Serial1.available();
}

void ProMicroDataClient::startTransaction() {
  noInterrupts();
}

void ProMicroDataClient::endTransaction() {
  interrupts();
}

void ProMicroDataClient::setMode(int mode) {
  if (mode == INPUT) {
    pinMode(portNumPlus , INPUT);
    pinMode(portNumMinus, INPUT);
    Serial1.begin(115200);
  } else {
    digitalWrite(portNumPlus , HIGH);
    digitalWrite(portNumMinus, LOW);
    pinMode(portNumPlus , OUTPUT);
    pinMode(portNumMinus, OUTPUT);
    Serial1.end();
  }
}

uint8_t ProMicroDataClient::read() {
  return Serial1.read();
}

// generate differential signal
void ProMicroDataClient::write(uint8_t data) {
  // 138t for each bit.
  // 16MHz / 138 is nearly equals to 115200bps.
  asm (
    "rjmp 4f\n"

   // Spends 134t = 8 + 1 + 3 x N - 1 + 2 + 4; N = 40
   "1:\n"
    "brcs 2f\n"      // 2t (1t for not taken)
    "nop\n"          // 1t
    "cbi %[portAddrDPlus], %[portBitDPlus]\n"   // 2t
    "sbi %[portAddrDMinus], %[portBitDMinus]\n" // 2t
    "rjmp 3f\n"      // 2t (1 + 1 + 2 + 2 + 2)
   "2:\n"
    "sbi %[portAddrDPlus], %[portBitDPlus]\n"   // 2t
    "cbi %[portAddrDMinus], %[portBitDMinus]\n" // 2t
    "rjmp 3f\n"      // 2t (2 + 2 + 2 + 2)
   "3:\n"
    "ldi r19, 40\n"  // 1t
   "2:\n"
    "dec r19\n"      // 1t
    "brne 2b\n"      // 2t (1t for not taken)
    "nop\n"          // 1t
    "nop\n"          // 1t
    "ret\n"          // 4t

   // Sends Start, bit 0, ..., bit 7, Stop
   "4:\n"
    "mov r18, %[data]\n"
    // Start bit
    "sec\n"         // 1t
    "rcall 1b\n"    // 3t
    "clc\n"         // 1t
    "rcall 1b\n"    // 3t
    // Bit 0
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 1
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 2
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 3
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 4
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 5
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 6
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Bit 7
    "ror r18\n"     // 1t
    "rcall 1b\n"    // 3t
    // Stop bit
    "sec\n"         // 1t
    "rcall 1b\n"    // 3t
    : //output operand
    : //input operand
    [data]"r"(data),
    [portAddrDPlus]"I"(portAddrPlus),
    [portAddrDMinus]"I"(portAddrMinus),
    [portBitDPlus]"I"(portBitPlus),
    [portBitDMinus]"I"(portBitMinus)
    );
}

void ProMicroSenseClient::begin() {
  // CTC mode
  // Toggle output on matching the counter for OC1A (Pin 9)
  TCCR1A = 0b01000000;
  // Count from 0 to 1
  OCR1A = 1;
  // Stop
  TCCR1B = (TCCR1B & ~0b111) | 0b000;
  // Run at CLK/1
  TCCR1B = (TCCR1B & ~0b111) | 0b001;
  pinMode(portNum, OUTPUT);
  digitalWrite(portNum, LOW);
}

void ProMicroSenseClient::set(bool ready) {
  if (ready)
    TCCR1A &= ~0b01000000;
  else
    TCCR1A |=  0b01000000;
}

void ProMicroSenseClientSupportingDaisyChain::begin() {
  ProMicroSenseClient::begin();
  pinMode(A3, INPUT_PULLUP);
}

bool ProMicroSenseClientSupportingDaisyChain::is_ready() {
  int val = analogRead(A3);
  return val < 200 || 800 < val;  // Roughly 0-1V or 4-5V
}

void ProMicroLedClient::begin() {
}

void ProMicroLedClient::set(bool ready) {
  pinMode(portNum, OUTPUT);
  digitalWrite(portNum, ready ? HIGH : LOW);
}
