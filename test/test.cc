// Copyright 2022 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

extern "C" {
#include "JVSIO_c.h"
}

#include <functional>

#include "gtest/gtest.h"

class ClientTest : public ::testing::Test {
 protected:
  uint8_t* GetNextCommand(uint8_t* len) {
    return io_->getNextCommand(io_, len, 0);
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

    led_.begin = DoNothingForLed;
    led_.set = SetLed;

    io_ = JVSIO_open(&data_, &sense_, &led_, 1);
    ASSERT_TRUE(io_);
    io_->begin(io_);
  }

  static int IsDataAvailable(JVSIO_DataClient* client) { return 0; }
  static void DoNothingForData(JVSIO_DataClient* client) {}
  static uint8_t ReadData(JVSIO_DataClient* client) { return 0; }
  static void WriteData(JVSIO_DataClient* client, uint8_t data) {}
  static void Delay(JVSIO_DataClient* client, unsigned int time) {}

  static void DoNothingForSense(JVSIO_SenseClient* client) {}
  static void SetSense(JVSIO_SenseClient* client, bool ready) {}
  static bool IsSenseReady(JVSIO_SenseClient* client) { return false; }
  static bool IsSenseConnected(JVSIO_SenseClient* client) { return false; }

  static void DoNothingForLed(JVSIO_LedClient* client) {}
  static void SetLed(JVSIO_LedClient* client, bool ready) {}

  JVSIO_DataClient data_;
  JVSIO_SenseClient sense_;
  JVSIO_LedClient led_;
  JVSIO_Lib* io_ = nullptr;
};

TEST_F(ClientTest, DoNothing) {
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
}