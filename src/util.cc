// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

namespace atrusctl {

void bcd_to_version(uint16_t bcd, int* major, int* minor, int* patch) {
  *major = (bcd >> 8) & 0xFF;
  *minor = (bcd >> 4) & 0xF;
  *patch = bcd & 0xF;
}

void timeout(long ms) {
  int retval;
  struct timespec sleep_req;
  struct timespec sleep_rem;

  sleep_req.tv_sec = ms / 1000;
  sleep_req.tv_nsec = (ms % 1000) * 1000000;

  do {
    retval = nanosleep(&sleep_req, &sleep_rem);
    if (retval < 0) {
      if (errno != EINTR) {
        return;
      }
      sleep_req.tv_sec = sleep_rem.tv_sec;
      sleep_req.tv_nsec = sleep_rem.tv_nsec;
    }
  } while (retval != 0);
}

}  // namespace atrusctl
