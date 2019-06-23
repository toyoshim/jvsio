// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "MightyClient.h"

#include "Arduino.h"

int MightyDataClient::available() {
  return Serial.available();
}

void MightyDataClient::startTransaction() {
  noInterrupts();
}

void MightyDataClient::endTransaction() {
  interrupts();
}

void MightyDataClient::setMode(int mode) {
  if (mode == INPUT) {
    pinMode(8, INPUT);
    pinMode(10, INPUT);
    Serial.begin(115200);
  } else {
    digitalWrite(8, HIGH);
    digitalWrite(10, LOW);
    pinMode(8, OUTPUT);
    pinMode(10, OUTPUT);
    Serial.end();
  }
}

uint8_t MightyDataClient::read() {
  return Serial.read();
}

void MightyDataClient::write(uint8_t data) {
  // 138t for each bit.
  asm (
    "rjmp 4f\n"

   // Spends 134t = 8 + 1 + 3 x N - 1 + 2 + 4; N = 40
   "1:\n"
    "brcs 2f\n"      // 2t (1t for not taken)
    "nop\n"          // 1t
    "cbi 0x12, 0\n"  // 2t
    "sbi 0x12, 2\n"  // 2t
    "rjmp 3f\n"      // 2t (1 + 1 + 2 + 2 + 2)
   "2:\n"
    "sbi 0x12, 0\n"  // 2t
    "cbi 0x12, 2\n"  // 2t
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

void MightySenseClient::begin() {
  TCCR2 = 0x19;  // CTC mode, toggle on matching
  OCR2 = 0;      // Match on every cycles
  pinMode(15, OUTPUT);
  digitalWrite(15, LOW);
}

void MightySenseClient::set(bool ready) {
  if (ready)
    TCCR2 &= ~0x30;
  else
    TCCR2 |= 0x10;
}

