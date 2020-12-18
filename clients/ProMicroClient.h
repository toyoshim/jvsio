#if !defined(__ProMicroClient_H__)
#define __ProMicroClient_H__

#define TCCRA_REG TCCR1A
#define TCCRB_REG TCCR1B
#define OCRA_REG OCR1A

#include "BaseClient.h"

// Data+: 0 (RXI) (PD2)
// Data-: 2       (PD1)
using ProMicroDataClient = BaseDataClient<0, 2, 2, 1, 0x0b, 0x0b>;

// On ProMicro:
// Serial = USB CDC serial
// Serial1 = hardware serial port on PD2,PD3
template<> HardwareSerial& ProMicroDataClient::GetSerial() { return Serial1; }

// Sense: 9 (PWM OC1A - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
using ProMicroSenseClient = BaseSenseClient<9, 0b01000000, 0b01000000, 1>;

// Downstream Sense: A3 (0V - ready, 5V - terminated, 2.5V - not ready)
using ProMicroSenseClientSupportingDaisyChain = BaseSenseClientSupportingDaisyChain<9, 0b01000000, 0b01000000, 1, A3>;

// LED Ready: 17 (D17 RX LED)
using ProMicroLedClient = BaseLedClient<17>;

#endif  // !defined(__ProMicroClient_H__)
