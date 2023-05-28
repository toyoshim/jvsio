// Copyright 2023 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__JVSIO_CLIENTS_H__)
#define __JVSIO_CLIENTS_H__

#include <stdbool.h>
#include <stdint.h>

enum JVSIO_CommSupMode {
  k115200 = 0,
  k1M = 1,
  k3M = 2,
};

// Client APIs should be implemented by JVSIO users to handle physical device
// operaqtions, such as driving bus signals or controlling led hints.

// Required for both clients and hosts.
int JVSIO_Client_isDataAvailable();
void JVSIO_Client_willSend();
void JVSIO_Client_willReceive();
void JVSIO_Client_send(uint8_t data);
uint8_t JVSIO_Client_receive();
void JVSIO_Client_dump(const char* str, uint8_t* data, uint8_t len);
bool JVSIO_Client_isSenseReady();
bool JVSIO_Client_isSenseConnected();

// Required for clients.
bool JVSIO_Client_setCommSupMode(enum JVSIO_CommSupMode mode, bool dryrun);
void JVSIO_Client_setSense(bool ready);
void JVSIO_Client_setLed(bool ready);
void JVSIO_Client_delayMicroseconds(unsigned int usec);

// Required for hosts.
uint32_t JVSIO_Client_getTick();
void JVSIO_Client_ioIdReceived(uint8_t address, uint8_t* data, uint8_t len);
void JVSIO_Client_commandRevReceived(uint8_t address, uint8_t rev);
void JVSIO_Client_jvRevReceived(uint8_t address, uint8_t rev);
void JVSIO_Client_protocolVerReceived(uint8_t address, uint8_t rev);
void JVSIO_Client_functionCheckReceived(uint8_t address,
                                        uint8_t* data,
                                        uint8_t len);
void JVSIO_Client_synced(uint8_t players,
                         uint8_t coin_state,
                         uint8_t* sw_state0,
                         uint8_t* sw_state1);

#endif  // !defined(__JVSIO_CLIENTS_H__)
