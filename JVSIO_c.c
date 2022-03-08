// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "JVSIO_c.h"

#include <stdlib.h>

struct JVSIO_Work {
  struct JVSIO_DataClient* data;
  struct JVSIO_SenseClient* sense;
  struct JVSIO_LedClient* led;
  uint8_t nodes;
  uint8_t rx_data[256];
  uint8_t rx_size;
  uint8_t rx_read_ptr;
  bool rx_receiving;
  bool rx_escaping;
  bool rx_available;
  bool rx_error;
  uint8_t address[2];
  uint8_t new_address;
  uint8_t tx_data[256];
  uint8_t tx_report_size;
  bool downstream_ready;
};

static struct JVSIO_Lib lib;
static struct JVSIO_Work work;

enum {
  kHostAddress = 0x00,
  kBroadcastAddress = 0xFF,
  kMarker = 0xD0,
  kSync = 0xE0,
};

#ifdef __SDCC
#pragma save
#pragma disable_warning 85
#include "serial.h"
#endif
static void dump(const char* str, uint8_t* data, size_t len) {
  // TODO: Define DebugClient.
  // do Serial.begin(); for Arduino series which have native USB CDC (=Serial),
  // such as Leonardo, ProMicro, etc.
  Serial.print(str);
  Serial.print(": ");
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 16)
      Serial.print("0");
#ifdef __SDCC
    Serial.printc(data[i], HEX);
#else
    Serial.print(data[i], HEX);
#endif
    Serial.print(" ");
  }
  Serial.println("");
}
#ifdef __SDCC
#pragma restore
#endif

static void writeEscapedByte(struct JVSIO_DataClient* client, uint8_t data) {
  if (data == kMarker || data == kSync) {
    client->write(client, kMarker);
    client->write(client, data - 1);
  } else {
    client->write(client, data);
  }
}

static uint8_t getCommandSize(uint8_t* command, uint8_t len) {
  switch (*command) {
    case kCmdReset:
    case kCmdAddressSet:
      return 2;
    case kCmdIoId:
    case kCmdCommandRev:
    case kCmdJvRev:
    case kCmdProtocolVer:
    case kCmdFunctionCheck:
      return 1;
    case kCmdMainId:
      break;  // handled later
    case kCmdSwInput:
      return 3;
    case kCmdCoinInput:
    case kCmdAnalogInput:
    case kCmdRotaryInput:
      return 2;
    case kCmdKeyCodeInput:
      return 1;
    case kCmdScreenPositionInput:
      return 2;
    case kCmdRetry:
      return 1;
    case kCmdCoinSub:
    case kCmdCoinAdd:
      return 4;
    case kCmdDriverOutput:
      return command[1] + 2;
    case kCmdAnalogOutput:
      return command[1] * 2 + 2;
    case kCmdCharacterOutput:
      return command[1] + 2;
    case kCmdNamco:
      switch (command[4]) {
        case 0x02:
          return 7;
        case 0x14:
          return 12;
        case 0x80:
          return 6;
      }
      return 0;
    default:
      dump("unknown command", command, 1);
      return 0;  // not supported
  }
  uint8_t size = 2;
  for (uint8_t i = 1; i < len && command[i]; ++i)
    size++;
  return size;
}

static bool matchAddress(struct JVSIO_Work* work) {
  uint8_t target = work->rx_data[0];
  for (uint8_t i = 0; i < work->nodes; ++i) {
    if (target == work->address[i])
      return true;
  }
  return false;
}

static void senseNotReady(struct JVSIO_Work* work) {
  work->sense->set(work->sense, false);
  work->led->set(work->led, false);
}

static void senseReady(struct JVSIO_Work* work) {
  work->sense->set(work->sense, true);
  work->led->set(work->led, true);
}

static void sendPacket(struct JVSIO_Work* work, const uint8_t* data) {
  work->data->startTransaction(work->data);
  work->data->write(work->data, kSync);
  uint8_t sum = 0;

  for (uint8_t i = 0; i <= data[1]; ++i) {
    sum += data[i];
    writeEscapedByte(work->data, data[i]);
  }
  writeEscapedByte(work->data, sum);
  work->data->endTransaction(work->data);

  work->data->setInput(work->data);
}

