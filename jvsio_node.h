// Copyright 2021 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__JVSIO_NODE_H__)
#define __JVSIO_NODE_H__

#include <stdbool.h>
#include <stdint.h>

void JVSIO_Node_init(uint8_t nodes);
void JVSIO_Node_run(bool speculative);
void JVSIO_Node_pushReport(uint8_t report);
bool JVSIO_Node_isBusy(void);

#endif  // !defined(__JVSIO_NODE_H__)