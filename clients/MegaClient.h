// Copyright 2020 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__MegaClient_H__)
#define __MegaClient_H__

#include <stdint.h>

#include "../JVSIO.h"

// Data+: 2 PD2 ( RXDI/INT2 )
// Data-: 3 PD3 ( TXD1/INT3 )
class MegaDataClient final : public JVSIO::DataClient {
  static constexpr uint8_t portNumPlus   = 0;     // PD2 as D2
  static constexpr uint8_t portNumMinus  = 1;     // PD3 as D3
  static constexpr uint8_t portBitPlus   = 0;     // PD2
  static constexpr uint8_t portBitMinus  = 1;     // P3
  static constexpr uint8_t portAddrPlus  = 0x0e;  // I/O address for PD
  static constexpr uint8_t portAddrMinus = 0x0e;  // I/O address for PD

  int available() override;
  void setMode(int mode) override;
  void startTransaction() override;
  void endTransaction() override;
  uint8_t read() override;
  void write(uint8_t data) override;
};

// Sense: 3 (PWM OC2B - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
class MegaSenseClient : public JVSIO::SenseClient {
 protected:
  void begin() override;

 private:
  static constexpr uint8_t portNum = 3;

  void set(bool ready) override;
};

// Downstream Sense: A5 (0V - ready, 5V - terminated, 2.5V - not ready)
class MegaSenseClientSupportingDaisyChain final : public MegaSenseClient {
  void begin() override;
  bool is_ready() override;
};

// LED Ready: 13
class MegaLedClient final : public JVSIO::LedClient {
  static constexpr uint8_t portNum = 13;

  void begin() override;
  void set(bool ready) override;
};

#endif  // !defined(__MegaClient_H__)