static void sendStatus(struct JVSIO_Work* work) {
  work->rx_available = false;
  work->rx_receiving = false;

  // Should not reply for broadcast commands.
  if (kBroadcastAddress == work->address[work->nodes - 1] &&
      kBroadcastAddress == work->new_address) {
    return;
  }

  // Direction should be changed within 100usec from sending/receiving a packet.
  work->data->setOutput(work->data);

  // Spec requires 100usec interval at minimum between each packet.
  // But response should be sent within 1msec from the last byte received.
  work->data->delayMicroseconds(work->data, 100);

  // Address is just assigned.
  // This timing to negate the sense signal is subtle. The spec expects this
  // signal should be done in 1msec. This place meets this requiment, but if
  // this action was too quick, the direct upstream client may misunderstand
  // that the last address command was for the upstream client. So, this signal
  // should be changed lately as we can as possible within the spec requiment.
  // However, as described below, sending response packet may take over 1msec.
  // Thus, this is the last place to negate the signal in a simle way.
  if (kBroadcastAddress != work->new_address) {
    for (uint8_t i = 0; i < work->nodes; ++i) {
      if (work->address[i] != kBroadcastAddress)
        continue;
      work->address[i] = work->new_address;
      if (i == (work->nodes - 1))
        senseReady(work);
      break;
    }
    work->new_address = kBroadcastAddress;
  }

  // We can send about 14 bytes per 1msec at maximum. So, it will take over 18
  // msec to send the largest packet. Actual packet will have interval time
  // between each byte. In total, it may take more time.
  sendPacket(work, work->tx_data);
}

static void sendOverflowStatus(struct JVSIO_Work* work) {
  work->tx_data[0] = kHostAddress;
  work->tx_data[1] = 2;
  work->tx_data[2] = 0x04;
  sendStatus(work);
}

static void sendOkStatus(struct JVSIO_Work* work) {
  if (work->tx_report_size > 253)
    return sendOverflowStatus(work);
  work->tx_data[0] = kHostAddress;
  work->tx_data[1] = 2 + work->tx_report_size;
  work->tx_data[2] = 0x01;
  sendStatus(work);
}

static void sendUnknownCommandStatus(struct JVSIO_Work* work) {
  if (work->tx_report_size > 253)
    return sendOverflowStatus(work);
  work->tx_data[0] = kHostAddress;
  work->tx_data[1] = 2 + work->tx_report_size;
  work->tx_data[2] = 0x02;
  sendStatus(work);
}

static void sendSumErrorStatus(struct JVSIO_Work* work) {
  work->tx_data[0] = kHostAddress;
  work->tx_data[1] = 2;
  work->tx_data[2] = 0x03;
  sendStatus(work);
}

static void pushReport(struct JVSIO_Lib* lib, uint8_t report) {
  struct JVSIO_Work* work = lib->work;
  if (work->tx_report_size == 253) {
    sendOverflowStatus(work);
    work->tx_report_size++;
  } else if (work->tx_report_size < 253) {
    work->tx_data[3 + work->tx_report_size] = report;
    work->tx_report_size++;
  }
}

static void sendUnknownStatus(struct JVSIO_Lib* lib) {
  return sendUnknownCommandStatus(lib->work);
}

