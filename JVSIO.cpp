// Copyright 2018 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "JVSIO.h"

#include "Arduino.h"

namespace {

constexpr uint8_t kHostAddress = 0x00;
constexpr uint8_t kBroadcastAddress = 0xFF;
constexpr uint8_t kMarker = 0xD0;
constexpr uint8_t kSync = 0xE0;

void dump(const char* str, uint8_t* data, size_t len) {
  // TODO : do Serial.begin();
  // for Arduino series which have native USB CDC (=Serial),
  //   such as Leonardo, ProMicro, etc. 
  Serial.print(str);
  Serial.print(": ");
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 16)
      Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println("");
}

void writeEscapedByte(JVSIO::DataClient* client, uint8_t data) {
  if (data == kMarker || data == kSync) {
    client->write(kMarker);
    client->write(data - 1);
  } else {
    client->write(data);
  }
}

uint8_t getCommandSize(uint8_t* command, uint8_t len) {
  switch(*command) {
   case JVSIO::kCmdReset:
   case JVSIO::kCmdAddressSet:
    return 2;
   case JVSIO::kCmdIoId:
   case JVSIO::kCmdCommandRev:
   case JVSIO::kCmdJvRev:
   case JVSIO::kCmdProtocolVer:
   case JVSIO::kCmdFunctionCheck:
    return 1;
   case JVSIO::kCmdMainId:
    break;  // handled later
   case JVSIO::kCmdSwInput:
    return 3;
   case JVSIO::kCmdCoinInput:
   case JVSIO::kCmdAnalogInput:
    return 2;
   case JVSIO::kCmdRetry:
    return 1;
   case JVSIO::kCmdCoinSub:
   case JVSIO::kCmdCoinAdd:
    return 4;
   case JVSIO::kCmdDriverOutput:
    return command[1] + 2;
   default:
    dump("unknown command", command, 1);
    return 0;  // not supported
  }
  uint8_t size = 2;
  for (uint8_t i = 1; i < len && command[i]; ++i)
    size++;
  return size;
}

bool matchAddress(uint8_t target, uint8_t* address, uint8_t nodes) {
  for (uint8_t i = 0; i < nodes; ++i) {
    if (target == address[i])
      return true;
  }
  return false;
}

}  // namespace

JVSIO::JVSIO(DataClient *data, SenseClient *sense, LedClient *led,
             uint8_t nodes)
    : data_(data), sense_(sense), led_(led), nodes_(nodes), rx_size_(0),
      rx_read_ptr_(0), rx_receiving_(false), rx_escaping_(false),
      rx_available_(false), rx_error_(false), new_address_(kBroadcastAddress),
      tx_report_size_(0), downstream_ready_(false) {
  for (uint8_t i = 0; i < nodes_; ++i)
    address_[i] = kBroadcastAddress;
}

JVSIO::~JVSIO() {}

void JVSIO::begin() {
  data_->setMode(INPUT);
  sense_->begin();
}

void JVSIO::end() {
}

