// Copyright 2020 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__NanoClient_H__)
#define __NanoClient_H__

#define TCCRA_REG TCCR2A
#define TCCRB_REG TCCR2B
#define OCRA_REG OCR2A

#include "BaseClient.h"

// Data+: D0 (RXD) (PD0)
// Data-: D2       (PD2)
using NanoDataClient = BaseDataClient<0, 2, 0, 2, 0x0b, 0x0b>;

// Sense: D3 (PWM OC2B - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
using NanoSenseClient = BaseSenseClient<3, 0b00010010, 0b00010000, 1>;

// Downstream Sense: A5 (0V - ready, 5V - terminated, 2.5V - not ready)
using NanoSenseClientSupportingDaisyChain =
    BaseSenseClientSupportingDaisyChain<3, 0b00010010, 0b00010000, 1, A5>;

// Host Sense: A0 (0V - ready, 5V - disconnected, 2.5V - connected)
using NanoHostSenseClient = BaseHostSenseClient<A0>;

// LED Ready: D13
using NanoLedClient = BaseLedClient<13>;

#endif  // !defined(__NanoClient_H__)