static void receive(struct JVSIO_Work* work) {
  while (!work->rx_available && work->data->available(work->data) > 0) {
    uint8_t data = work->data->read(work->data);
    if (data == kSync) {
      work->rx_size = 0;
      work->rx_receiving = true;
      work->rx_escaping = false;
      work->rx_error = false;
      work->downstream_ready = work->sense->is_ready(work->sense);
      continue;
    }
    if (!work->rx_receiving)
      continue;
    if (data == kMarker) {
      work->rx_escaping = true;
      continue;
    }
    if (work->rx_escaping) {
      work->rx_data[work->rx_size++] = data + 1;
      work->rx_escaping = false;
    } else {
      work->rx_data[work->rx_size++] = data;
    }
    if (work->rx_size >= 2 && ((work->rx_data[1] + 2) == work->rx_size)) {
      uint8_t sum = 0;
      for (size_t i = 0; i < (work->rx_size - 1u); ++i)
        sum += work->rx_data[i];
      if ((work->rx_data[0] == kBroadcastAddress &&
           work->rx_data[2] == kCmdReset) ||
          matchAddress(work)) {
        // Broadcasrt or for this device.
        if (work->rx_data[work->rx_size - 1] != sum) {
          if (work->address[0] == kHostAddress) {
            // Host mode does not need to send an error response back.
            // Set `rx_error` flag instead for later references.
            work->rx_error = true;
          } else {
            sendSumErrorStatus(work);
            work->rx_size = 0;
          }
        } else {
          work->rx_read_ptr = 2;  // Skip address and length
          work->rx_available = true;
          work->tx_report_size = 0;
        }
      } else {
        // For other devices.
        work->rx_size = 0;
      }
    }
  }
}

static void begin(struct JVSIO_Lib* lib) {
  lib->work->data->setInput(lib->work->data);
  lib->work->sense->begin(lib->work->sense);
  lib->work->led->begin(lib->work->led);
}

static void end(struct JVSIO_Lib* lib) {
  lib->begin = NULL;
  lib->end = NULL;
  lib->getNextCommand = NULL;
}

static uint8_t* getNextCommand(struct JVSIO_Lib* lib,
                               uint8_t* len,
                               uint8_t* node) {
  struct JVSIO_Work* work = lib->work;
  uint8_t i;

  for (;;) {
    receive(work);
    if (!work->rx_available)
      return NULL;

    if (node != NULL) {
      *node = 255;
      for (i = 0; i < work->nodes; ++i) {
        if (work->address[i] == work->rx_data[0])
          *node = i;
      }
    }
    uint8_t max_command_size = work->rx_data[1] - work->rx_read_ptr + 1;
    if (!max_command_size) {
      sendOkStatus(work);
      continue;
    }
    uint8_t command_size =
        getCommandSize(&work->rx_data[work->rx_read_ptr], max_command_size);
    if (!command_size) {
      sendUnknownCommandStatus(work);
      continue;
    }
    if (command_size > max_command_size) {
      pushReport(lib, kReportParamErrorNoResponse);
      sendUnknownCommandStatus(work);
      continue;
    }
    switch (work->rx_data[work->rx_read_ptr]) {
      case kCmdReset:
        senseNotReady(work);
        for (i = 0; i < work->nodes; ++i)
          work->address[i] = kBroadcastAddress;
        work->rx_available = false;
        work->rx_receiving = false;
        dump("reset", NULL, 0);
        work->rx_read_ptr += command_size;
        return &work->rx_data[work->rx_read_ptr - command_size];
      case kCmdAddressSet:
        if (work->downstream_ready) {
          work->new_address = work->rx_data[work->rx_read_ptr + 1];
          dump("address", &work->rx_data[work->rx_read_ptr + 1], 1);
        }
        pushReport(lib, kReportOk);
        break;
      case kCmdCommandRev:
        pushReport(lib, kReportOk);
        pushReport(lib, 0x13);
        break;
      case kCmdJvRev:
        pushReport(lib, kReportOk);
        pushReport(lib, 0x30);
        break;
      case kCmdProtocolVer:
        pushReport(lib, kReportOk);
        pushReport(lib, 0x10);
        break;
      case kCmdMainId:
        // We may hold the Id to provide it for the client code, but let's just
        // ignore it for now. It seems newer namco boards send this command,
        // e.g. BNGI.;WinArc;Ver"2.2.4";JPN, and expects OK status to proceed.
        pushReport(lib, kReportOk);
        break;
      case kCmdRetry:
        sendStatus(work);
        break;
      case kCmdIoId:
      case kCmdFunctionCheck:
      case kCmdSwInput:
      case kCmdCoinInput:
      case kCmdAnalogInput:
      case kCmdRotaryInput:
      case kCmdKeyCodeInput:
      case kCmdScreenPositionInput:
      case kCmdCoinSub:
      case kCmdDriverOutput:
      case kCmdAnalogOutput:
      case kCmdCharacterOutput:
      case kCmdCoinAdd:
      case kCmdNamco:
        *len = command_size;
        work->rx_read_ptr += command_size;
        return &work->rx_data[work->rx_read_ptr - command_size];
      default:
        sendUnknownCommandStatus(work);
        break;
    }
    work->rx_read_ptr += command_size;
  }
}

