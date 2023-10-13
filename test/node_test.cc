// Copyright 2022 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

extern "C" {
#include "jvsio_client.h"
#include "jvsio_common.h"
#include "jvsio_node.h"
}

#include <functional>
#include <queue>
#include <vector>

#include "gtest/gtest.h"

const uint8_t kClientAddress = 0x01;

class ClientTest : public ::testing::Test {
 public:
  static int IsDataAvailable() {
    if (instance->incoming_data_.empty())
      return 0;
    return 1;
  }
  static uint8_t ReadData() {
    auto c = instance->incoming_data_.front();
    instance->incoming_data_.pop();
    return c;
  }
  static void WriteData(uint8_t data) {
    if (instance->outgoing_marked_) {
      instance->outgoing_data_.push_back(data + 1);
      instance->outgoing_marked_ = false;
    } else {
      if (data == kMarker)
        instance->outgoing_marked_ = true;
      else
        instance->outgoing_data_.push_back(data);
    }
  }
  static void Dump(const char* str, uint8_t* data, uint8_t len) {
    fprintf(stderr, "%s: ", str);
    for (uint8_t i = 0; i < len; ++i)
      fprintf(stderr, "%02x", data[i]);
    fprintf(stderr, "\n");
  }
  static void SetSense(bool ready) { instance->SetReady(ready); }
  static bool ReceiveCommand(uint8_t node,
                             uint8_t* command,
                             uint8_t len,
                             bool commit) {
    Command data;
    data.node = node;
    for (uint8_t i = 0; i < len; ++i) {
      data.command.push_back(command[i]);
    }
    data.commit = commit;
    instance->received_commands_.push_back(data);
    if (instance->report_.empty()) {
      return false;
    }
    auto report = instance->report_.front();
    instance->report_.pop();
    for (uint8_t c : report) {
      JVSIO_Node_pushReport(c);
    }
    return true;
  }

 protected:
  struct Command {
    uint8_t node;
    std::vector<uint8_t> command;
    bool commit;
  };

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

  bool IsIncomingDataEmpty() { return incoming_data_.empty(); }
  bool IsOutgoingDataEmpty() { return outgoing_data_.empty(); }

