// Copyright 2019 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__MightyClient_H__)
#define __MightyClient_H__

// For ATmega32 / MightyCore

#define TCCRA_REG TCCR2
#define OCRA_REG OCR2

#include "BaseClient.h"

// Data+: 8 (RXI)
// Data-: 10
using MightyDataClient = BaseDataClient<8, 10, 0, 2, 0x12, 0x12>;

// Sense: 15 (PWM - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
using MightySenseClient = BaseSenseClient<15, 0x19, 0b00010000, 0>;

#endif  // !defined(__MightyClient_H__)
