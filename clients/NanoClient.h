// Copyright 2020 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__NanoClient_H__)
#define __NanoClient_H__

#include "BaseClient.h"

// Data+: 0 (RXI) (PD0)
// Data-: 2       (PD2)
using NanoDataClient = BaseDataClient<0, 2, 0, 2, 0x0b, 0x0b>;

// Sense: 3 (PWM OC2B - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
using NanoSenseClient = BaseSenseClient<3>;

// Downstream Sense: A5 (0V - ready, 5V - terminated, 2.5V - not ready)
using NanoSenseClientSupportingDaisyChain =
    BaseSenseClientSupportingDaisyChain<3, A5>;

// LED Ready: 13
using NanoLedClient = BaseLedClient<13>;

#endif  // !defined(__NanoClient_H__)
