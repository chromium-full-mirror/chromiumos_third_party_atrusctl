/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

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
