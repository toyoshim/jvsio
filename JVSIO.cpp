// Copyright 2022 Takashi Toyoshima <toyoshim@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "JVSIO.h"

#include "Arduino.h"

extern "C" {
#include "JVSIO_c.h"
};

namespace {

static int data_available(struct JVSIO_DataClient* client) {
  return static_cast<JVSIO::DataClient*>(client->work)->available();
}
static void data_setInput(struct JVSIO_DataClient* client) {
  static_cast<JVSIO::DataClient*>(client->work)->setMode(INPUT);
}
static void data_setOutput(struct JVSIO_DataClient* client) {
  static_cast<JVSIO::DataClient*>(client->work)->setMode(OUTPUT);
}
static void data_startTransaction(struct JVSIO_DataClient* client) {
  static_cast<JVSIO::DataClient*>(client->work)->startTransaction();
}
static void data_endTransaction(struct JVSIO_DataClient* client) {
  static_cast<JVSIO::DataClient*>(client->work)->endTransaction();
}
static uint8_t data_read(struct JVSIO_DataClient* client) {
  return static_cast<JVSIO::DataClient*>(client->work)->read();
}

static void data_write(struct JVSIO_DataClient* client, uint8_t data) {
  static_cast<JVSIO::DataClient*>(client->work)->write(data);
}
static void data_delayMicroseconds(struct JVSIO_DataClient* client,
                                   unsigned int usec) {
  delayMicroseconds(usec);
}
static void data_delay(struct JVSIO_DataClient* client, unsigned int msec) {
  delay(msec);
}
static bool data_setCommSupMode(struct JVSIO_DataClient* client,
                                enum JVSIO_CommSupMode mode,
                                bool dryrun) {
  return mode == k115200;
}
static void data_dump(struct JVSIO_DataClient* client,
                      const char* str,
                      uint8_t* data,
                      uint8_t len) {
  static_cast<JVSIO::DataClient*>(client->work)->dump(str, data, len);
}
static void sense_begin(struct JVSIO_SenseClient* client) {
  static_cast<JVSIO::SenseClient*>(client->work)->begin();
}
static void sense_set(struct JVSIO_SenseClient* client, bool ready) {
  static_cast<JVSIO::SenseClient*>(client->work)->set(ready);
}
static bool sense_isReady(struct JVSIO_SenseClient* client) {
  return static_cast<JVSIO::SenseClient*>(client->work)->isReady();
}
static bool sense_isConnected(struct JVSIO_SenseClient* client) {
  return static_cast<JVSIO::SenseClient*>(client->work)->isConnected();
}

static void led_begin(struct JVSIO_LedClient* client) {
  static_cast<JVSIO::LedClient*>(client->work)->begin();
}
static void led_set(struct JVSIO_LedClient* client, bool ready) {
  static_cast<JVSIO::LedClient*>(client->work)->set(ready);
}

static JVSIO_DataClient data_client;
static JVSIO_SenseClient sense_client;
static JVSIO_LedClient led_client;

}  // namespace

JVSIO::JVSIO(DataClient* data,
             SenseClient* sense,
             LedClient* led,
             uint8_t nodes) {
  data_client.available = data_available;
  data_client.setInput = data_setInput;
  data_client.setOutput = data_setOutput;
  data_client.startTransaction = data_startTransaction;
  data_client.endTransaction = data_endTransaction;
  data_client.read = data_read;
  data_client.write = data_write;
  data_client.delayMicroseconds = data_delayMicroseconds;
  data_client.delay = data_delay;
  data_client.setCommSupMode = data_setCommSupMode;
  data_client.dump = data_dump;
  data_client.work = static_cast<void*>(data);

  sense_client.begin = sense_begin;
  sense_client.set = sense_set;
  sense_client.isReady = sense_isReady;
  sense_client.isConnected = sense_isConnected;
  sense_client.work = static_cast<void*>(sense);

  led_client.begin = led_begin;
  led_client.set = led_set;
  led_client.work = static_cast<void*>(led);

  io = JVSIO_open(&data_client, &sense_client, &led_client, nodes);
}

JVSIO::~JVSIO() {
  JVSIO_close(io);
}

void JVSIO::begin() {
  io->begin(io);
}
void JVSIO::end() {
  io->end(io);
}

uint8_t* JVSIO::getNextCommand(uint8_t* len, uint8_t* node) {
  return io->getNextCommand(io, len, node);
}

void JVSIO::pushReport(uint8_t report) {
  io->pushReport(io, report);
}

void JVSIO::boot() {
  io->boot(io, true);
}

bool JVSIO::sendAndReceive(const uint8_t* packet,
                           uint8_t** ack,
                           uint8_t* ack_len) {
  return io->sendAndReceive(io, packet, ack, ack_len);
}
