// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__NanoClient_H__)
#define __NanoClient_H__

#include <stdint.h>

#include "../JVSIO.h"

// Data+: 0 (RXI) (PD0)
// Data-: 2       (PD2)
class NanoDataClient final : public JVSIO::DataClient {
  static constexpr uint8_t portNumPlus   = 0;     // PD0 as D0
  static constexpr uint8_t portNumMinus  = 2;     // PD2 as D2
  static constexpr uint8_t portBitPlus   = 0;     // PD0
  static constexpr uint8_t portBitMinus  = 2;     // PD2
  static constexpr uint8_t portAddrPlus  = 0x0b;  // I/O address for PD
  static constexpr uint8_t portAddrMinus = 0x0b;  // I/O address for PD

  int available() override;
  void setMode(int mode) override;
  void startTransaction() override;
  void endTransaction() override;
  uint8_t read() override;
  void write(uint8_t data) override;
};
 
// Sense: 3 (PWM OC2B - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
class NanoSenseClient : public JVSIO::SenseClient {
 protected:
  void begin() override;

 private:
  static constexpr uint8_t portNum = 3;

  void set(bool ready) override;
};

// Downstream Sense: A5 (0V - ready, 5V - terminated, 2.5V - not ready)
class NanoSenseClientSupportingDaisyChain final : public NanoSenseClient {
  void begin() override;
  bool is_ready() override;
};

// LED Ready: 13
class NanoLedClient final : public JVSIO::LedClient {
  static constexpr uint8_t portNum = 13;

  void begin() override;
  void set(bool ready) override;
};

#endif  // !defined(__NanoClient_H__)
