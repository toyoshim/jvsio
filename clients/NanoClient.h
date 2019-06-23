// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#if !defined(__NanoClient_H__)
#define __NanoClient_H__

#include "../JVSIO.h"

// Data+: 0 (RXI)
// Data-: 2
class NanoDataClient : public JVSIO::DataClient {
  int available() override;
  void setMode(int mode) override;
  void startTransaction() override;
  void endTransaction() override;
  uint8_t read() override;
  void write(uint8_t data)override;
};
 
// Sense: 3 (PWM - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
class NanoSenseClient : public JVSIO::SenseClient {
 public:
  void begin() override;
  void set(bool ready) override;
};

// LED Ready: 13
class NanoLedClient : public JVSIO::LedClient {
  void begin() override;
  void set(bool ready) override;
};

#endif  // !defined(__NanoClient_H__)
