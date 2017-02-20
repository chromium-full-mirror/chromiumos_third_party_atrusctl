/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

uint32_t
crc32(uint32_t accum, uint8_t delta);

#endif
