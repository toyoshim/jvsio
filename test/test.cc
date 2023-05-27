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
  uint8_t* GetNextSpeculativeCommand(uint8_t* len) {
    return io_->getNextSpeculativeCommand(io_, len, 0);
  }

  bool IsReady() { return ready_; }
  void SetReady(bool ready) { ready_ = ready; }

  void SetRawCommand(const uint8_t* command, uint8_t size) {
    for (size_t i = 0; i < size; ++i) {
      incoming_data_.push(command[i]);
    }
  }

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

  bool IsCommandEmpty() { return incoming_data_.empty(); }

  uint8_t GetStatus(std::vector<uint8_t>& report) {
    EXPECT_GE(outgoing_data_.size(), 5u);
    EXPECT_EQ(kSync, outgoing_data_[0]);
    EXPECT_EQ(kHostAddress, outgoing_data_[1]);
    uint8_t size = outgoing_data_[2];
    uint8_t status = outgoing_data_[3];
    uint8_t sum = kHostAddress + size + status;
    report.clear();
    for (uint8_t i = 4; i < outgoing_data_.size() - 1; ++i) {
      report.push_back(outgoing_data_[i]);
      sum += outgoing_data_[i];
    }
    EXPECT_EQ(sum, outgoing_data_[outgoing_data_.size() - 1]);
    outgoing_data_.clear();
    return status;
  }

  void SetUpAddress() {
    const uint8_t kAddressSetCommand[] = {kCmdAddressSet, kClientAddress};
    SetCommand(kBroadcastAddress, kAddressSetCommand,
               sizeof(kAddressSetCommand));

    uint8_t len;
    uint8_t* data = GetNextCommand(&len);
    EXPECT_EQ(nullptr, data);

    EXPECT_TRUE(IsReady());
  }

  void SendUnknownStatus() { io_->sendUnknownStatus(io_); }
  void PushReport(uint8_t report) { io_->pushReport(io_, report); }

 private:
  void SetUp() override {
    data_.available = IsDataAvailable;
    data_.setInput = DoNothingForData;
    data_.setOutput = DoNothingForData;
    data_.startTransaction = DoNothingForData;
    data_.endTransaction = DoNothingForData;
    data_.read = ReadData;
    data_.write = WriteData;
    data_.dump = Dump;
    data_.work = this;

    sense_.begin = DoNothingForSense;
    sense_.set = SetSense;
    sense_.isReady = IsSenseReady;
    sense_.isConnected = IsSenseConnected;
    sense_.work = this;

    led_.begin = DoNothingForLed;
    led_.set = SetLed;
    led_.work = this;

    time_.delayMicroseconds = Delay;
    time_.delay = Delay;
    time_.work = this;

    io_ = JVSIO_open(&data_, &sense_, &led_, &time_, 1);
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
  static void WriteData(JVSIO_DataClient* client, uint8_t data) {
    auto* self = ClientTest::From(client);
    if (self->outgoing_marked_) {
      self->outgoing_data_.push_back(data + 1);
      self->outgoing_marked_ = false;
    } else {
      if (data == kMarker)
        self->outgoing_marked_ = true;
      else
        self->outgoing_data_.push_back(data);
    }
  }
  static void Delay(JVSIO_TimeClient* client, unsigned int time) {}
  static void Dump(JVSIO_DataClient* client,
                   const char* str,
                   uint8_t* data,
                   uint8_t len) {
    fprintf(stderr, "%s: ", str);
    for (uint8_t i = 0; i < len; ++i)
      fprintf(stderr, "%02x", data[i]);
    fprintf(stderr, "\n");
  }
  static void DoNothingForSense(JVSIO_SenseClient* client) {}
  static void SetSense(JVSIO_SenseClient* client, bool ready) {
    ClientTest::From(client)->SetReady(ready);
  }
  static bool IsSenseReady(JVSIO_SenseClient* client) { return true; }
  static bool IsSenseConnected(JVSIO_SenseClient* client) { return false; }

  static void DoNothingForLed(JVSIO_LedClient* client) {}
  static void SetLed(JVSIO_LedClient* client, bool ready) {}

  JVSIO_DataClient data_;
  JVSIO_SenseClient sense_;
  JVSIO_LedClient led_;
  JVSIO_TimeClient time_;
  JVSIO_Lib* io_ = nullptr;

  bool ready_ = false;
  std::queue<uint8_t> incoming_data_;
  std::vector<uint8_t> outgoing_data_;
  bool outgoing_marked_ = false;
};

