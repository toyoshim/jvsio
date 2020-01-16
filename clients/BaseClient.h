// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__BaseClient_H__)
#define __BaseClient_H__

#include <stdint.h>

#include "Arduino.h"
#include "../JVSIO.h"

template <int NUM_PLUS, int NUM_MINUS, int BIT_PLUS, int BIT_MINUS,
          int ADDR_PLUS, int ADDR_MINUS>
class BaseDataClient final : public JVSIO::DataClient {
  static constexpr uint8_t portNumPlus   = NUM_PLUS;
  static constexpr uint8_t portNumMinus  = NUM_MINUS;
  static constexpr uint8_t portBitPlus   = BIT_PLUS;
  static constexpr uint8_t portBitMinus  = BIT_MINUS;
  static constexpr uint8_t portAddrPlus  = ADDR_PLUS;
  static constexpr uint8_t portAddrMinus = ADDR_MINUS;

  int available() override { return Serial.available(); }
  void setMode(int mode) override {
    if (mode == INPUT) {
      pinMode(portNumPlus, INPUT);
      pinMode(portNumMinus, INPUT);
      Serial.begin(115200);
    } else {
      digitalWrite(portNumPlus, HIGH);
      digitalWrite(portNumMinus, LOW);
      pinMode(portNumPlus, OUTPUT);
      pinMode(portNumMinus, OUTPUT);
      Serial.end();
    }
  }
  void startTransaction() override { noInterrupts(); }
  void endTransaction() override { interrupts(); }
  uint8_t read() override { return Serial.read(); }
  void write(uint8_t data) override {
    // 138t for each bit.
    // 16MHz / 138 is nearly equals to 115200bps.
    asm("rjmp 4f\n"

        // Spends 134t = 8 + 1 + 3 x N - 1 + 2 + 4; N = 40
        "1:\n"
        "brcs 2f\n"                                 // 2t (1t for not taken)
        "nop\n"                                     // 1t
        "cbi %[portAddrDPlus], %[portBitDPlus]\n"   // 2t
        "sbi %[portAddrDMinus], %[portBitDMinus]\n" // 2t
        "rjmp 3f\n"                                 // 2t (1 + 1 + 2 + 2 + 2)
        "2:\n"
        "sbi %[portAddrDPlus], %[portBitDPlus]\n"   // 2t
        "cbi %[portAddrDMinus], %[portBitDMinus]\n" // 2t
        "rjmp 3f\n"                                 // 2t (2 + 2 + 2 + 2)
        "3:\n"
        "ldi r19, 40\n" // 1t
        "2:\n"
        "dec r19\n" // 1t
        "brne 2b\n" // 2t (1t for not taken)
        "nop\n"     // 1t
        "nop\n"     // 1t
        "ret\n"     // 4t

        // Sends Start, bit 0, ..., bit 7, Stop
        "4:\n"
        "mov r18, %[data]\n"
        // Start bit
        "sec\n"      // 1t
        "rcall 1b\n" // 3t
        "clc\n"      // 1t
        "rcall 1b\n" // 3t
        // Bit 0
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 1
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 2
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 3
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 4
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 5
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 6
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Bit 7
        "ror r18\n"  // 1t
        "rcall 1b\n" // 3t
        // Stop bit
        "sec\n"      // 1t
        "rcall 1b\n" // 3t
        :            // output operands
        :            // input operands
        [data] "r"(data), [portAddrDPlus] "I"(portAddrPlus),
        [portAddrDMinus] "I"(portAddrMinus), [portBitDPlus] "I"(portBitPlus),
        [portBitDMinus] "I"(portBitMinus));
  }
};

template <int PORT>
class BaseSenseClient : public JVSIO::SenseClient {
 protected:
  void begin() override {
    // CTC mode
    // Toggle output on matching the counter for OC2B (Pin 3)
    TCCR2A = 0b00010010;
    // Count from 0 to 1
    OCR2A = 1;
    // Stop
    TCCR2B = (TCCR2B & ~0b111) | 0b000;
    // Run at CLK/1
    TCCR2B = (TCCR2B & ~0b111) | 0b001;
    pinMode(portNum, OUTPUT);
    digitalWrite(portNum, LOW);
  }

 private:
  static constexpr uint8_t portNum = PORT;

  void set(bool ready) override {
    if (ready)
      TCCR2A &= ~0b00010000;
    else
      TCCR2A |= 0b00010000;
  }
};

template <int PORT, int SENSE>
class BaseSenseClientSupportingDaisyChain final : public BaseSenseClient<PORT> {
  void begin() override {
    BaseSenseClient<PORT>::begin();
    pinMode(SENSE, INPUT_PULLUP);
  }

  bool is_ready() override {
    int val = analogRead(SENSE);
    return val < 200 || 800 < val; // Roughly 0-1V or 4-5V
  }
};

template <int PORT>
class BaseLedClient final : public JVSIO::LedClient {
  static constexpr uint8_t portNum = PORT;

  void begin() override {}
  void set(bool ready) override {
    pinMode(PORT, OUTPUT);
    digitalWrite(PORT, ready ? HIGH : LOW);
  }
};

#endif  // !defined(__BaseClient_H__)
