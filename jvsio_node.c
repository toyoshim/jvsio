// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jvsio_node.h"

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

static struct JVSIO_Work gWork;

static void writeEscapedByte(uint8_t data) {
  if (data == kMarker || data == kSync) {
    JVSIO_Client_send(kMarker);
    JVSIO_Client_send(data - 1);
  } else {
    JVSIO_Client_send(data);
  }
}

static bool getCommandSize(uint8_t* command, uint8_t len, uint8_t* size) {
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
      JVSIO_Client_dump("unknown command", command, 1);
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

static bool matchAddress() {
  uint8_t target = gWork.rx_data[0];
  for (uint8_t i = 0; i < gWork.nodes; ++i) {
    if (target == gWork.address[i])
      return true;
  }
  return false;
}

static void senseNotReady() {
  JVSIO_Client_setSense(false);
  JVSIO_Client_setLed(false);
}

static void senseReady() {
  JVSIO_Client_setSense(true);
  JVSIO_Client_setLed(true);
}

// TODO: remove data argument as it is always tx_data.
static void sendPacket(const uint8_t* data) {
  JVSIO_Client_send(kSync);
  uint8_t sum = 0;

  for (uint8_t i = 0; i <= data[1]; ++i) {
    sum += data[i];
    writeEscapedByte(data[i]);
  }
  writeEscapedByte(sum);

  JVSIO_Client_willReceive();
}

static void sendStatus() {
  // Should not reply if the rx_receiving is reset, e.g. for broadcast commands.
  if (!gWork.rx_receiving)
    return;

  gWork.rx_available = false;
  gWork.rx_receiving = false;
  gWork.tx_report_size = 0;

  // Direction should be changed within 100usec from sending/receiving a packet.
  JVSIO_Client_willSend();

  if (gWork.comm_mode == k115200) {
    // Spec requires 100usec interval at minimum between each packet.
    // But response should be sent within 1msec from the last byte received.
    JVSIO_Client_delayMicroseconds(100);
  }

  // Address is just assigned.
  // This timing to negate the sense signal is subtle. The spec expects this
  // signal should be done in 1msec. This place meets this requiment, but if
  // this action was too quick, the direct upstream client may misunderstand
  // that the last address command was for the upstream client. So, this signal
  // should be changed lately as we can as possible within the spec requiment.
  // However, as described below, sending response packet may take over 1msec.
  // Thus, this is the last place to negate the signal in a simle way.
  if (kBroadcastAddress != gWork.new_address) {
    for (uint8_t i = 0; i < gWork.nodes; ++i) {
      if (gWork.address[i] != kBroadcastAddress)
        continue;
      gWork.address[i] = gWork.new_address;
      if (i == (gWork.nodes - 1))
        senseReady();
      break;
    }
    gWork.new_address = kBroadcastAddress;
  }

  // We can send about 14 bytes per 1msec at maximum. So, it will take over 18
  // msec to send the largest packet. Actual packet will have interval time
  // between each byte. In total, it may take more time.
  sendPacket(gWork.tx_data);
}

static void sendOverflowStatus() {
  gWork.tx_data[0] = kHostAddress;
  gWork.tx_data[1] = 2;
  gWork.tx_data[2] = 0x04;
  sendStatus();
}

static void sendOkStatus() {
  if (gWork.tx_report_size > 253)
    return sendOverflowStatus();
  gWork.tx_data[0] = kHostAddress;
  gWork.tx_data[1] = 2 + gWork.tx_report_size;
  gWork.tx_data[2] = 0x01;
  sendStatus();
}

static void sendUnknownCommandStatus() {
  if (gWork.tx_report_size > 253)
    return sendOverflowStatus();
  gWork.tx_data[0] = kHostAddress;
  gWork.tx_data[1] = 2 + gWork.tx_report_size;
  gWork.tx_data[2] = 0x02;
  sendStatus();
}

static void sendSumErrorStatus() {
  gWork.tx_data[0] = kHostAddress;
  gWork.tx_data[1] = 2;
  gWork.tx_data[2] = 0x03;
  sendStatus();
}

void JVSIO_pushReport(uint8_t report) {
  if (gWork.tx_report_size == 253) {
    sendOverflowStatus();
    gWork.tx_report_size++;
  } else if (gWork.tx_report_size < 253) {
    gWork.tx_data[3 + gWork.tx_report_size] = report;
    gWork.tx_report_size++;
  }
}

void JVSIO_sendUnknownStatus() {
  return sendUnknownCommandStatus();
}

bool JVSIO_isBusy() {
  return gWork.tx_report_size != 0;
}

static void receive() {
  while (JVSIO_Client_isDataAvailable()) {
    uint8_t data = JVSIO_Client_receive();
    if (data == kSync) {
      gWork.rx_size = 0;
      gWork.rx_read_ptr = 2;
      gWork.tx_report_size = 0;
      gWork.rx_receiving = true;
      gWork.rx_escaping = false;
      gWork.downstream_ready = JVSIO_Client_isSenseReady();
      continue;
    }
    if (!gWork.rx_receiving)
      continue;
    if (data == kMarker) {
      gWork.rx_escaping = true;
      continue;
    }
    if (gWork.rx_escaping) {
      gWork.rx_data[gWork.rx_size++] = data + 1;
      gWork.rx_escaping = false;
    } else {
      gWork.rx_data[gWork.rx_size++] = data;
    }
  }
  if (gWork.rx_size < 2) {
    return;
  }
  if (gWork.rx_data[0] != kBroadcastAddress && !matchAddress()) {
    // Ignore packets for other nodes.
    gWork.rx_receiving = false;
    return;
  }
  if (gWork.rx_size == gWork.rx_read_ptr) {
    // No data.
    return;
  }
  if ((gWork.rx_data[1] + 1) != gWork.rx_read_ptr) {
    uint8_t command_size;
    if (!getCommandSize(&gWork.rx_data[gWork.rx_read_ptr],
                        gWork.rx_size - gWork.rx_read_ptr, &command_size)) {
      // Contain an unknown comamnd. Reply with the error status and ignore the
      // whole packet.
      gWork.rx_receiving = false;
      sendUnknownCommandStatus();
      return;
    }
    if (command_size == 0 ||
        (gWork.rx_read_ptr + command_size) > gWork.rx_size) {
      // No command is ready to process.
      return;
    }
    // At least, one command is ready to process.
    gWork.rx_available = true;
    return;
  }

  // Wait for the last byte, checksum.
  if ((gWork.rx_data[1] + 2) != gWork.rx_size) {
    return;
  }

  // Let's calculate the checksum.
  gWork.rx_available = false;
  uint8_t sum = 0;
  for (size_t i = 0; i < (gWork.rx_size - 1u); ++i) {
    sum += gWork.rx_data[i];
  }
  if (gWork.rx_data[gWork.rx_size - 1] != sum) {
    // Handles check sum error cases.
    if (gWork.address[0] == kHostAddress) {
      // Host mode does not need to send an error response back.
    } else if (gWork.rx_data[2] == kCmdReset ||
               gWork.rx_data[2] == kCmdCommChg) {
    } else {
      // Reply with the error and ignore commands in the packet.
      sendSumErrorStatus();
    }
  } else {
    // Flish status.
    sendOkStatus();
  }
}

static uint8_t* getNextCommandInternal(uint8_t* len,
                                       uint8_t* node,
                                       bool speculative) {
  uint8_t i;

  for (;;) {
    receive();
    if (!gWork.rx_available)
      return NULL;

    if (node != NULL) {
      *node = 255;
      for (i = 0; i < gWork.nodes; ++i) {
        if (gWork.address[i] == gWork.rx_data[0])
          *node = i;
      }
    }
    if (!speculative && (gWork.rx_data[1] + 2) != gWork.rx_size) {
      return NULL;
    }
    uint8_t command_size;
    getCommandSize(&gWork.rx_data[gWork.rx_read_ptr],
                   gWork.rx_size - gWork.rx_read_ptr, &command_size);
    gWork.rx_available = false;
    switch (gWork.rx_data[gWork.rx_read_ptr]) {
      case kCmdReset:
        senseNotReady();
        for (i = 0; i < gWork.nodes; ++i)
          gWork.address[i] = kBroadcastAddress;
        gWork.rx_receiving = false;
        JVSIO_Client_dump("reset", NULL, 0);
        gWork.rx_read_ptr += command_size;
        *len = command_size;
        return &gWork.rx_data[gWork.rx_read_ptr - command_size];
      case kCmdAddressSet:
        if (gWork.downstream_ready) {
          gWork.new_address = gWork.rx_data[gWork.rx_read_ptr + 1];
          JVSIO_Client_dump("address", &gWork.rx_data[gWork.rx_read_ptr + 1],
                            1);
          JVSIO_pushReport(kReportOk);
        } else {
          gWork.rx_receiving = false;
          return NULL;
        }
        break;
      case kCmdCommandRev:
        JVSIO_pushReport(kReportOk);
        JVSIO_pushReport(0x13);
        break;
      case kCmdJvRev:
        JVSIO_pushReport(kReportOk);
        JVSIO_pushReport(0x30);
        break;
      case kCmdProtocolVer:
        JVSIO_pushReport(kReportOk);
        if ((JVSIO_Client_setCommSupMode(k1M, true) ||
             JVSIO_Client_setCommSupMode(k3M, true))) {
          // Activate the JVS Dash high speed modes if underlying
          // implementation provides functionalities to upgrade the protocol.
          JVSIO_pushReport(0x20);
        } else {
          JVSIO_pushReport(0x10);
        }
        break;
      case kCmdMainId:
        // We may hold the Id to provide it for the client code, but let's
        // just ignore it for now. It seems newer namco boards send this
        // command, e.g. BNGI.;WinArc;Ver"2.2.4";JPN, and expects OK status to
        // proceed.
        JVSIO_pushReport(kReportOk);
        break;
      case kCmdRetry:
        sendStatus();
        break;
      case kCmdCommSup:
        JVSIO_pushReport(kReportOk);
        JVSIO_pushReport(1 | (JVSIO_Client_setCommSupMode(k1M, true) ? 2 : 0) |
                         (JVSIO_Client_setCommSupMode(k3M, true) ? 4 : 0));
        break;
      case kCmdCommChg:
        if (JVSIO_Client_setCommSupMode(gWork.rx_data[gWork.rx_read_ptr + 1],
                                        false)) {
          gWork.comm_mode = gWork.rx_data[gWork.rx_read_ptr + 1];
        }
        gWork.rx_receiving = false;
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
        gWork.rx_read_ptr += command_size;
        return &gWork.rx_data[gWork.rx_read_ptr - command_size];
      default:
        sendUnknownCommandStatus();
        break;
    }
    gWork.rx_read_ptr += command_size;
  }
}

uint8_t* JVSIO_getNextCommand(uint8_t* len, uint8_t* node) {
  return getNextCommandInternal(len, node, false);
}

uint8_t* JVSIO_getNextSpeculativeCommand(uint8_t* len, uint8_t* node) {
  return getNextCommandInternal(len, node, true);
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

static uint8_t* receiveStatus(uint8_t* len) {
  if (!timeInRange(gWork.tick, JVSIO_Client_getTick(), kResponseTimeout)) {
    gWork.state = kStateTimeout;
    return NULL;
  }

  gWork.address[0] = kHostAddress;
  receive();
  if (gWork.rx_error || !gWork.rx_available)
    return NULL;

  *len = gWork.rx_data[1] - 1;
  gWork.rx_size = 0;
  gWork.rx_available = false;
  gWork.rx_receiving = false;
  return &gWork.rx_data[2];
}

static bool host() {
  uint8_t* status = 0;
  uint8_t status_len = 0;
  bool connected = JVSIO_Client_isSenseConnected();
  if (!connected)
    gWork.state = kStateDisconnected;

  switch (gWork.state) {
    case kStateDisconnected:
      if (connected) {
        gWork.tick = JVSIO_Client_getTick();
        gWork.state = kStateConnected;
      }
      return false;
    case kStateConnected:
    case kStateResetWaitInterval:
      // Wait til 500[ms] to operate the RESET.
      if (timeInRange(gWork.tick, JVSIO_Client_getTick(), kResetInterval)) {
        return false;
      }
      break;
    case kStateReset:
    case kStateReset2:
      JVSIO_Client_dump("RESET", 0, 0);
      gWork.tx_data[0] = kBroadcastAddress;
      gWork.tx_data[1] = 3;  // Bytes
      gWork.tx_data[2] = kCmdReset;
      gWork.tx_data[3] = 0xd9;  // Magic number.
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      gWork.devices = 0;
      gWork.total_player = 0;
      gWork.coin_state = 0;
      break;
    case kStateAddress:
      if (gWork.devices == 255) {
        gWork.state = kStateUnexpected;
        return false;
      }
      gWork.tx_data[0] = kBroadcastAddress;
      gWork.tx_data[1] = 3;  // Bytes
      gWork.tx_data[2] = kCmdAddressSet;
      gWork.tx_data[3] = ++gWork.devices;
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateAddressWaitResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 2 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateReadyCheck:
      if (!JVSIO_Client_isSenseReady()) {
        if (!timeInRange(gWork.tick, JVSIO_Client_getTick(), 2)) {
          // More I/O devices exist. Assign for the next.
          gWork.state = kStateAddress;
        }
        return false;
      }
      gWork.target = 1;
      break;
    case kStateRequestIoId:
      gWork.tx_data[0] = gWork.target;
      gWork.tx_data[1] = 2;  // Bytes
      gWork.tx_data[2] = kCmdIoId;
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateWaitIoIdResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len < 3 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_ioIdReceived(gWork.target, &status[2], status_len - 2);
      break;
    case kStateRequestCommandRev:
      gWork.tx_data[0] = gWork.target;
      gWork.tx_data[1] = 2;  // Bytes
      gWork.tx_data[2] = kCmdCommandRev;
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateWaitCommandRevResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_commandRevReceived(gWork.target, status[2]);
      break;
    case kStateRequestJvRev:
      gWork.tx_data[0] = gWork.target;
      gWork.tx_data[1] = 2;  // Bytes
      gWork.tx_data[2] = kCmdJvRev;
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateWaitJvRevResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_jvRevReceived(gWork.target, status[2]);
      break;
    case kStateRequestProtocolVer:
      gWork.tx_data[0] = gWork.target;
      gWork.tx_data[1] = 2;  // Bytes
      gWork.tx_data[2] = kCmdProtocolVer;
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateWaitProtocolVerResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_protocolVerReceived(gWork.target, status[2]);
      break;
    case kStateRequestFunctionCheck:
      gWork.tx_data[0] = gWork.target;
      gWork.tx_data[1] = 2;  // Bytes
      gWork.tx_data[2] = kCmdFunctionCheck;
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    case kStateWaitFunctionCheckResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len < 3 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      if (gWork.target <= 4) {
        // Doesn't support 5+ devices.
        for (uint8_t i = 2; i < status_len; i += 4) {
          switch (status[i]) {
            case 0x01:
              gWork.players[gWork.target - 1] = status[i + 1];
              gWork.buttons[gWork.target - 1] = status[i + 2];
              gWork.total_player += status[i + 1];
              if (gWork.total_player > 4)
                gWork.total_player = 4;
              break;
            case 0x02:
              gWork.coin_slots[gWork.target - 1] = status[i + 1];
              break;
            default:
              break;
          }
        }
      }
      JVSIO_Client_functionCheckReceived(gWork.target, &status[2],
                                         status_len - 2);
      if (gWork.target != gWork.devices) {
        gWork.state = kStateRequestIoId;
        gWork.target++;
        return false;
      }
      break;
    case kStateReady:
      return true;
    case kStateRequestSync: {
      uint8_t target_index = gWork.target - 1;
      gWork.tx_data[0] = gWork.target;
      gWork.tx_data[1] = 6;  // Bytes
      gWork.tx_data[2] = kCmdSwInput;
      gWork.tx_data[3] = gWork.players[target_index];
      gWork.tx_data[4] = (gWork.buttons[target_index] + 7) >> 3;
      gWork.tx_data[5] = kCmdCoinInput;
      gWork.tx_data[6] = gWork.coin_slots[target_index];
      JVSIO_Client_willSend();
      sendPacket(gWork.tx_data);
      gWork.tick = JVSIO_Client_getTick();
      break;
    }
    case kStateWaitSyncResponse: {
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      uint8_t target_index = gWork.target - 1;
      uint8_t button_bytes = (gWork.buttons[target_index] + 7) >> 3;
      uint8_t sw_bytes = 1 + button_bytes * gWork.players[target_index];
      uint8_t coin_bytes = gWork.coin_slots[target_index] * 2;
      uint8_t status_bytes = 3 + sw_bytes + coin_bytes;
      if (status_len != status_bytes || status[0] != 1 || status[1] != 1 ||
          status[2 + sw_bytes] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      uint8_t player_index = 0;
      for (uint8_t i = 0; i < target_index; ++i) {
        player_index += gWork.players[target_index];
      }
      gWork.coin_state |= status[2] & 0x80;
      for (uint8_t player = 0; player < gWork.players[target_index]; ++player) {
        gWork.sw_state0[player_index + player] =
            status[3 + button_bytes * player];
        gWork.sw_state1[player_index + player] =
            status[4 + button_bytes * player];
      }
      for (uint8_t player = 0; player < gWork.coin_slots[target_index];
           ++player) {
        uint8_t mask = 1 << (player_index + player);
        if (gWork.coin_state & mask) {
          gWork.coin_state &= ~mask;
        } else {
          uint8_t index = 3 + sw_bytes + player * 2;
          uint16_t coin = (status[index] << 8) | status[index + 1];
          if (coin & 0xc000 || coin == 0)
            continue;
          gWork.coin_state |= mask;
          gWork.tx_data[0] = gWork.target;
          gWork.tx_data[1] = 5;  // Bytes
          gWork.tx_data[2] = kCmdCoinSub;
          gWork.tx_data[3] = 1 + player;
          gWork.tx_data[4] = 0;
          gWork.tx_data[5] = 1;
          JVSIO_Client_willSend();
          sendPacket(gWork.tx_data);
          gWork.tick = JVSIO_Client_getTick();
          gWork.state = kStateWaitCoinSyncResponse;
          return false;
        }
      }
      if (gWork.target == gWork.devices) {
        gWork.state = kStateReady;
        JVSIO_Client_synced(gWork.total_player, gWork.coin_state,
                            gWork.sw_state0, gWork.sw_state1);
      } else {
        gWork.state = kStateRequestSync;
        gWork.target++;
      }
      return false;
    }
    case kStateWaitCoinSyncResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 2 || status[0] != 1 || status[1] != 1) {
        gWork.state = kStateInvalidResponse;
        return false;
      }
      if (gWork.target == gWork.devices) {
        gWork.state = kStateReady;
        JVSIO_Client_synced(gWork.total_player, gWork.coin_state,
                            gWork.sw_state0, gWork.sw_state1);
      } else {
        gWork.state = kStateRequestSync;
        gWork.target++;
      }
      return false;
    case kStateTimeout:
    case kStateInvalidResponse:
    case kStateUnexpected:
      gWork.state = kStateDisconnected;
      return false;
    default:
      break;
  }
  gWork.state++;
  return false;
}

static void sync() {
  if (gWork.state != kStateReady)
    return;
  gWork.state = kStateRequestSync;
  gWork.target = 1;
}
#endif

void JVSIO_init(uint8_t nodes) {
#if !defined(__NO_JVS_HOST__)
  gWork.state = kStateDisconnected;
#endif

  gWork.nodes = nodes ? nodes : 1;
  gWork.rx_size = 0;
  gWork.rx_read_ptr = 0;
  gWork.rx_receiving = false;
  gWork.rx_escaping = false;
  gWork.rx_available = false;
  gWork.rx_error = false;
  gWork.new_address = kBroadcastAddress;
  gWork.tx_report_size = 0;
  gWork.downstream_ready = false;
  gWork.comm_mode = k115200;
  for (uint8_t i = 0; i < gWork.nodes; ++i)
    gWork.address[i] = kBroadcastAddress;

  JVSIO_Client_willReceive();
}