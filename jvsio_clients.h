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
int JVSIO_DataClient_available();
void JVSIO_DataClient_setInput();
void JVSIO_DataClient_setOutput();
void JVSIO_DataClient_startTransaction();
void JVSIO_DataClient_endTransaction();
uint8_t JVSIO_DataClient_read();
void JVSIO_DataClient_write(uint8_t data);
void JVSIO_DataClient_dump(const char* str, uint8_t* data, uint8_t len);

bool JVSIO_SenseClient_isReady();
bool JVSIO_SenseClient_isConnected();

// Required for clients.
bool JVSIO_DataClient_setCommSupMode(enum JVSIO_CommSupMode mode, bool dryrun);

void JVSIO_SenseClient_set(bool ready);

void JVSIO_LedClient_set(bool ready);

void JVSIO_TimeClient_delayMicroseconds(unsigned int usec);

// Required for hosts.
uint32_t JVSIO_TimeClient_getTick();

void JVSIO_HostClient_receiveIoId(uint8_t address, uint8_t* data, uint8_t len);
void JVSIO_HostClient_receiveCommandRev(uint8_t address, uint8_t rev);
void JVSIO_HostClient_receiveJvRev(uint8_t address, uint8_t rev);
void JVSIO_HostClient_receiveProtocolVer(uint8_t address, uint8_t rev);
void JVSIO_HostClient_receiveFunctionCheck(uint8_t address,
                                           uint8_t* data,
                                           uint8_t len);
void JVSIO_HostClient_synced(uint8_t players,
                             uint8_t coin_state,
                             uint8_t* sw_state0,
                             uint8_t* sw_state1);

#endif  // !defined(__JVSIO_CLIENTS_H__)
