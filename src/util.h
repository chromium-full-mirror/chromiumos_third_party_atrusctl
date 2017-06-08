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

void bcd_to_version(uint16_t bcd, int* major, int* minor, int* patch);

void timeout(long ms);

}  // namespace atrusctl

#endif
