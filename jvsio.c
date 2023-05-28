// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jvsio.h"

#include <stdlib.h>

enum {
  kResetInterval = 500,
  kResponseTimeout = 100,
};

enum State {
  kStateDisconnected,
  kStateConnected,
  kStateReset,
  kStateResetWaitInterval,
  kStateReset2,
  kStateAddress,
  kStateAddressWaitResponse,
  kStateReadyCheck,
  kStateRequestIoId,
  kStateWaitIoIdResponse,
  kStateRequestCommandRev,
  kStateWaitCommandRevResponse,
  kStateRequestJvRev,
  kStateWaitJvRevResponse,
  kStateRequestProtocolVer,
  kStateWaitProtocolVerResponse,
  kStateRequestFunctionCheck,
  kStateWaitFunctionCheckResponse,

  kStateReady,

  kStateRequestSync,
  kStateWaitSyncResponse,
  kStateWaitCoinSyncResponse,

  kStateTimeout,
  kStateInvalidResponse,
  kStateUnexpected,
};

struct JVSIO_Work {
  struct JVSIO_DataClient* data;
  struct JVSIO_SenseClient* sense;
  struct JVSIO_LedClient* led;
  struct JVSIO_TimeClient* time;
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
  enum JVSIO_CommSupMode comm_mode;
#if !defined(__NO_JVS_HOST__)
  enum State state;
  uint32_t tick;
  uint8_t devices;
  uint8_t target;
  uint8_t players[4];
  uint8_t buttons[4];
  uint8_t coin_slots[4];
  uint8_t total_player;
  uint8_t coin_state;
  uint8_t sw_state0[4];
  uint8_t sw_state1[4];
#endif
};

static struct JVSIO_Lib gLib;
static struct JVSIO_Work gWork;

enum {
  kHostAddress = 0x00,
  kBroadcastAddress = 0xFF,
  kMarker = 0xD0,
  kSync = 0xE0,
};

static void writeEscapedByte(struct JVSIO_DataClient* client, uint8_t data) {
  if (data == kMarker || data == kSync) {
    client->write(client, kMarker);
    client->write(client, data - 1);
  } else {
    client->write(client, data);
  }
}

