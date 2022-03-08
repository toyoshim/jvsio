// Copyright 2022 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

extern "C" {
#include "JVSIO_c.h"
}

#include <functional>
#include <queue>
#include <vector>

#include "gtest/gtest.h"

namespace {

const uint8_t kSync = 0xe0;
const uint8_t kMarker = 0xd0;
const uint8_t kHostAddress = 0x00;
const uint8_t kClientAddress = 0x01;
const uint8_t kBroadcastAddress = 0xff;

class ClientTest : public ::testing::Test {
 protected:
  uint8_t* GetNextCommand(uint8_t* len) {
    return io_->getNextCommand(io_, len, 0);
  }

  bool IsReady() { return ready_; }
  void SetReady(bool ready) { ready_ = ready; }

  void SetCommand(uint8_t address, const uint8_t* command, uint8_t size) {
    std::vector<uint8_t> data;
    uint8_t sum = address + size + 1;
    data.push_back(address);
    data.push_back(size + 1);
    for (size_t i = 0; i < size; ++i) {
      sum += command[i];
      data.push_back(command[i]);
    }
    data.push_back(sum);

    ASSERT_TRUE(incoming_data_.empty());
    incoming_data_.push(kSync);
    for (const auto& c : data) {
      if (c == kSync || c == kMarker) {
        incoming_data_.push(kMarker);
        incoming_data_.push(c - 1);
      } else {
        incoming_data_.push(c);
      }
    }
  }

 private:
  void SetUp() override {
    data_.available = IsDataAvailable;
    data_.setInput = DoNothingForData;
    data_.setOutput = DoNothingForData;
    data_.startTransaction = DoNothingForData;
    data_.endTransaction = DoNothingForData;
    data_.read = ReadData;
    data_.write = WriteData;
    data_.delayMicroseconds = Delay;
    data_.delay = Delay;
    data_.work = this;

    sense_.begin = DoNothingForSense;
    sense_.set = SetSense;
    sense_.is_ready = IsSenseReady;
    sense_.is_connected = IsSenseConnected;
    sense_.work = this;

    led_.begin = DoNothingForLed;
    led_.set = SetLed;
    led_.work = this;

    io_ = JVSIO_open(&data_, &sense_, &led_, 1);
    ASSERT_TRUE(io_);
    io_->begin(io_);
  }

  static ClientTest* From(JVSIO_DataClient* client) {
    return static_cast<ClientTest*>(client->work);
  }
  static ClientTest* From(JVSIO_SenseClient* client) {
    return static_cast<ClientTest*>(client->work);
  }
  static int IsDataAvailable(JVSIO_DataClient* client) {
    auto* self = ClientTest::From(client);
    if (self->incoming_data_.empty())
      return 0;
    return 1;
  }
  static void DoNothingForData(JVSIO_DataClient* client) {}
  static uint8_t ReadData(JVSIO_DataClient* client) {
    auto* self = ClientTest::From(client);
    auto c = self->incoming_data_.front();
    self->incoming_data_.pop();
    return c;
  }
  static void WriteData(JVSIO_DataClient* client, uint8_t data) {}
  static void Delay(JVSIO_DataClient* client, unsigned int time) {}
  static void DoNothingForSense(JVSIO_SenseClient* client) {}
  static void SetSense(JVSIO_SenseClient* client, bool ready) {
    ClientTest::From(client)->SetReady(ready);
  }
  static bool IsSenseReady(JVSIO_SenseClient* client) {
    return ClientTest::From(client)->IsReady();
  }
  static bool IsSenseConnected(JVSIO_SenseClient* client) { return false; }

  static void DoNothingForLed(JVSIO_LedClient* client) {}
  static void SetLed(JVSIO_LedClient* client, bool ready) {}

  JVSIO_DataClient data_;
  JVSIO_SenseClient sense_;
  JVSIO_LedClient led_;
  JVSIO_Lib* io_ = nullptr;

  bool ready_ = false;
  std::queue<uint8_t> incoming_data_;
};

TEST_F(ClientTest, DoNothing) {
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
}

TEST_F(ClientTest, Reset) {
  SetReady(true);
  ASSERT_TRUE(IsReady());

  const uint8_t kResetCommand[] = {0xf0, 0xd9};
  SetCommand(kBroadcastAddress, kResetCommand, sizeof(kResetCommand));

  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(kCmdReset, data[0]);
  EXPECT_EQ(2u, len);

  EXPECT_FALSE(IsReady());
}

}  // namespace