  uint8_t RetrieveStatus(std::vector<uint8_t>& report) {
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

  std::vector<Command>& GetReceivedCommands() { return received_commands_; }

  void SetUpAddress() {
    const uint8_t kAddressSetCommand[] = {kCmdAddressSet, kClientAddress};
    SetCommand(kBroadcastAddress, kAddressSetCommand,
               sizeof(kAddressSetCommand));

    size_t commands = received_commands_.size();
    JVSIO_Node_run(false);
    EXPECT_EQ(commands, received_commands_.size());

    EXPECT_TRUE(IsReady());

    std::vector<uint8_t> reports;
    uint8_t status = RetrieveStatus(reports);
    EXPECT_EQ(0x01, status);
    ASSERT_EQ(1u, reports.size());
    EXPECT_EQ(0x01, reports[0]);
  }

  void PushReport(std::vector<uint8_t> report) { report_.push(report); }

 private:
  void SetUp() override {
    JVSIO_Node_init(1);
    instance = this;
  }

  static void Delay(unsigned int time) {}

 private:
  bool ready_ = false;
  std::queue<uint8_t> incoming_data_;
  std::vector<uint8_t> outgoing_data_;
  std::vector<Command> received_commands_;
  std::queue<std::vector<uint8_t>> report_;
  bool outgoing_marked_ = false;

  static ClientTest* instance;
};

ClientTest* ClientTest::instance = nullptr;

extern "C" {
int JVSIO_Client_isDataAvailable() {
  return ClientTest::IsDataAvailable();
}
void JVSIO_Client_willSend() {}
void JVSIO_Client_willReceive() {}
void JVSIO_Client_send(uint8_t data) {
  ClientTest::WriteData(data);
}
uint8_t JVSIO_Client_receive() {
  return ClientTest::ReadData();
}
void JVSIO_Client_dump(const char* str, uint8_t* data, uint8_t len) {
  ClientTest::Dump(str, data, len);
}
bool JVSIO_Client_isSenseReady() {
  return true;
}
bool JVSIO_Client_receiveCommand(uint8_t node,
                                 uint8_t* command,
                                 uint8_t len,
                                 bool commit) {
  return ClientTest::ReceiveCommand(node, command, len, commit);
}
bool JVSIO_Client_setCommSupMode(enum JVSIO_CommSupMode mode, bool dryrun) {
  return false;
}
void JVSIO_Client_setSense(bool ready) {
  ClientTest::SetSense(ready);
}
void JVSIO_Client_setLed(bool ready) {}
void JVSIO_Client_delayMicroseconds(unsigned int usec) {}
}  // extern "C"

TEST_F(ClientTest, DoNothing) {
  JVSIO_Node_run(false);
  EXPECT_EQ(0u, GetReceivedCommands().size());
}

TEST_F(ClientTest, Reset) {
  SetReady(true);
  ASSERT_TRUE(IsReady());

  const uint8_t kResetCommand[] = {kCmdReset, 0xd9};
  SetCommand(kBroadcastAddress, kResetCommand, sizeof(kResetCommand));

  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(1u, GetReceivedCommands().size());
  EXPECT_EQ(kCmdReset, GetReceivedCommands()[0].command[0]);
  EXPECT_EQ(2, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(true, GetReceivedCommands()[0].commit);

  EXPECT_FALSE(IsReady());

  EXPECT_TRUE(IsOutgoingDataEmpty());
}

TEST_F(ClientTest, AddressSet) {
  ASSERT_FALSE(IsReady());

  const uint8_t kAddressSetCommand[] = {kCmdAddressSet, kClientAddress};
  SetCommand(kBroadcastAddress, kAddressSetCommand, sizeof(kAddressSetCommand));

  // Address command should not be passed to the client.
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(0u, GetReceivedCommands().size());

  EXPECT_TRUE(IsReady());

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(0x01, reports[0]);

  // Reset to a different address should be ignored.
  const uint8_t kResetCommand[] = {kCmdReset, 0xd9};
  SetCommand(0x02, kResetCommand, sizeof(kResetCommand));
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsReady());

  // Reset to the node address should be handled.
  SetCommand(kClientAddress, kResetCommand, sizeof(kResetCommand));
  JVSIO_Node_run(false);
  EXPECT_FALSE(IsReady());
}

TEST_F(ClientTest, AddressSetNeedsCheckSum) {
  ASSERT_FALSE(IsReady());

  const uint8_t kAddressSetCommand[] = {kCmdAddressSet, kClientAddress};
  SetCommand(kBroadcastAddress, kAddressSetCommand, sizeof(kAddressSetCommand));

  // Address command should not be passed to the client.
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(0u, GetReceivedCommands().size());

  EXPECT_TRUE(IsReady());

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(0x01, reports[0]);
}

TEST_F(ClientTest, SumError) {
  SetUpAddress();

  const uint8_t kCommand[] = {0xe0, 0x01, 0x04, 0x32, 0x01, 0x20, 0x00};
  SetRawCommand(kCommand, sizeof(kCommand));

  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(0u, GetReceivedCommands().size());

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x03, status);
  EXPECT_EQ(0u, reports.size());
}

TEST_F(ClientTest, MultiPackets) {
  SetUpAddress();

  const uint8_t kCommand[] = {0x21, 0x02};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x01});
  ASSERT_TRUE(IsOutgoingDataEmpty());
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_FALSE(IsOutgoingDataEmpty());

  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_EQ(2u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x21, GetReceivedCommands()[0].command[0]);

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  EXPECT_EQ(2u, reports.size());
  ASSERT_TRUE(IsOutgoingDataEmpty());

  JVSIO_Node_run(false);
  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_TRUE(IsOutgoingDataEmpty());
}

TEST_F(ClientTest, Namco_70_18_50_4c_14) {
  SetUpAddress();

  const uint8_t kCommand[] = {kCmdNamco, 0x18, 0x50, 0x4c, 0x14, 0xd0,
                              0x3b,      0x57, 0x69, 0x6e, 0x41, 0x72};
  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_EQ(12u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(kCmdNamco, GetReceivedCommands()[0].command[0]);
  EXPECT_EQ(0x18, GetReceivedCommands()[0].command[1]);
  EXPECT_EQ(0x50, GetReceivedCommands()[0].command[2]);
  EXPECT_EQ(0x4c, GetReceivedCommands()[0].command[3]);
  EXPECT_EQ(0x14, GetReceivedCommands()[0].command[4]);
  EXPECT_EQ(true, GetReceivedCommands()[0].commit);

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x02, status);
  ASSERT_EQ(0u, reports.size());
}

TEST_F(ClientTest, Namco_70_18_50_4c_02) {
  SetUpAddress();

  const uint8_t kCommand[] = {kCmdNamco, 0x18, 0x50, 0x4c, 0x02, 0x0e, 0x10};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x01});
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_EQ(7u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(kCmdNamco, GetReceivedCommands()[0].command[0]);
  EXPECT_EQ(0x18, GetReceivedCommands()[0].command[1]);
  EXPECT_EQ(0x50, GetReceivedCommands()[0].command[2]);
  EXPECT_EQ(0x4c, GetReceivedCommands()[0].command[3]);
  EXPECT_EQ(0x02, GetReceivedCommands()[0].command[4]);
  EXPECT_EQ(true, GetReceivedCommands()[0].commit);

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(2u, reports.size());
  EXPECT_EQ(0x01, reports[0]);
  EXPECT_EQ(0x01, reports[1]);
}

