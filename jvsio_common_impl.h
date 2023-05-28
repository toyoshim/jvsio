// Copyright 2023 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jvsio_common.h"

#include <stdint.h>
#include <stdlib.h>

#include "jvsio_client.h"

static uint8_t tx_data[256];
static uint8_t tx_report_size;

static uint8_t rx_data[256];
static uint8_t rx_size;
static uint8_t rx_read_ptr;
static bool rx_receiving;
static bool rx_escaping;
static bool rx_available;
static bool rx_error;

static uint8_t nodes;
static uint8_t address[2];
static bool downstream_ready;

static bool matchAddress() {
  uint8_t target = rx_data[0];
  for (uint8_t i = 0; i < nodes; ++i) {
    if (target == address[i]) {
      return true;
    }
  }
  return false;
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

static void writeEscapedByte(uint8_t data) {
  if (data == kMarker || data == kSync) {
    JVSIO_Client_send(kMarker);
    JVSIO_Client_send(data - 1);
  } else {
    JVSIO_Client_send(data);
  }
}

static void sendPacket() {
  JVSIO_Client_send(kSync);
  uint8_t sum = 0;

  for (uint8_t i = 0; i <= tx_data[1]; ++i) {
    sum += tx_data[i];
    writeEscapedByte(tx_data[i]);
  }
  writeEscapedByte(sum);

  JVSIO_Client_willReceive();
}

static void pushOverflowStatus() {
  tx_data[0] = kHostAddress;
  tx_data[1] = 2;
  tx_data[2] = 0x04;
}

static void pushUnknownCommandStatus() {
  if (tx_report_size > 253) {
    return pushOverflowStatus();
  }
  tx_data[0] = kHostAddress;
  tx_data[1] = 2 + tx_report_size;
  tx_data[2] = 0x02;
}

// If `speculative` is true, `rx_available` is set to true when a command is
// ready to process spculatively. If `rx_receiving` is still true, the packet
// isn't verified yet. `rx_receiving` is set to false for the last command.
// Caller should set `rx_available` to false after processing the command.
static void receive(bool speculative) {
  while (JVSIO_Client_isDataAvailable()) {
    uint8_t data = JVSIO_Client_receive();
    if (data == kSync) {
      rx_size = 0;
      rx_read_ptr = 2;
      rx_receiving = true;
      rx_available = false;
      rx_escaping = false;
      rx_error = false;
      tx_report_size = 0;
      downstream_ready = JVSIO_Client_isSenseReady();
      continue;
    }
    if (!rx_receiving) {
      continue;
    }
    if (data == kMarker) {
      rx_escaping = true;
      continue;
    }
    if (rx_escaping) {
      rx_data[rx_size++] = data + 1;
      rx_escaping = false;
    } else {
      rx_data[rx_size++] = data;
    }
  }
  if (!rx_receiving) {
    return;
  }
  if (rx_size < 2) {
    return;
  }
  if (rx_data[0] != kBroadcastAddress && !matchAddress()) {
    // Ignore packets for other nodes.
    rx_receiving = false;
    return;
  }
  if (rx_size == rx_read_ptr) {
    // No data.
    return;
  }
  if (speculative && (rx_data[1] + 2) != rx_size) {
    // Speculatively handle receiving commands.
    uint8_t command_size;
    if (!getCommandSize(&rx_data[rx_read_ptr], rx_size - rx_read_ptr,
                        &command_size)) {
      // Contain an unknown comamnd. Reply with the error status and ignore the
      // whole packet.
      rx_receiving = false;
      pushUnknownCommandStatus();
      return;
    }
    if (command_size == 0 || (rx_read_ptr + command_size) > rx_size) {
      // No command is ready to process.
      return;
    }
    if ((rx_data[1] + 1) == (rx_read_ptr + command_size)) {
      // The last command needs a checksum verification. Do nothing until the
      // last byte is received.
      return;
    }
    // At least, one command is ready to process.
    rx_available = true;
    return;
  }

  // Wait for the last byte, checksum.
  if ((rx_data[1] + 2) != rx_size) {
    return;
  }

  // Let's calculate the checksum.
  rx_receiving = false;
  rx_available = true;
  uint8_t sum = 0;
  for (size_t i = 0; i < (rx_size - 1u); ++i) {
    sum += rx_data[i];
  }
  if (rx_data[rx_size - 1] != sum) {
    // Handles check sum error cases.
    if (address[0] == kHostAddress) {
      // Host mode does not need to send an error response back.
    } else if (rx_data[2] == kCmdReset || rx_data[2] == kCmdCommChg) {
      // These commands don't need a response.
    } else {
      // Reply with the error and ignore commands in the packet.
      rx_error = true;
    }
  }
}
