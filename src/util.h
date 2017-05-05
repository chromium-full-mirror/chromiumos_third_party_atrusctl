// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_H_
#define UTIL_H_

#include <stdbool.h>
#include <stdint.h>

namespace atrusctl {

extern bool is_debug;
extern bool is_daemon;

void bcd_to_version(uint16_t bcd, int* major, int* minor, int* patch);

void psyslog(int priority, const char* format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

void timeout(long ms);

}  // namespace atrusctl

#endif
