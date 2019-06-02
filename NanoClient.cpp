// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "NanoClient.h"

#include "Arduino.h"

int NanoDataClient::available() {
  return Serial.available();
}

void NanoDataClient::startTransaction() {
  noInterrupts();
}

void NanoDataClient::endTransaction() {
  interrupts();
}

void NanoDataClient::setMode(int mode) {
  if (mode == INPUT) {
    pinMode(0, INPUT);
    pinMode(2, INPUT);
    Serial.begin(115200);
  } else {
    digitalWrite(0, HIGH);
    digitalWrite(2, LOW);
    pinMode(0, OUTPUT);
    pinMode(2, OUTPUT);
    Serial.end();
  }
}

uint8_t NanoDataClient::read() {
  return Serial.read();
}

void NanoDataClient::write(uint8_t data) {
  // 138t for each bit.
  asm (
    "rjmp 4f\n"

   // Spends 134t = 8 + 1 + 3 x N - 1 + 2 + 4; N = 40
   "1:\n"
    "brcs 2f\n"      // 2t (1t for not taken)
    "nop\n"          // 1t
    "cbi 0x0b, 0\n"  // 2t
    "sbi 0x0b, 2\n"  // 2t
    "rjmp 3f\n"      // 2t (1 + 1 + 2 + 2 + 2)
   "2:\n"
    "sbi 0x0b, 0\n"  // 2t
    "cbi 0x0b, 2\n"  // 2t
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
    "mov r18, %0\n"
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
   :: "r" (data));
}

void NanoSenseClient::begin() {
  // CTC mode
  // Toggle output on matching the counter for Ch.B (Pin 3)
  TCCR2A = 0x12;
  // Count from 0 to 1
  OCR2A = 1;
  // Stop
  TCCR2B = (TCCR2B & ~7) | 0;
  // Run at CLK/1
  TCCR2B = (TCCR2B & ~7) | 1;
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
}

void NanoSenseClient::set(bool ready) {
  if (ready)
    TCCR2A &= ~0x30;
  else
    TCCR2A |= 0x10;
}

void NanoLedClient::begin() {
}

void NanoLedClient::set(bool ready) {
  pinMode(13, OUTPUT);
  digitalWrite(13, ready ? HIGH : LOW);
}
