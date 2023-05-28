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
  int (*available)();
  void (*setInput)();
  void (*setOutput)();
  void (*startTransaction)();
  void (*endTransaction)();
  uint8_t (*read)();
  void (*write)(uint8_t data);
  bool (*setCommSupMode)(enum JVSIO_CommSupMode mode, bool dryrun);
  void (*dump)(const char* str, uint8_t* data, uint8_t len);
};

struct JVSIO_SenseClient {
  void (*set)(bool ready);
  bool (*isReady)();
  bool (*isConnected)();
};

struct JVSIO_LedClient {
  void (*set)(bool ready);
};

struct JVSIO_TimeClient {
  void (*delayMicroseconds)(unsigned int usec);
  void (*delay)(unsigned int msec);
  uint32_t (*getTick)();
};

struct JVSIO_HostClient {
  void (*receiveIoId)(uint8_t address, uint8_t* data, uint8_t len);
  void (*receiveCommandRev)(uint8_t address, uint8_t rev);
  void (*receiveJvRev)(uint8_t address, uint8_t rev);
  void (*receiveProtocolVer)(uint8_t address, uint8_t rev);
  void (*receiveFunctionCheck)(uint8_t address, uint8_t* data, uint8_t len);
  void (*synced)(uint8_t players,
                 uint8_t coin_state,
                 uint8_t* sw_state0,
                 uint8_t* sw_state1);
};

#endif  // !defined(__JVSIO_CLIENTS_H__)
