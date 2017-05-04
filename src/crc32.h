/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CRC32_H_
#define CRC32_H_

#include <stdint.h>

namespace atrusctl {

uint32_t crc32(uint32_t accum, uint8_t delta);

}  // namespace atrusctl

#endif
