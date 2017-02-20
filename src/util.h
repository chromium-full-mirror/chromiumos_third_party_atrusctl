/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

void
bcd_to_version(uint16_t bcd, int *major, int *minor, int *patch);

#endif