TEST_F(ClientTest, Namco_70_18_50_4c_80) {
  SetUpAddress();

  const uint8_t kCommand[] = {kCmdNamco, 0x18, 0x50, 0x4c, 0x80, 0x08};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x01});
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_EQ(6u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(kCmdNamco, GetReceivedCommands()[0].command[0]);
  EXPECT_EQ(0x18, GetReceivedCommands()[0].command[1]);
  EXPECT_EQ(0x50, GetReceivedCommands()[0].command[2]);
  EXPECT_EQ(0x4c, GetReceivedCommands()[0].command[3]);
  EXPECT_EQ(0x80, GetReceivedCommands()[0].command[4]);
  EXPECT_EQ(true, GetReceivedCommands()[0].commit);

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(2u, reports.size());
  EXPECT_EQ(0x01, reports[0]);
  EXPECT_EQ(0x01, reports[1]);
}

TEST_F(ClientTest, Namco_70_04_70_02) {
  SetUpAddress();

  const uint8_t kCommand[] = {kCmdNamco, 0x04, 0x70, 0x02};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x01});
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_EQ(4u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(kCmdNamco, GetReceivedCommands()[0].command[0]);
  EXPECT_EQ(0x04, GetReceivedCommands()[0].command[1]);
  EXPECT_EQ(0x70, GetReceivedCommands()[0].command[2]);
  EXPECT_EQ(0x02, GetReceivedCommands()[0].command[3]);
  EXPECT_EQ(true, GetReceivedCommands()[0].commit);

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  ASSERT_EQ(2u, reports.size());
  EXPECT_EQ(0x01, reports[0]);
  EXPECT_EQ(0x01, reports[1]);
}

TEST_F(ClientTest, MultiCommandWithUnknownToLibrary) {
  SetUpAddress();

  const uint8_t kCommand[] = {0x32, 0x01, 0x20, 0x80, 0x00};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x01});
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  // A command that is unknown to the library doesn't appear in the client API
  // as of due to unknown command length.
  ASSERT_EQ(1u, GetReceivedCommands().size());
  ASSERT_EQ(3u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x32, GetReceivedCommands()[0].command[0]);
}

TEST_F(ClientTest, MultiCommandWithUnknownToClient) {
  SetUpAddress();

  const uint8_t kCommand[] = {0x21, 0x02, 0x22, 0x04, 0x25, 0x01};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  // Prepare reports only for the first command, and results on unknown report
  // for remaining commands.
  PushReport({kReportOk, 0x01});
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  // A command that is unknown by the library doesn't appear in the client API
  // as of due to unknown command length.
  ASSERT_EQ(2u, GetReceivedCommands().size());
  ASSERT_EQ(2u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x21, GetReceivedCommands()[0].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[1].command.size());
  EXPECT_EQ(0x22, GetReceivedCommands()[1].command[0]);
}

TEST_F(ClientTest, MultiCommand) {
  SetUpAddress();

  const uint8_t kCommand[] = {0x32, 0x01, 0x20, 0x20, 0x02, 0x02,
                              0x21, 0x02, 0x22, 0x04, 0x25, 0x01};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(5u, GetReceivedCommands().size());
  ASSERT_EQ(3u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x32, GetReceivedCommands()[0].command[0]);
  ASSERT_EQ(3u, GetReceivedCommands()[1].command.size());
  EXPECT_EQ(0x20, GetReceivedCommands()[1].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[2].command.size());
  EXPECT_EQ(0x21, GetReceivedCommands()[2].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[3].command.size());
  EXPECT_EQ(0x22, GetReceivedCommands()[3].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[4].command.size());
  EXPECT_EQ(0x25, GetReceivedCommands()[4].command[0]);
}

TEST_F(ClientTest, MultiCommandWithSpeculaton) {
  SetUpAddress();

  const uint8_t kCommand[] = {0x21, 0x02, 0x20, 0x02, 0x02};

  SetCommand(kClientAddress, kCommand, sizeof(kCommand));
  PushReport({kReportOk, 0x00, 0x01, 0x00, 0x00});
  PushReport({kReportOk, 0x12, 0x34, 0x56, 0x78});
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(2u, GetReceivedCommands().size());
  ASSERT_EQ(2u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x21, GetReceivedCommands()[0].command[0]);
  ASSERT_EQ(3u, GetReceivedCommands()[1].command.size());
  EXPECT_EQ(0x20, GetReceivedCommands()[1].command[0]);
}