TEST_F(ClientTest, DoNothing) {
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
}

TEST_F(ClientTest, Reset) {
  SetReady(true);
  ASSERT_TRUE(IsReady());

  const uint8_t kResetCommand[] = {kCmdReset, 0xd9};
  SetCommand(kBroadcastAddress, kResetCommand, sizeof(kResetCommand));

  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(kCmdReset, data[0]);
  EXPECT_EQ(2u, len);

  EXPECT_FALSE(IsReady());
}

TEST_F(ClientTest, AddressSet) {
  ASSERT_FALSE(IsReady());

  const uint8_t kAddressSetCommand[] = {kCmdAddressSet, kClientAddress};
  SetCommand(kBroadcastAddress, kAddressSetCommand, sizeof(kAddressSetCommand));

  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);

  EXPECT_TRUE(IsReady());

  std::vector<uint8_t> reports;
  uint8_t status = GetStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(0x01, reports[0]);

  const uint8_t kResetCommand[] = {kCmdReset, 0xd9};
  SetCommand(0x02, kResetCommand, sizeof(kResetCommand));
  GetNextCommand(&len);
  EXPECT_TRUE(IsReady());

  SetCommand(kClientAddress, kResetCommand, sizeof(kResetCommand));
  GetNextCommand(&len);
  EXPECT_FALSE(IsReady());
}

TEST_F(ClientTest, Namco_70_18_50_4c_14) {
  SetUpAddress();

  std::vector<uint8_t> reports;
  uint8_t status = GetStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(0x01, reports[0]);

  const uint8_t kCommand[] = {kCmdNamco, 0x18, 0x50, 0x4c, 0x14, 0xd0,
                              0x3b,      0x57, 0x69, 0x6e, 0x41, 0x72};
  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(kCmdNamco, data[0]);
  EXPECT_EQ(0x18, data[1]);
  EXPECT_EQ(0x50, data[2]);
  EXPECT_EQ(0x4c, data[3]);
  EXPECT_EQ(0x14, data[4]);
  SendUnknownStatus();

  status = GetStatus(reports);
  EXPECT_EQ(0x02, status);
  ASSERT_EQ(0u, reports.size());
}

TEST_F(ClientTest, Namco_70_18_50_4c_02) {
  SetUpAddress();

  std::vector<uint8_t> reports;
  uint8_t status = GetStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(0x01, reports[0]);

  const uint8_t kCommand[] = {kCmdNamco, 0x18, 0x50, 0x4c, 0x02, 0x0e, 0x10};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(kCmdNamco, data[0]);
  EXPECT_EQ(0x18, data[1]);
  EXPECT_EQ(0x50, data[2]);
  EXPECT_EQ(0x4c, data[3]);
  EXPECT_EQ(0x02, data[4]);
  PushReport(kReportOk);
  PushReport(0x01);

  data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);

  status = GetStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(2u, reports.size());
  EXPECT_EQ(0x01, reports[0]);
  EXPECT_EQ(0x01, reports[1]);
}

TEST_F(ClientTest, Namco_70_18_50_4c_80) {
  SetUpAddress();

  std::vector<uint8_t> reports;
  uint8_t status = GetStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(0x01, reports[0]);

  const uint8_t kCommand[] = {kCmdNamco, 0x18, 0x50, 0x4c, 0x80, 0x08};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(kCmdNamco, data[0]);
  EXPECT_EQ(0x18, data[1]);
  EXPECT_EQ(0x50, data[2]);
  EXPECT_EQ(0x4c, data[3]);
  EXPECT_EQ(0x80, data[4]);
  PushReport(kReportOk);
  PushReport(0x01);

  data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);

  status = GetStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(2u, reports.size());
  EXPECT_EQ(0x01, reports[0]);
  EXPECT_EQ(0x01, reports[1]);
}

