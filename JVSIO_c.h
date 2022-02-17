// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdint.h>

#if !defined(__JVSIO_c_H__)
#define __JVSIO_c_H__

// JVSIO provides JVS (Jamma Video Standard 3.0) I/O transport,
// differential serial at 115.2Kbps, 8-bits, 1-start/stop bits, non parity.
// This library can not use with the Serial library because RXI is used
// internally.
// Incomoing data is analyzed by the hardware serial peripheral, RXI.
// The library just reads data from Data+ pin, but ignores Data-.
// Outgoint data is generated by hand-crafted code because the hardware
// does not support differential serial output.
struct JVSIO_DataClient {
  int (*available)(struct JVSIO_DataClient* client);
  void (*setInput)(struct JVSIO_DataClient* client);
  void (*setOutput)(struct JVSIO_DataClient* client);
  void (*startTransaction)(struct JVSIO_DataClient* client);
  void (*endTransaction)(struct JVSIO_DataClient* client);
  uint8_t (*read)(struct JVSIO_DataClient* client);
  void (*write)(struct JVSIO_DataClient* client, uint8_t data);
  void (*delayMicroseconds)(struct JVSIO_DataClient* client, unsigned int usec);
  void (*delay)(struct JVSIO_DataClient* client, unsigned int msec);
  void* work;
};

struct JVSIO_SenseClient {
  void (*begin)(struct JVSIO_SenseClient* client);
  void (*set)(struct JVSIO_SenseClient* client, bool ready);
  bool (*is_ready)(struct JVSIO_SenseClient* client);
  bool (*is_connected)(struct JVSIO_SenseClient* client);
  void* work;
};

struct JVSIO_LedClient {
  void (*begin)(struct JVSIO_LedClient* client);
  void (*set)(struct JVSIO_LedClient* client, bool ready);
  void* work;
};

enum JVSIO_Cmd {
  kCmdReset = 0xF0,       // mandatory
  kCmdAddressSet = 0xF1,  // mandatory

  kCmdIoId = 0x10,           // mandatory
  kCmdCommandRev = 0x11,     // mandatory
  kCmdJvRev = 0x12,          // mandatory
  kCmdProtocolVer = 0x13,    // mandatory
  kCmdFunctionCheck = 0x14,  // mandatory
  kCmdMainId = 0x15,

  kCmdSwInput = 0x20,
  kCmdCoinInput = 0x21,
  kCmdAnalogInput = 0x22,
  kCmdRotaryInput = 0x23,
  kCmdKeyCodeInput = 0x24,
  kCmdScreenPositionInput = 0x25,

  kCmdRetry = 0x2F,  // mandatory

  kCmdCoinSub = 0x30,
  kCmdDriverOutput = 0x32,
  kCmdCoinAdd = 0x35,

  kReportOk = 0x01,
  kReportParamErrorNoResponse = 0x02,
  kReportParamErrorIgnored = 0x03,
  kReportBusy = 0x04,
};

struct JVSIO_Work;

struct JVSIO_Lib {
  void (*begin)(struct JVSIO_Lib* lib);
  void (*end)(struct JVSIO_Lib* lib);

  // For client nodes.
  uint8_t* (*getNextCommand)(struct JVSIO_Lib* lib,
                             uint8_t* len,
                             uint8_t* node);
  void (*pushReport)(struct JVSIO_Lib* lib, uint8_t report);

  // For hosts.
  bool (*boot)(struct JVSIO_Lib* lib, bool block);
  bool (*sendAndReceive)(struct JVSIO_Lib* lib,
                         const uint8_t* packet,
                         uint8_t** ack,
                         uint8_t* ack_len);

  struct JVSIO_Work* work;
};

struct JVSIO_Lib* JVSIO_open(struct JVSIO_DataClient* data,
                             struct JVSIO_SenseClient* sense,
                             struct JVSIO_LedClient* led,
                             uint8_t nodes);

void JVSIO_close(struct JVSIO_Lib* lib);

#endif  // !defined(__JVSIO_c_H__)