TEST_F(ClientTest, PartialCommandVerified) {
  SetUpAddress();

  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});
  PushReport({kReportOk, 0x01});

  const uint8_t kCommand1[] = {0xe0, 0x01, 0x0d, 0x32};
  const uint8_t kCommand2[] = {0x01, 0x20, 0x20, 0x02, 0x02};
  const uint8_t kCommand3[] = {0x21, 0x02, 0x22};
  const uint8_t kCommand4[] = {0x04, 0x25, 0x01, 0xf4};

  SetRawCommand(kCommand1, sizeof(kCommand1));
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(0u, GetReceivedCommands().size());

  SetRawCommand(kCommand2, sizeof(kCommand2));
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(0u, GetReceivedCommands().size());

  SetRawCommand(kCommand3, sizeof(kCommand3));
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(0u, GetReceivedCommands().size());

  SetRawCommand(kCommand4, sizeof(kCommand4));
  JVSIO_Node_run(false);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(5u, GetReceivedCommands().size());
  ASSERT_EQ(3u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x32, GetReceivedCommands()[0].command[0]);
  ASSERT_EQ(3u, GetReceivedCommands()[1].command.size());
  EXPECT_EQ(0x20, GetReceivedCommands()[1].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[2].command.size());
  EXPECT_EQ(0x21, GetReceivedCommands()[2].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[3].command.size());
  EXPECT_EQ(0x22, GetReceivedCommands()[3].command[0]);
  ASSERT_EQ(2u, GetReceivedCommands()[4].command.size());
  EXPECT_EQ(0x25, GetReceivedCommands()[4].command[0]);
}

TEST_F(ClientTest, PartialCommandSpeculative) {
  SetUpAddress();

  PushReport({kReportOk});
  PushReport({kReportOk});
  PushReport({kReportOk});
  PushReport({kReportOk});
  PushReport({kReportOk});

  const uint8_t kCommand1[] = {0xe0, 0x01, 0x0d, 0x32};
  const uint8_t kCommand2[] = {0x01, 0x20, 0x20, 0x02, 0x02};
  const uint8_t kCommand3[] = {0x21, 0x02, 0x22};
  const uint8_t kCommand4[] = {0x04, 0x25, 0x01};
  const uint8_t kCommand5[] = {0xf4};

  SetRawCommand(kCommand1, sizeof(kCommand1));
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(0u, GetReceivedCommands().size());

  SetRawCommand(kCommand2, sizeof(kCommand2));
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(2u, GetReceivedCommands().size());

  SetRawCommand(kCommand3, sizeof(kCommand3));
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(3u, GetReceivedCommands().size());

  SetRawCommand(kCommand4, sizeof(kCommand4));
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  EXPECT_EQ(4u, GetReceivedCommands().size());

  SetRawCommand(kCommand5, sizeof(kCommand5));
  JVSIO_Node_run(true);
  EXPECT_TRUE(IsIncomingDataEmpty());
  ASSERT_EQ(5u, GetReceivedCommands().size());

  ASSERT_EQ(5u, GetReceivedCommands().size());
  ASSERT_EQ(3u, GetReceivedCommands()[0].command.size());
  EXPECT_EQ(0x32, GetReceivedCommands()[0].command[0]);
  EXPECT_FALSE(GetReceivedCommands()[0].commit);
  ASSERT_EQ(3u, GetReceivedCommands()[1].command.size());
  EXPECT_EQ(0x20, GetReceivedCommands()[1].command[0]);
  EXPECT_FALSE(GetReceivedCommands()[1].commit);
  ASSERT_EQ(2u, GetReceivedCommands()[2].command.size());
  EXPECT_EQ(0x21, GetReceivedCommands()[2].command[0]);
  EXPECT_FALSE(GetReceivedCommands()[2].commit);
  ASSERT_EQ(2u, GetReceivedCommands()[3].command.size());
  EXPECT_EQ(0x22, GetReceivedCommands()[3].command[0]);
  EXPECT_FALSE(GetReceivedCommands()[3].commit);
  ASSERT_EQ(2u, GetReceivedCommands()[4].command.size());
  EXPECT_EQ(0x25, GetReceivedCommands()[4].command[0]);
  EXPECT_TRUE(GetReceivedCommands()[4].commit);

  std::vector<uint8_t> reports;
  uint8_t status = RetrieveStatus(reports);
  EXPECT_EQ(0x01, status);
  EXPECT_EQ(5u, reports.size());
}