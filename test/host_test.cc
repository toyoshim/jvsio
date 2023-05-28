// Copyright 2023 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

extern "C" {
#include "jvsio_host.h"
}  // extern "C"

#include "gtest/gtest.h"

class HostTest : public ::testing::Test {
 private:
  void SetUp() override { JVSIO_Host_init(); }
};

extern "C" {
int JVSIO_Client_isDataAvailable() {
  return 0;
}
void JVSIO_Client_willSend() {}
void JVSIO_Client_willReceive() {}
void JVSIO_Client_send(uint8_t data) {}
uint8_t JVSIO_Client_receive() {
  return 0;
}
void JVSIO_Client_dump(const char* str, uint8_t* data, uint8_t len) {}
bool JVSIO_Client_isSenseReady() {
  return false;
}
bool JVSIO_Client_isSenseConnected() {
  return false;
}
uint32_t JVSIO_Client_getTick() {
  return 0;
}
void JVSIO_Client_ioIdReceived(uint8_t address, uint8_t* data, uint8_t len) {}
void JVSIO_Client_commandRevReceived(uint8_t address, uint8_t rev) {}
void JVSIO_Client_jvRevReceived(uint8_t address, uint8_t rev) {}
void JVSIO_Client_protocolVerReceived(uint8_t address, uint8_t rev) {}
void JVSIO_Client_functionCheckReceived(uint8_t address,
                                        uint8_t* data,
                                        uint8_t len) {}
void JVSIO_Client_synced(uint8_t players,
                         uint8_t coin_state,
                         uint8_t* sw_state0,
                         uint8_t* sw_state1) {}
}  // extern "C"

TEST_F(HostTest, CompileAndLink) {
  JVSIO_Host_run();
}