// Copyright 2023 Takashi Toyoshima <toyoshim@gmail.com>.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !defined(__JVSIO_HOST_H__)
#define __JVSIO_HOST_H__

#include <stdbool.h>

void JVSIO_Host_init();
bool JVSIO_Host_run();
void JVSIO_Host_sync();

#endif  // !defined(__JVSIO_HOST_H__)