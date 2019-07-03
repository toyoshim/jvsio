#include <stdint.h>

#if !defined(__ProMicroClient_H__)
#define __ProMicroClient_H__

#include "../JVSIO.h"

// Data+: 0 (RXI) (PD2)
// Data-: 2       (PD1)
class ProMicroDataClient : public JVSIO::DataClient {
  const int portNumPlus    = 0;
  const int portNumMinus   = 2;
  int available() override;
  void setMode(int mode) override;
  void startTransaction() override;
  void endTransaction() override;
  uint8_t read() override;
  void write(uint8_t data)override;
};
 
// Sense: 9 (PWM OC1A - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
class ProMicroSenseClient : public JVSIO::SenseClient {
  const int portNum = 9;
  void begin() override;
  void set(bool ready) override;
};

// LED Ready: 21 (note : ProMicro don't have onboard LED)
class ProMicroLedClient : public JVSIO::LedClient {
  const int portNum = 21;
  void begin() override;
  void set(bool ready) override;
};

#endif  // !defined(__ProMicroClient_H__)
