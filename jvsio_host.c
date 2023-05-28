// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jvsio_host.h"

#include <stdint.h>
#include <stdlib.h>

#include "jvsio_client.h"
#include "jvsio_common_impl.h"

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

static enum State state;
static uint32_t tick;
static uint8_t devices;
static uint8_t target;
static uint8_t players[4];
static uint8_t buttons[4];
static uint8_t coin_slots[4];
static uint8_t total_player;
static uint8_t coin_state;
static uint8_t sw_state0[4];
static uint8_t sw_state1[4];

static bool timeInRange(uint32_t start, uint32_t now, uint32_t duration) {
  uint32_t end = start + duration;
  if (end < start) {
    // The uint32_t value wraps. So, "end < now < start" is out of the range.
    return !(end < now && now < start);
  }
  return start <= now && now <= end;
}

static uint8_t* receiveStatus(uint8_t* len) {
  if (!timeInRange(tick, JVSIO_Client_getTick(), kResponseTimeout)) {
    state = kStateTimeout;
    return NULL;
  }

  address[0] = kHostAddress;
  receive(false);
  if (rx_error || !rx_available)
    return NULL;

  *len = rx_data[1] - 1;
  rx_size = 0;
  rx_available = false;
  rx_receiving = false;
  return &rx_data[2];
}

void JVSIO_Host_init() {
  state = kStateDisconnected;
  rx_size = 0;
  rx_read_ptr = 0;
  rx_receiving = false;
  rx_escaping = false;
  rx_available = false;
  rx_error = false;
  tx_report_size = 0;
  nodes = 1;
  address[0] = kBroadcastAddress;

  JVSIO_Client_willReceive();
}