uint8_t* JVSIO::getNextCommand(uint8_t* len, uint8_t* node) {
  for (;;) {
    receive();
    if (!rx_available_)
      return nullptr;

    if (node != nullptr) {
      *node = 255;
      for (uint8_t i = 0; i < nodes_; ++i) {
        if (address_[i] == rx_data_[0])
          *node = i;
      }
    }
    uint8_t max_command_size = rx_data_[1] - rx_read_ptr_ + 1;
    if (!max_command_size) {
      sendOkStatus();
      continue;
    }
    uint8_t command_size =
        getCommandSize(&rx_data_[rx_read_ptr_], max_command_size);
    if (!command_size) {
      sendUnknownCommandStatus();
      continue;
    }
    if (command_size > max_command_size) {
      pushReport(JVSIO::kReportParamErrorNoResponse);
      sendUnknownCommandStatus();
      continue;
    }
    switch (rx_data_[rx_read_ptr_]) {
     case JVSIO::kCmdReset:
      senseNotReady();
      for (uint8_t i = 0; i < nodes_; ++i)
        address_[i] = kBroadcastAddress;
      rx_available_ = false;
      rx_receiving_ = false;
      dump("reset", nullptr, 0);
      rx_read_ptr_ += command_size;
      return &rx_data_[rx_read_ptr_ - command_size];
     case JVSIO::kCmdAddressSet:
      if (downstream_ready_) {
        new_address_ = rx_data_[rx_read_ptr_ + 1];
        dump("address", &rx_data_[rx_read_ptr_ + 1], 1);
      }
      pushReport(JVSIO::kReportOk);
      break;
     case JVSIO::kCmdCommandRev:
      pushReport(JVSIO::kReportOk);
      pushReport(0x13);
      break;
     case JVSIO::kCmdJvRev:
      pushReport(JVSIO::kReportOk);
      pushReport(0x30);
      break;
     case JVSIO::kCmdProtocolVer:
      pushReport(JVSIO::kReportOk);
      pushReport(0x10);
      break;
     case JVSIO::kCmdMainId:
      // TODO
      dump("ignore kCmdMainId", nullptr, 0);
      sendUnknownCommandStatus();
      break;
     case JVSIO::kCmdRetry:
      sendStatus();
      break;
     case JVSIO::kCmdIoId:
     case JVSIO::kCmdFunctionCheck:
     case JVSIO::kCmdSwInput:
     case JVSIO::kCmdCoinInput:
     case JVSIO::kCmdAnalogInput:
     case JVSIO::kCmdCoinSub:
     case JVSIO::kCmdDriverOutput:
     case JVSIO::kCmdCoinAdd:
      *len = command_size;
      rx_read_ptr_ += command_size;
      return &rx_data_[rx_read_ptr_ - command_size];
     default:
      sendUnknownCommandStatus();
      break;
    }
    rx_read_ptr_ += command_size;
  }
}

void JVSIO::pushReport(uint8_t report) {
  if (tx_report_size_ == 253) {
    sendOverflowStatus();
    tx_report_size_++;
  } else if (tx_report_size_ < 253) {
    tx_data_[3 + tx_report_size_] = report;
    tx_report_size_++;
  }
}

void JVSIO::boot() {
  address_[0] = kHostAddress;

  // Spec requires to wait for 2 seconds before starting any host operation.
  delay(2000);

  for (;;) {
    while (!sense_->is_connected());

    // Reset x2
    tx_data_[0] = kBroadcastAddress;
    tx_data_[1] = 3;  // Bytes
    tx_data_[2] = kCmdReset;
    tx_data_[3] = 0xd9;  // Magic number.
    data_->setMode(OUTPUT);
    sendPacket(tx_data_);
    delayMicroseconds(100);
    data_->setMode(OUTPUT);
    sendPacket(tx_data_);
    delayMicroseconds(100);

    // Set address.
    tx_data_[0] = kBroadcastAddress;
    tx_data_[1] = 3;  // Bytes
    tx_data_[2] = kCmdAddressSet;
    tx_data_[3] = 1;  // Address
    uint8_t* ack;
    uint8_t ack_len;
    if (!sendAndReceive(tx_data_, &ack, &ack_len))
      continue;
    if (ack_len != 2)
      continue;
    if (ack[0] != 1 || ack[1] != 1)
      continue;
    delayMicroseconds(1000);
    if (!sense_->is_ready())
      continue;
    break;
  }
}

bool JVSIO::sendAndReceive(const uint8_t* packet, uint8_t** ack, uint8_t* ack_len) {
  data_->setMode(OUTPUT);
  sendPacket(packet);
  *ack = receiveStatus(ack_len);
  return *ack != nullptr;
}


void JVSIO::senseNotReady() {
  sense_->set(false);
  led_->set(false);
}

void JVSIO::senseReady() {
  sense_->set(true);
  led_->set(true);
}