TEST_F(ClientTest, MultiCommand) {
  SetUpAddress();

  const uint8_t kCommand[] = {0x32, 0x01, 0x20, 0x20, 0x02, 0x02,
                              0x21, 0x02, 0x22, 0x04, 0x25, 0x01};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(3, len);
  EXPECT_EQ(0x32, *data);
  PushReport(kReportOk);
  PushReport(0x01);

  data = GetNextCommand(&len);
  EXPECT_EQ(3, len);
  EXPECT_EQ(0x20, *data);

  data = GetNextCommand(&len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0x21, *data);

  data = GetNextCommand(&len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0x22, *data);

  data = GetNextCommand(&len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0x25, *data);

  data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
}

TEST_F(ClientTest, PartialCommandVerified) {
  SetUpAddress();

  const uint8_t kCommand1[] = {0xe0, 0x01, 0x0d, 0x32};
  const uint8_t kCommand2[] = {0x01, 0x20, 0x20, 0x02, 0x02};
  const uint8_t kCommand3[] = {0x21, 0x02, 0x22};
  const uint8_t kCommand4[] = {0x04, 0x25, 0x01, 0xf4};

  SetRawCommand(kCommand1, sizeof(kCommand1));

  uint8_t len;
  uint8_t* data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
  ASSERT_TRUE(IsCommandEmpty());

  SetRawCommand(kCommand2, sizeof(kCommand2));
  data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
  ASSERT_TRUE(IsCommandEmpty());

  SetRawCommand(kCommand3, sizeof(kCommand3));
  data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
  ASSERT_TRUE(IsCommandEmpty());

  SetRawCommand(kCommand4, sizeof(kCommand4));
  data = GetNextCommand(&len);
  ASSERT_TRUE(IsCommandEmpty());
  ASSERT_EQ(3, len);
  EXPECT_EQ(0x32, *data);

  data = GetNextCommand(&len);
  ASSERT_EQ(3, len);
  EXPECT_EQ(0x20, *data);

  data = GetNextCommand(&len);
  ASSERT_EQ(2, len);
  EXPECT_EQ(0x21, *data);

  data = GetNextCommand(&len);
  ASSERT_EQ(2, len);
  EXPECT_EQ(0x22, *data);

  data = GetNextCommand(&len);
  ASSERT_EQ(2, len);
  EXPECT_EQ(0x25, *data);

  data = GetNextCommand(&len);
  EXPECT_EQ(nullptr, data);
}

TEST_F(ClientTest, PartialCommandSpeculative) {
  SetUpAddress();

  const uint8_t kCommand1[] = {0xe0, 0x01, 0x0d, 0x32};
  const uint8_t kCommand2[] = {0x01, 0x20, 0x20, 0x02, 0x02};
  const uint8_t kCommand3[] = {0x21, 0x02, 0x22};
  const uint8_t kCommand4[] = {0x04, 0x25, 0x01, 0xf4};

  SetRawCommand(kCommand1, sizeof(kCommand1));

  uint8_t len;
  uint8_t* data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(nullptr, data);

  SetRawCommand(kCommand2, sizeof(kCommand2));

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(3, len);
  EXPECT_EQ(0x32, *data);

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(3, len);
  EXPECT_EQ(0x20, *data);

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(nullptr, data);

  SetRawCommand(kCommand3, sizeof(kCommand3));

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0x21, *data);

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(nullptr, data);

  SetRawCommand(kCommand4, sizeof(kCommand4));

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0x22, *data);

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(2, len);
  EXPECT_EQ(0x25, *data);

  data = GetNextSpeculativeCommand(&len);
  EXPECT_EQ(nullptr, data);
}

}  // namespace