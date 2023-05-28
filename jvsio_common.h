// Copyright 2023 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__JVSIO_COMMON_H__)
#define __JVSIO_COMMON_H__

enum JVSIO {
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
  kCmdAnalogOutput = 0x33,
  kCmdCharacterOutput = 0x34,
  kCmdCoinAdd = 0x35,

  kCmdNamco = 0x70,  // vendor specific.

  // JVS Dash
  kCmdCommSup = 0xD0,
  kCmdCommChg = 0xF2,

  kReportOk = 0x01,
  kReportParamErrorNoResponse = 0x02,
  kReportParamErrorIgnored = 0x03,
  kReportBusy = 0x04,

  kHostAddress = 0x00,
  kBroadcastAddress = 0xFF,
  kMarker = 0xD0,
  kSync = 0xE0,
};

#endif  // !defined(__JVSIO_COMMON_H__)