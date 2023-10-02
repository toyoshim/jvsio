// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jvsio_node.h"

#include <stdlib.h>

#include "jvsio_client.h"
#include "jvsio_common_impl.h"

static uint8_t new_address;
static enum JVSIO_CommSupMode comm_mode;

static void senseNotReady(void) {
  JVSIO_Client_setSense(false);
  JVSIO_Client_setLed(false);
}

static void senseReady(void) {
  JVSIO_Client_setSense(true);
  JVSIO_Client_setLed(true);
}

static void sendStatus(void) {
  // Should not reply if the rx_receiving is reset, e.g. for broadcast commands.
  if (rx_receiving) {
    return;
  }

  tx_report_size = 0;

  // Direction should be changed within 100usec from sending/receiving a packet.
  JVSIO_Client_willSend();

  if (comm_mode == k115200) {
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
  if (kBroadcastAddress != new_address) {
    for (uint8_t i = 0; i < nodes; ++i) {
      if (address[i] != kBroadcastAddress) {
        continue;
      }
      address[i] = new_address;
      if (i == (nodes - 1)) {
        senseReady();
      }
      break;
    }
    new_address = kBroadcastAddress;
  }

  // We can send about 14 bytes per 1msec at maximum. So, it will take over 18
  // msec to send the largest packet. Actual packet will have interval time
  // between each byte. In total, it may take more time.
  sendPacket();
}

static void sendOkStatus(void) {
  if (tx_report_size > 253) {
    pushOverflowStatus();
  } else {
    tx_data[0] = kHostAddress;
    tx_data[1] = 2 + tx_report_size;
    tx_data[2] = 0x01;
  }
  sendStatus();
}

static void sendSumErrorStatus(void) {
  tx_data[0] = kHostAddress;
  tx_data[1] = 2;
  tx_data[2] = 0x03;
  sendStatus();
}

static uint8_t getReceivingNode(void) {
  uint8_t node = kBroadcastAddress;
  for (uint8_t i = 0; i < nodes; ++i) {
    if (address[i] == rx_data[0]) {
      node = i;
    }
  }
  return node;
}

static bool receiveCommand(uint8_t node,
                           uint8_t* command,
                           uint8_t len,
                           bool commit) {
  switch (command[0]) {
    case kCmdReset:
      senseNotReady();
      for (uint8_t i = 0; i < nodes; ++i) {
        address[i] = kBroadcastAddress;
      }
      rx_receiving = false;
      JVSIO_Client_dump("reset", NULL, 0);
      JVSIO_Client_receiveCommand(node, command, len, commit);
      break;
    case kCmdAddressSet:
      if (downstream_ready) {
        new_address = command[1];
        JVSIO_Client_dump("address", &command[1], 1);
        JVSIO_Node_pushReport(kReportOk);
      } else {
        rx_receiving = false;
      }
      break;
    case kCmdCommandRev:
      JVSIO_Node_pushReport(kReportOk);
      JVSIO_Node_pushReport(0x13);
      break;
    case kCmdJvRev:
      JVSIO_Node_pushReport(kReportOk);
      JVSIO_Node_pushReport(0x30);
      break;
    case kCmdProtocolVer:
      JVSIO_Node_pushReport(kReportOk);
      if ((JVSIO_Client_setCommSupMode(k1M, true) ||
           JVSIO_Client_setCommSupMode(k3M, true))) {
        // Activate the JVS Dash high speed modes if underlying
        // implementation provides functionalities to upgrade the protocol.
        JVSIO_Node_pushReport(0x20);
      } else {
        JVSIO_Node_pushReport(0x10);
      }
      break;
    case kCmdMainId:
      // We may hold the Id to provide it for the client code, but let's
      // just ignore it for now. It seems newer namco boards send this
      // command, e.g. BNGI.;WinArc;Ver"2.2.4";JPN, and expects OK status to
      // proceed.
      JVSIO_Node_pushReport(kReportOk);
      break;
    case kCmdRetry:
      break;
    case kCmdCommSup:
      JVSIO_Node_pushReport(kReportOk);
      JVSIO_Node_pushReport(1 |
                            (JVSIO_Client_setCommSupMode(k1M, true) ? 2 : 0) |
                            (JVSIO_Client_setCommSupMode(k3M, true) ? 4 : 0));
      break;
    case kCmdCommChg:
      if (JVSIO_Client_setCommSupMode(rx_data[rx_read_ptr + 1], false)) {
        comm_mode = rx_data[rx_read_ptr + 1];
      }
      break;
    default:
      return JVSIO_Client_receiveCommand(node, command, len, commit);
  }
  return true;
}

void JVSIO_Node_pushReport(uint8_t report) {
  if (tx_report_size < 253) {
    tx_data[3 + tx_report_size] = report;
    tx_report_size++;
  }
}

bool JVSIO_Node_isBusy(void) {
  return rx_receiving;
}

void JVSIO_Node_run(bool speculative) {
  if (speculative) {
    for (;;) {
      receive(true);
      if (!rx_available) {
        return;
      }
      rx_available = false;
      uint8_t node = getReceivingNode();
      if (rx_error) {
        JVSIO_Client_receiveCommand(node, NULL, 0, true);
        rx_receiving = false;
        sendSumErrorStatus();
        return;
      }
      if (rx_receiving) {
        uint8_t cmd = rx_data[rx_read_ptr];
        if (cmd == kCmdReset || cmd == kCmdAddressSet || cmd == kCmdCommChg) {
          // These commands above should not be handled without verification.
          return;
        }
      }
      uint8_t len;
      bool known =
          getCommandSize(&rx_data[rx_read_ptr], rx_size - rx_read_ptr, &len);
      if (!known ||
          !receiveCommand(node, &rx_data[rx_read_ptr], len, !rx_receiving)) {
        while (!rx_available) {
          receive(false);
        }
        if (rx_error) {
          sendSumErrorStatus();
          return;
        }
        pushUnknownCommandStatus();
        sendStatus();
        return;
      }
      rx_read_ptr += len;
      if (!rx_receiving) {
        sendOkStatus();
      }
    }
  } else {
    receive(false);
    if (!rx_available) {
      return;
    }
    rx_available = false;
    if (rx_error) {
      sendSumErrorStatus();
      return;
    }
    uint8_t node = getReceivingNode();
    for (uint8_t len; rx_read_ptr < (rx_size - 1); rx_read_ptr += len) {
      if (!getCommandSize(&rx_data[rx_read_ptr], rx_size - rx_read_ptr, &len) ||
          !receiveCommand(node, &rx_data[rx_read_ptr], len, true)) {
        pushUnknownCommandStatus();
        sendStatus();
        return;
      }
    }
    sendOkStatus();
  }
}

void JVSIO_Node_init(uint8_t given_nodes) {
  nodes = given_nodes ? given_nodes : 1;
  rx_size = 0;
  rx_read_ptr = 0;
  rx_receiving = false;
  rx_escaping = false;
  rx_available = false;
  rx_error = false;
  new_address = kBroadcastAddress;
  tx_report_size = 0;
  downstream_ready = false;
  comm_mode = k115200;
  for (uint8_t i = 0; i < nodes; ++i)
    address[i] = kBroadcastAddress;

  JVSIO_Client_willReceive();
}