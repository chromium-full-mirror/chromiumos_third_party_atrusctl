// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTIL_H_
#define UTIL_H_

#include <cstdint>

namespace atrusctl {

// Calculates the CRC32 checksum of |data| from the initial |crc|. Example:
//
//   uint8_t data;
//   uint32_t crc = 0xFFFFFFFF;
//   while ((data = read(file)) {
//     crc = Crc32(crc, data);
//   }
//
uint32_t Crc32(uint32_t crc, uint8_t data);

// Will sleep for |ms|, even if interrupted.
void TimeoutMs(int64_t ms);

}  // namespace atrusctl

#endif  // UTIL_H_