static bool getCommandSize(struct JVSIO_Work* work,
                           uint8_t* command,
                           uint8_t len,
                           uint8_t* size) {
  switch (*command) {
    case kCmdReset:
    case kCmdAddressSet:
      *size = 2;
      return true;
    case kCmdIoId:
    case kCmdCommandRev:
    case kCmdJvRev:
    case kCmdProtocolVer:
    case kCmdFunctionCheck:
      *size = 1;
      return true;
    case kCmdMainId:
      break;  // handled later
    case kCmdSwInput:
      *size = 3;
      return true;
    case kCmdCoinInput:
    case kCmdAnalogInput:
    case kCmdRotaryInput:
      *size = 2;
      return true;
    case kCmdKeyCodeInput:
      *size = 1;
      return true;
    case kCmdScreenPositionInput:
      *size = 2;
      return true;
    case kCmdRetry:
      *size = 1;
      return true;
    case kCmdCoinSub:
    case kCmdCoinAdd:
      *size = 4;
      return true;
    case kCmdDriverOutput:
      *size = command[1] + 2;
      return true;
    case kCmdAnalogOutput:
      *size = command[1] * 2 + 2;
      return true;
    case kCmdCharacterOutput:
      *size = command[1] + 2;
      return true;
    case kCmdNamco:
      switch (command[4]) {
        case 0x02:
          *size = 7;
          return true;
        case 0x14:
          *size = 12;
          return true;
        case 0x80:
          *size = 6;
          return true;
      }
      return false;
    case kCmdCommSup:
      *size = 1;
      return true;
    case kCmdCommChg:
      *size = 2;
      return true;
    default:
      work->data->dump(work->data, "unknown command", command, 1);
      return false;
  }
  *size = 2;
  for (uint8_t i = 1; i < len && command[i]; ++i) {
    *size = *size + 1;
  }
  if (command[*size - 1]) {
    *size = 0;  // data is incomplete.
  }
  return true;
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
  // Should not reply if the rx_receiving is reset, e.g. for broadcast commands.
  if (!work->rx_receiving)
    return;

  work->rx_available = false;
  work->rx_receiving = false;
  work->tx_report_size = 0;

  // Direction should be changed within 100usec from sending/receiving a packet.
  work->data->setOutput(work->data);

  if (work->comm_mode == k115200) {
    // Spec requires 100usec interval at minimum between each packet.
    // But response should be sent within 1msec from the last byte received.
    work->time->delayMicroseconds(work->time, 100);
  }

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

static bool isBusy(struct JVSIO_Lib* lib) {
  return lib->work->tx_report_size != 0;
}

static void receive(struct JVSIO_Work* work) {
  while (work->data->available(work->data)) {
    uint8_t data = work->data->read(work->data);
    if (data == kSync) {
      work->rx_size = 0;
      work->rx_read_ptr = 2;
      work->tx_report_size = 0;
      work->rx_receiving = true;
      work->rx_escaping = false;
      work->downstream_ready = work->sense->isReady(work->sense);
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
  }
  if (work->rx_size < 2) {
    return;
  }
  if (work->rx_data[0] != kBroadcastAddress && !matchAddress(work)) {
    // Ignore packets for other nodes.
    work->rx_receiving = false;
    return;
  }
  if (work->rx_size == work->rx_read_ptr) {
    // No data.
    return;
  }
  if ((work->rx_data[1] + 1) != work->rx_read_ptr) {
    uint8_t command_size;
    if (!getCommandSize(work, &work->rx_data[work->rx_read_ptr],
                        work->rx_size - work->rx_read_ptr, &command_size)) {
      // Contain an unknown comamnd. Reply with the error status and ignore the
      // whole packet.
      work->rx_receiving = false;
      sendUnknownCommandStatus(work);
      return;
    }
    if (command_size == 0 ||
        (work->rx_read_ptr + command_size) > work->rx_size) {
      // No command is ready to process.
      return;
    }
    // At least, one command is ready to process.
    work->rx_available = true;
    return;
  }

  // Wait for the last byte, checksum.
  if ((work->rx_data[1] + 2) != work->rx_size) {
    return;
  }

  // Let's calculate the checksum.
  work->rx_available = false;
  uint8_t sum = 0;
  for (size_t i = 0; i < (work->rx_size - 1u); ++i) {
    sum += work->rx_data[i];
  }
  if (work->rx_data[work->rx_size - 1] != sum) {
    // Handles check sum error cases.
    if (work->address[0] == kHostAddress) {
      // Host mode does not need to send an error response back.
    } else if (work->rx_data[2] == kCmdReset ||
               work->rx_data[2] == kCmdCommChg) {
    } else {
      // Reply with the error and ignore commands in the packet.
      sendSumErrorStatus(work);
    }
  } else {
    // Flish status.
    sendOkStatus(work);
  }
}

static void begin(struct JVSIO_Lib* lib) {}

static uint8_t* getNextCommandInternal(struct JVSIO_Lib* lib,
                                       uint8_t* len,
                                       uint8_t* node,
                                       bool speculative) {
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
    if (!speculative && (work->rx_data[1] + 2) != work->rx_size) {
      return NULL;
    }
    uint8_t command_size;
    getCommandSize(work, &work->rx_data[work->rx_read_ptr],
                   work->rx_size - work->rx_read_ptr, &command_size);
    work->rx_available = false;
    switch (work->rx_data[work->rx_read_ptr]) {
      case kCmdReset:
        senseNotReady(work);
        for (i = 0; i < work->nodes; ++i)
          work->address[i] = kBroadcastAddress;
        work->rx_receiving = false;
        work->data->dump(work->data, "reset", NULL, 0);
        work->rx_read_ptr += command_size;
        *len = command_size;
        return &work->rx_data[work->rx_read_ptr - command_size];
      case kCmdAddressSet:
        if (work->downstream_ready) {
          work->new_address = work->rx_data[work->rx_read_ptr + 1];
          work->data->dump(work->data, "address",
                           &work->rx_data[work->rx_read_ptr + 1], 1);
          pushReport(lib, kReportOk);
        } else {
          work->rx_receiving = false;
          return NULL;
        }
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
        if (work->data->setCommSupMode &&
            (work->data->setCommSupMode(work->data, k1M, true) ||
             work->data->setCommSupMode(work->data, k3M, true))) {
          // Activate the JVS Dash high speed modes if underlying
          // implementation provides functionalities to upgrade the protocol.
          pushReport(lib, 0x20);
        } else {
          pushReport(lib, 0x10);
        }
        break;
      case kCmdMainId:
        // We may hold the Id to provide it for the client code, but let's
        // just ignore it for now. It seems newer namco boards send this
        // command, e.g. BNGI.;WinArc;Ver"2.2.4";JPN, and expects OK status to
        // proceed.
        pushReport(lib, kReportOk);
        break;
      case kCmdRetry:
        sendStatus(work);
        break;
      case kCmdCommSup:
        pushReport(lib, kReportOk);
        pushReport(
            lib,
            1 | (work->data->setCommSupMode(work->data, k1M, true) ? 2 : 0) |
                (work->data->setCommSupMode(work->data, k3M, true) ? 4 : 0));
        break;
      case kCmdCommChg:
        if (work->data->setCommSupMode(
                work->data, work->rx_data[work->rx_read_ptr + 1], false)) {
          work->comm_mode = work->rx_data[work->rx_read_ptr + 1];
        }
        work->rx_receiving = false;
        return NULL;
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

static uint8_t* getNextCommand(struct JVSIO_Lib* lib,
                               uint8_t* len,
                               uint8_t* node) {
  return getNextCommandInternal(lib, len, node, false);
}

static uint8_t* getNextSpeculativeCommand(struct JVSIO_Lib* lib,
                                          uint8_t* len,
                                          uint8_t* node) {
  return getNextCommandInternal(lib, len, node, true);
}

#if !defined(__NO_JVS_HOST__)
static bool timeInRange(uint32_t start, uint32_t now, uint32_t duration) {
  uint32_t end = start + duration;
  if (end < start) {
    // The uint32_t value wraps. So, "end < now < start" is out of the range.
    return !(end < now && now < start);
  }
  return start <= now && now <= end;
}

static uint8_t* receiveStatus(struct JVSIO_Work* work, uint8_t* len) {
  if (!timeInRange(work->tick, work->time->getTick(work->time),
                   kResponseTimeout)) {
    work->state = kStateTimeout;
    return NULL;
  }

  work->address[0] = kHostAddress;
  receive(work);
  if (work->rx_error || !work->rx_available)
    return NULL;

  *len = work->rx_data[1] - 1;
  work->rx_size = 0;
  work->rx_available = false;
  work->rx_receiving = false;
  return &work->rx_data[2];
}

static bool host(struct JVSIO_Lib* lib, struct JVSIO_HostClient* client) {
  struct JVSIO_Work* work = lib->work;
  uint8_t* status = 0;
  uint8_t status_len = 0;
  bool connected = work->sense->isConnected(work->sense);
  if (!connected)
    work->state = kStateDisconnected;

  switch (work->state) {
    case kStateDisconnected:
      if (connected) {
        work->tick = work->time->getTick(work->time);
        work->state = kStateConnected;
      }
      return false;
    case kStateConnected:
    case kStateResetWaitInterval:
      // Wait til 500[ms] to operate the RESET.
      if (timeInRange(work->tick, work->time->getTick(work->time),
                      kResetInterval)) {
        return false;
      }
      break;
    case kStateReset:
    case kStateReset2:
      work->data->dump(0, "RESET", 0, 0);
      work->tx_data[0] = kBroadcastAddress;
      work->tx_data[1] = 3;  // Bytes
      work->tx_data[2] = kCmdReset;
      work->tx_data[3] = 0xd9;  // Magic number.
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      work->devices = 0;
      work->total_player = 0;
      work->coin_state = 0;
      break;
    case kStateAddress:
      if (work->devices == 255) {
        work->state = kStateUnexpected;
        return false;
      }
      work->tx_data[0] = kBroadcastAddress;
      work->tx_data[1] = 3;  // Bytes
      work->tx_data[2] = kCmdAddressSet;
      work->tx_data[3] = ++work->devices;
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    case kStateAddressWaitResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len != 2 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      work->tick = work->time->getTick(work->time);
      break;
    case kStateReadyCheck:
      if (!work->sense->isReady(work->sense)) {
        if (!timeInRange(work->tick, work->time->getTick(work->time), 2)) {
          // More I/O devices exist. Assign for the next.
          work->state = kStateAddress;
        }
        return false;
      }
      work->target = 1;
      break;
    case kStateRequestIoId:
      work->tx_data[0] = work->target;
      work->tx_data[1] = 2;  // Bytes
      work->tx_data[2] = kCmdIoId;
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    case kStateWaitIoIdResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len < 3 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      if (client->receiveIoId)
        client->receiveIoId(client, work->target, &status[2], status_len - 2);
      break;
    case kStateRequestCommandRev:
      work->tx_data[0] = work->target;
      work->tx_data[1] = 2;  // Bytes
      work->tx_data[2] = kCmdCommandRev;
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    case kStateWaitCommandRevResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      if (client->receiveCommandRev)
        client->receiveCommandRev(client, work->target, status[2]);
      break;
    case kStateRequestJvRev:
      work->tx_data[0] = work->target;
      work->tx_data[1] = 2;  // Bytes
      work->tx_data[2] = kCmdJvRev;
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    case kStateWaitJvRevResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      if (client->receiveJvRev)
        client->receiveJvRev(client, work->target, status[2]);
      break;
    case kStateRequestProtocolVer:
      work->tx_data[0] = work->target;
      work->tx_data[1] = 2;  // Bytes
      work->tx_data[2] = kCmdProtocolVer;
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    case kStateWaitProtocolVerResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      if (client->receiveProtocolVer)
        client->receiveProtocolVer(client, work->target, status[2]);
      break;
    case kStateRequestFunctionCheck:
      work->tx_data[0] = work->target;
      work->tx_data[1] = 2;  // Bytes
      work->tx_data[2] = kCmdFunctionCheck;
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    case kStateWaitFunctionCheckResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len < 3 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      if (work->target <= 4) {
        // Doesn't support 5+ devices.
        for (uint8_t i = 2; i < status_len; i += 4) {
          switch (status[i]) {
            case 0x01:
              work->players[work->target - 1] = status[i + 1];
              work->buttons[work->target - 1] = status[i + 2];
              work->total_player += status[i + 1];
              if (work->total_player > 4)
                work->total_player = 4;
              break;
            case 0x02:
              work->coin_slots[work->target - 1] = status[i + 1];
              break;
            default:
              break;
          }
        }
      }
      if (client->receiveFunctionCheck) {
        client->receiveFunctionCheck(client, work->target, &status[2],
                                     status_len - 2);
      }
      if (work->target != work->devices) {
        work->state = kStateRequestIoId;
        work->target++;
        return false;
      }
      break;
    case kStateReady:
      return true;
    case kStateRequestSync: {
      uint8_t target_index = work->target - 1;
      work->tx_data[0] = work->target;
      work->tx_data[1] = 6;  // Bytes
      work->tx_data[2] = kCmdSwInput;
      work->tx_data[3] = work->players[target_index];
      work->tx_data[4] = (work->buttons[target_index] + 7) >> 3;
      work->tx_data[5] = kCmdCoinInput;
      work->tx_data[6] = work->coin_slots[target_index];
      work->data->setOutput(work->data);
      sendPacket(work, work->tx_data);
      work->tick = work->time->getTick(work->time);
      break;
    }
    case kStateWaitSyncResponse: {
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      uint8_t target_index = work->target - 1;
      uint8_t button_bytes = (work->buttons[target_index] + 7) >> 3;
      uint8_t sw_bytes = 1 + button_bytes * work->players[target_index];
      uint8_t coin_bytes = work->coin_slots[target_index] * 2;
      uint8_t status_bytes = 3 + sw_bytes + coin_bytes;
      if (status_len != status_bytes || status[0] != 1 || status[1] != 1 ||
          status[2 + sw_bytes] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      uint8_t player_index = 0;
      for (uint8_t i = 0; i < target_index; ++i) {
        player_index += work->players[target_index];
      }
      work->coin_state |= status[2] & 0x80;
      for (uint8_t player = 0; player < work->players[target_index]; ++player) {
        work->sw_state0[player_index + player] =
            status[3 + button_bytes * player];
        work->sw_state1[player_index + player] =
            status[4 + button_bytes * player];
      }
      for (uint8_t player = 0; player < work->coin_slots[target_index];
           ++player) {
        uint8_t mask = 1 << (player_index + player);
        if (work->coin_state & mask) {
          work->coin_state &= ~mask;
        } else {
          uint8_t index = 3 + sw_bytes + player * 2;
          uint16_t coin = (status[index] << 8) | status[index + 1];
          if (coin & 0xc000 || coin == 0)
            continue;
          work->coin_state |= mask;
          work->tx_data[0] = work->target;
          work->tx_data[1] = 5;  // Bytes
          work->tx_data[2] = kCmdCoinSub;
          work->tx_data[3] = 1 + player;
          work->tx_data[4] = 0;
          work->tx_data[5] = 1;
          work->data->setOutput(work->data);
          sendPacket(work, work->tx_data);
          work->tick = work->time->getTick(work->time);
          work->state = kStateWaitCoinSyncResponse;
          return false;
        }
      }
      if (work->target == work->devices) {
        work->state = kStateReady;
        if (client->synced) {
          client->synced(client, work->total_player, work->coin_state,
                         work->sw_state0, work->sw_state1);
        }
      } else {
        work->state = kStateRequestSync;
        work->target++;
      }
      return false;
    }
    case kStateWaitCoinSyncResponse:
      status = receiveStatus(work, &status_len);
      if (!status)
        return false;
      if (status_len != 2 || status[0] != 1 || status[1] != 1) {
        work->state = kStateInvalidResponse;
        return false;
      }
      if (work->target == work->devices) {
        work->state = kStateReady;
        if (client->synced) {
          client->synced(client, work->total_player, work->coin_state,
                         work->sw_state0, work->sw_state1);
        }
      } else {
        work->state = kStateRequestSync;
        work->target++;
      }
      return false;
    case kStateTimeout:
    case kStateInvalidResponse:
    case kStateUnexpected:
      work->state = kStateDisconnected;
      return false;
    default:
      break;
  }
  work->state++;
  return false;
}

static void sync(struct JVSIO_Lib* lib) {
  struct JVSIO_Work* work = lib->work;
  if (work->state != kStateReady)
    return;
  work->state = kStateRequestSync;
  work->target = 1;
}
#endif

struct JVSIO_Lib* JVSIO_open(struct JVSIO_DataClient* data,
                             struct JVSIO_SenseClient* sense,
                             struct JVSIO_LedClient* led,
                             struct JVSIO_TimeClient* time,
                             uint8_t nodes) {
  gLib.work = &gWork;
  struct JVSIO_Lib* jvsio = &gLib;
  struct JVSIO_Work* work = jvsio->work;

  jvsio->getNextCommand = getNextCommand;
  jvsio->getNextSpeculativeCommand = getNextSpeculativeCommand;
  jvsio->pushReport = pushReport;
  jvsio->sendUnknownStatus = sendUnknownStatus;
  jvsio->isBusy = isBusy;
#if !defined(__NO_JVS_HOST__)
  jvsio->host = host;
  jvsio->sync = sync;

  work->state = kStateDisconnected;
#endif

  work->data = data;
  work->sense = sense;
  work->led = led;
  work->time = time;
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
  work->comm_mode = k115200;
  for (uint8_t i = 0; i < work->nodes; ++i)
    work->address[i] = kBroadcastAddress;

  work->data->setInput(work->data);
  work->sense->begin(work->sense);
  work->led->begin(work->led);

  return jvsio;
}

void JVSIO_close(struct JVSIO_Lib* lib) {
  (void)lib;
}