static uint8_t* receiveStatus(struct JVSIO_Work* work, uint8_t* len) {
  do {
    receive(work);  // TODO: timeout.
    if (!work->sense->is_connected(work->sense))
      return NULL;
  } while (!work->rx_available);
  if (work->rx_error)
    return NULL;
  *len = work->rx_data[1] - 1;
  work->rx_size = 0;
  work->rx_available = false;
  work->rx_receiving = false;
  return &work->rx_data[2];
}

static bool sendAndReceive(struct JVSIO_Lib* lib,
                           const uint8_t* packet,
                           uint8_t** ack,
                           uint8_t* ack_len) {
  struct JVSIO_Work* work = lib->work;

  work->data->setOutput(work->data);
  sendPacket(work, packet);
  *ack = receiveStatus(work, ack_len);
  return *ack != NULL;
}

static bool boot(struct JVSIO_Lib* lib, bool block) {
  struct JVSIO_Work* work = lib->work;

  work->address[0] = kHostAddress;

  // Spec requires to wait for 2 seconds before starting any host operation.
  work->data->delay(work->data, 2000);

  bool stop = false;
  for (;;) {
    stop = !block;
    while (!work->sense->is_connected(work->sense)) {
      if (stop)
        return false;
    }

    // Reset x2
    work->tx_data[0] = kBroadcastAddress;
    work->tx_data[1] = 3;  // Bytes
    work->tx_data[2] = kCmdReset;
    work->tx_data[3] = 0xd9;  // Magic number.
    work->data->setOutput(work->data);
    sendPacket(work, work->tx_data);
    work->data->delayMicroseconds(work->data, 100);
    work->data->setInput(work->data);
    sendPacket(work, work->tx_data);
    work->data->delayMicroseconds(work->data, 100);

    // Set address.
    work->tx_data[0] = kBroadcastAddress;
    work->tx_data[1] = 3;  // Bytes
    work->tx_data[2] = kCmdAddressSet;
    work->tx_data[3] = 1;  // Address
    uint8_t* ack;
    uint8_t ack_len;
    if (!sendAndReceive(lib, work->tx_data, &ack, &ack_len))
      continue;
    if (ack_len != 2)
      continue;
    if (ack[0] != 1 || ack[1] != 1)
      continue;
    work->data->delayMicroseconds(work->data, 1000);
    if (!work->sense->is_ready(work->sense))
      continue;
    break;
  }
  return true;
}

struct JVSIO_Lib* JVSIO_open(struct JVSIO_DataClient* data,
                             struct JVSIO_SenseClient* sense,
                             struct JVSIO_LedClient* led,
                             uint8_t nodes) {
  lib.work = &work;
  struct JVSIO_Lib* jvsio = &lib;
  struct JVSIO_Work* work = jvsio->work;

  jvsio->begin = begin;
  jvsio->end = end;
  jvsio->getNextCommand = getNextCommand;
  jvsio->pushReport = pushReport;
  jvsio->sendUnknownStatus = sendUnknownStatus;
  jvsio->boot = boot;
  jvsio->sendAndReceive = sendAndReceive;

  work->data = data;
  work->sense = sense;
  work->led = led;
  work->nodes = nodes ? nodes : 1;
  work->rx_size = 0;
  work->rx_read_ptr = 0;
  work->rx_receiving = false;
  work->rx_escaping = false;
  work->rx_available = false;
  work->rx_error = false;
  work->new_address = kBroadcastAddress;
  work->tx_report_size = 0;
  work->downstream_ready = false;
  for (uint8_t i = 0; i < work->nodes; ++i)
    work->address[i] = kBroadcastAddress;
  return jvsio;
}

void JVSIO_close(struct JVSIO_Lib* lib) {}
