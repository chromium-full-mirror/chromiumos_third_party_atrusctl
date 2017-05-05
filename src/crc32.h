// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRC32_H_
#define CRC32_H_

#include <stdint.h>

namespace atrusctl {

uint32_t crc32(uint32_t accum, uint8_t delta);

}  // namespace atrusctl

#endif
