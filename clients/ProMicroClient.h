#include <stdint.h>

#if !defined(__ProMicroClient_H__)
#define __ProMicroClient_H__

// TODO: Migrate to use BaseDataClient template.

#include "../JVSIO.h"

// Data+: 0 (RXI) (PD2)
// Data-: 2       (PD1)
class ProMicroDataClient : public JVSIO::DataClient {
  static constexpr uint8_t portNumPlus    = 0;     // PD2 as D0
  static constexpr uint8_t portNumMinus   = 2;     // PD1 as D2
  static constexpr uint8_t portBitPlus    = 2;     // PD2
  static constexpr uint8_t portBitMinus   = 1;     // PD1
  static constexpr uint8_t portAddrPlus   = 0x0b;  // I/O address for PD
  static constexpr uint8_t portAddrMinus  = 0x0b;  // I/O address for PD

  int available() override;
  void setMode(int mode) override;
  void startTransaction() override;
  void endTransaction() override;
  uint8_t read() override;
  void write(uint8_t data) override;
};

// Sense: 9 (PWM OC1A - RC LPF of 100nF, 100Ω is needed to generate 2.5V)
class ProMicroSenseClient : public JVSIO::SenseClient {
 protected:
  void begin() override;

 private:
  static constexpr uint8_t portNum = 9;

  void set(bool ready) override;
};

// Downstream Sense: A3 (0V - ready, 5V - terminated, 2.5V - not ready)
class ProMicroSenseClientSupportingDaisyChain final
    : public ProMicroSenseClient {
  void begin() override;
  bool is_ready() override;
};

// LED Ready: 17 (D17 RX LED)
class ProMicroLedClient : public JVSIO::LedClient {
  static constexpr uint8_t portNum = 17;

  void begin() override;
  void set(bool ready) override;
};

#endif  // !defined(__ProMicroClient_H__)
