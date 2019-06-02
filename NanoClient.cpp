// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "NanoClient.h"

#include "Arduino.h"

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
