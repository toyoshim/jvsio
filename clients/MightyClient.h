// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__MightyClient_H__)
#define __MightyClient_H__

// TODO: Migrate to use BaseDataClient template.

#include <stdint.h>

#include "../JVSIO.h"

// Data+: 8 (RXI)
// Data-: 10
class MightyDataClient : public JVSIO::DataClient {
  int available() override;
  void setMode(int mode) override;
  void startTransaction() override;
  void endTransaction() override;
  uint8_t read() override;
  void write(uint8_t data) override;
};

// Sense: 15 (PWM - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
class MightySenseClient : public JVSIO::SenseClient {
 public:
  void begin() override;
  void set(bool ready) override;
};

#endif  // !defined(__MightyClient_H__)