bool JVSIO_Host_run() {
  uint8_t* status = 0;
  uint8_t status_len = 0;
  bool connected = JVSIO_Client_isSenseConnected();
  if (!connected)
    state = kStateDisconnected;

  switch (state) {
    case kStateDisconnected:
      if (connected) {
        tick = JVSIO_Client_getTick();
        state = kStateConnected;
      }
      return false;
    case kStateConnected:
    case kStateResetWaitInterval:
      // Wait til 500[ms] to operate the RESET.
      if (timeInRange(tick, JVSIO_Client_getTick(), kResetInterval)) {
        return false;
      }
      break;
    case kStateReset:
    case kStateReset2:
      JVSIO_Client_dump("RESET", 0, 0);
      tx_data[0] = kBroadcastAddress;
      tx_data[1] = 3;  // Bytes
      tx_data[2] = kCmdReset;
      tx_data[3] = 0xd9;  // Magic number.
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      devices = 0;
      total_player = 0;
      coin_state = 0;
      break;
    case kStateAddress:
      if (devices == 255) {
        state = kStateUnexpected;
        return false;
      }
      tx_data[0] = kBroadcastAddress;
      tx_data[1] = 3;  // Bytes
      tx_data[2] = kCmdAddressSet;
      tx_data[3] = ++devices;
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    case kStateAddressWaitResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 2 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      tick = JVSIO_Client_getTick();
      break;
    case kStateReadyCheck:
      if (!JVSIO_Client_isSenseReady()) {
        if (!timeInRange(tick, JVSIO_Client_getTick(), 2)) {
          // More I/O devices exist. Assign for the next.
          state = kStateAddress;
        }
        return false;
      }
      target = 1;
      break;
    case kStateRequestIoId:
      tx_data[0] = target;
      tx_data[1] = 2;  // Bytes
      tx_data[2] = kCmdIoId;
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    case kStateWaitIoIdResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len < 3 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_ioIdReceived(target, &status[2], status_len - 2);
      break;
    case kStateRequestCommandRev:
      tx_data[0] = target;
      tx_data[1] = 2;  // Bytes
      tx_data[2] = kCmdCommandRev;
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    case kStateWaitCommandRevResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_commandRevReceived(target, status[2]);
      break;
    case kStateRequestJvRev:
      tx_data[0] = target;
      tx_data[1] = 2;  // Bytes
      tx_data[2] = kCmdJvRev;
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    case kStateWaitJvRevResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_jvRevReceived(target, status[2]);
      break;
    case kStateRequestProtocolVer:
      tx_data[0] = target;
      tx_data[1] = 2;  // Bytes
      tx_data[2] = kCmdProtocolVer;
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    case kStateWaitProtocolVerResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 3 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      JVSIO_Client_protocolVerReceived(target, status[2]);
      break;
    case kStateRequestFunctionCheck:
      tx_data[0] = target;
      tx_data[1] = 2;  // Bytes
      tx_data[2] = kCmdFunctionCheck;
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    case kStateWaitFunctionCheckResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len < 3 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      if (target <= 4) {
        // Doesn't support 5+ devices.
        for (uint8_t i = 2; i < status_len; i += 4) {
          switch (status[i]) {
            case 0x01:
              players[target - 1] = status[i + 1];
              buttons[target - 1] = status[i + 2];
              total_player += status[i + 1];
              if (total_player > 4)
                total_player = 4;
              break;
            case 0x02:
              coin_slots[target - 1] = status[i + 1];
              break;
            default:
              break;
          }
        }
      }
      JVSIO_Client_functionCheckReceived(target, &status[2], status_len - 2);
      if (target != devices) {
        state = kStateRequestIoId;
        target++;
        return false;
      }
      break;
    case kStateReady:
      return true;
    case kStateRequestSync: {
      uint8_t target_index = target - 1;
      tx_data[0] = target;
      tx_data[1] = 6;  // Bytes
      tx_data[2] = kCmdSwInput;
      tx_data[3] = players[target_index];
      tx_data[4] = (buttons[target_index] + 7) >> 3;
      tx_data[5] = kCmdCoinInput;
      tx_data[6] = coin_slots[target_index];
      JVSIO_Client_willSend();
      sendPacket();
      tick = JVSIO_Client_getTick();
      break;
    }
    case kStateWaitSyncResponse: {
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      uint8_t target_index = target - 1;
      uint8_t button_bytes = (buttons[target_index] + 7) >> 3;
      uint8_t sw_bytes = 1 + button_bytes * players[target_index];
      uint8_t coin_bytes = coin_slots[target_index] * 2;
      uint8_t status_bytes = 3 + sw_bytes + coin_bytes;
      if (status_len != status_bytes || status[0] != 1 || status[1] != 1 ||
          status[2 + sw_bytes] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      uint8_t player_index = 0;
      for (uint8_t i = 0; i < target_index; ++i) {
        player_index += players[target_index];
      }
      coin_state |= status[2] & 0x80;
      for (uint8_t player = 0; player < players[target_index]; ++player) {
        sw_state0[player_index + player] = status[3 + button_bytes * player];
        sw_state1[player_index + player] = status[4 + button_bytes * player];
      }
      for (uint8_t player = 0; player < coin_slots[target_index]; ++player) {
        uint8_t mask = 1 << (player_index + player);
        if (coin_state & mask) {
          coin_state &= ~mask;
        } else {
          uint8_t index = 3 + sw_bytes + player * 2;
          uint16_t coin = (status[index] << 8) | status[index + 1];
          if (coin & 0xc000 || coin == 0)
            continue;
          coin_state |= mask;
          tx_data[0] = target;
          tx_data[1] = 5;  // Bytes
          tx_data[2] = kCmdCoinSub;
          tx_data[3] = 1 + player;
          tx_data[4] = 0;
          tx_data[5] = 1;
          JVSIO_Client_willSend();
          sendPacket();
          tick = JVSIO_Client_getTick();
          state = kStateWaitCoinSyncResponse;
          return false;
        }
      }
      if (target == devices) {
        state = kStateReady;
        JVSIO_Client_synced(total_player, coin_state, sw_state0, sw_state1);
      } else {
        state = kStateRequestSync;
        target++;
      }
      return false;
    }
    case kStateWaitCoinSyncResponse:
      status = receiveStatus(&status_len);
      if (!status)
        return false;
      if (status_len != 2 || status[0] != 1 || status[1] != 1) {
        state = kStateInvalidResponse;
        return false;
      }
      if (target == devices) {
        state = kStateReady;
        JVSIO_Client_synced(total_player, coin_state, sw_state0, sw_state1);
      } else {
        state = kStateRequestSync;
        target++;
      }
      return false;
    case kStateTimeout:
    case kStateInvalidResponse:
    case kStateUnexpected:
      state = kStateDisconnected;
      return false;
    default:
      break;
  }
  state++;
  return false;
}

void JVSIO_Host_sync() {
  if (state != kStateReady)
    return;
  state = kStateRequestSync;
  target = 1;
}
