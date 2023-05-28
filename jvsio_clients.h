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

struct JVSIO_DataClient {
  int (*available)(struct JVSIO_DataClient* client);
  void (*setInput)(struct JVSIO_DataClient* client);
  void (*setOutput)(struct JVSIO_DataClient* client);
  void (*startTransaction)(struct JVSIO_DataClient* client);
  void (*endTransaction)(struct JVSIO_DataClient* client);
  uint8_t (*read)(struct JVSIO_DataClient* client);
  void (*write)(struct JVSIO_DataClient* client, uint8_t data);
  bool (*setCommSupMode)(struct JVSIO_DataClient* client,
                         enum JVSIO_CommSupMode mode,
                         bool dryrun);
  void (*dump)(struct JVSIO_DataClient* client,
               const char* str,
               uint8_t* data,
               uint8_t len);
  void* work;
};

struct JVSIO_SenseClient {
  void (*begin)(struct JVSIO_SenseClient* client);
  void (*set)(struct JVSIO_SenseClient* client, bool ready);
  bool (*isReady)(struct JVSIO_SenseClient* client);
  bool (*isConnected)(struct JVSIO_SenseClient* client);
  void* work;
};

struct JVSIO_LedClient {
  void (*begin)(struct JVSIO_LedClient* client);
  void (*set)(struct JVSIO_LedClient* client, bool ready);
  void* work;
};

struct JVSIO_TimeClient {
  void (*delayMicroseconds)(struct JVSIO_TimeClient* client, unsigned int usec);
  void (*delay)(struct JVSIO_TimeClient* client, unsigned int msec);
  uint32_t (*getTick)(struct JVSIO_TimeClient* client);
  void* work;
};

struct JVSIO_HostClient {
  void (*receiveIoId)(struct JVSIO_HostClient* client,
                      uint8_t address,
                      uint8_t* data,
                      uint8_t len);
  void (*receiveCommandRev)(struct JVSIO_HostClient* client,
                            uint8_t address,
                            uint8_t rev);
  void (*receiveJvRev)(struct JVSIO_HostClient* client,
                       uint8_t address,
                       uint8_t rev);
  void (*receiveProtocolVer)(struct JVSIO_HostClient* client,
                             uint8_t address,
                             uint8_t rev);
  void (*receiveFunctionCheck)(struct JVSIO_HostClient* client,
                               uint8_t address,
                               uint8_t* data,
                               uint8_t len);
  void (*synced)(struct JVSIO_HostClient* client,
                 uint8_t players,
                 uint8_t coin_state,
                 uint8_t* sw_state0,
                 uint8_t* sw_state1);
  void* work;
};

#endif  // !defined(__JVSIO_CLIENTS_H__)