void JVSIO::receive() {
  while (!rx_available_ && data_->available() > 0) {
    uint8_t data = data_->read();
    if (data == kSync) {
      rx_size_ = 0;
      rx_receiving_ = true;
      rx_escaping_ = false;
      rx_error_ = false;
      downstream_ready_ = sense_->is_ready();
      continue;
    }
    if (!rx_receiving_)
      continue;
    if (data == kMarker) {
      rx_escaping_ = true;
      continue;
    }
    if (rx_escaping_) {
      rx_data_[rx_size_++] = data + 1;
      rx_escaping_ = false;
    } else {
      rx_data_[rx_size_++] = data;
    }
    if (rx_size_ >= 2 && ((rx_data_[1] + 2) == rx_size_)) {
      uint8_t sum = 0;
      for (size_t i = 0; i < (rx_size_ - 1u); ++i)
        sum += rx_data_[i];
      if ((rx_data_[0] == kBroadcastAddress &&
           rx_data_[2] == JVSIO::kCmdReset) ||
          matchAddress(rx_data_[0], address_, nodes_)) {
        // Broadcasrt or for this device.
        if (rx_data_[rx_size_ - 1] != sum) {
          if (address_[0] == kHostAddress) {
            // Host mode does not need to send an error response back.
            // Set `rx_error_` flag instead for later references.
            rx_error_ = true;
          } else {
            sendSumErrorStatus();
            rx_size_ = 0;
          }
        } else {
          rx_read_ptr_ = 2;  // Skip address and length
          rx_available_ = true;
          tx_report_size_ = 0;
        }
      } else {
        // For other devices.
        rx_size_ = 0;
      }
    }
  }
}

uint8_t* JVSIO::receiveStatus(uint8_t* len) {
  do {
    receive();  // TODO: timeout.
    if (!sense_->is_connected())
      return nullptr;
  } while(!rx_available_);
  if (rx_error_)
    return nullptr;
  *len = rx_data_[1] - 1;
  rx_size_ = 0;
  rx_available_ = false;
  rx_receiving_ = false;
  return &rx_data_[2];
}

void JVSIO::sendPacket(const uint8_t* data) {
  data_->startTransaction();
  data_->write(kSync);
  uint8_t sum = 0;

  for (uint8_t i = 0; i <= data[1]; ++i) {
    sum += data[i];
    writeEscapedByte(data_, data[i]);
  }
  writeEscapedByte(data_, sum);
  data_->endTransaction();

  data_->setMode(INPUT);
}

void JVSIO::sendStatus() {
  rx_available_ = false;
  rx_receiving_ = false;

  // Should not reply for broadcast commands.
  if (kBroadcastAddress == address_[nodes_ - 1] &&
      kBroadcastAddress == new_address_)
    return;

  // Direction should be changed within 100usec from sending/receiving a packet.
  data_->setMode(OUTPUT);

  // Spec requires 100usec interval at minimum between each packet.
  // But response should be sent within 1msec from the last byte received.
  delayMicroseconds(100);

  // Address is just assigned.
  // This timing to negate the sense signal is subtle. The spec expects this
  // signal should be done in 1msec. This place meets this requiment, but if
  // this action was too quick, the direct upstream client may misunderstand
  // that the last address command was for the upstream client. So, this signal
  // should be changed lately as we can as possible within the spec requiment.
  // However, as described below, sending response packet may take over 1msec.
  // Thus, this is the last place to negate the signal in a simle way.
  if (kBroadcastAddress != new_address_) {
    for (uint8_t i = 0; i < nodes_; ++i) {
      if (address_[i] != kBroadcastAddress)
        continue;
      address_[i] = new_address_;
      if (i == (nodes_ - 1))
        senseReady();
      break;
    }
    new_address_ = kBroadcastAddress;
  }

  // We can send about 14 bytes per 1msec at maximum. So, it will take over 18
  // msec to send the largest packet. Actual packet will have interval time
  // between each byte. In total, it may take more time.
  sendPacket(tx_data_);
}

void JVSIO::sendOkStatus() {
  if (tx_report_size_ > 253)
    return sendOverflowStatus();
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2 + tx_report_size_;
  tx_data_[2] = 0x01;
  sendStatus();
}

void JVSIO::sendUnknownCommandStatus() {
  if (tx_report_size_ > 253)
    return sendOverflowStatus();
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2 + tx_report_size_;
  tx_data_[2] = 0x02;
  sendStatus();
}

void JVSIO::sendSumErrorStatus() {
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2;
  tx_data_[2] = 0x03;
  sendStatus();
}

void JVSIO::sendOverflowStatus() {
  tx_data_[0] = kHostAddress;
  tx_data_[1] = 2;
  tx_data_[2] = 0x04;
  sendStatus();
}
