// Copyright 2020 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__MegaClient_H__)
#define __MegaClient_H__

#include "BaseClient.h"

// Data+: 2 PD2 ( RXDI/INT2 )
// Data-: 3 PD3 ( TXD1/INT3 )
using MegaDataClient = BaseDataClient<0, 1, 0, 1, 0x0e, 0x0e>;

// Sense: 3 (PWM OC2B - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
using MegaSenseClient = BaseSenseClient<3>;

// Downstream Sense: A5 (0V - ready, 5V - terminated, 2.5V - not ready)
using MegaSenseClientSupportingDaisyChain =
    BaseSenseClientSupportingDaisyChain<3, A5>;

// LED Ready: 13
using MegaLedClient = BaseLedClient<13>;

#endif  // !defined(__MegaClient_H__)
