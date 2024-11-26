// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATRUS_DEVICE_H_
#define ATRUS_DEVICE_H_

#include <cstdint>

namespace atrusctl {

const uint16_t kAtrusUsbVid = 0x18D1;
const uint16_t kAtrusUsbPid = 0x8001;
const uint16_t kAtrusUsbDfuVid = kAtrusUsbVid;
const uint16_t kAtrusUsbDfuPid = 0x8002;

const uint16_t kOrionUsbVid = kAtrusUsbVid;
const uint16_t kOrionUsbPid = 0x8007;
const uint16_t kOrionUsbDfuVid = kOrionUsbVid;
const uint16_t kOrionUsbDfuPid = 0x8008;

enum class DeviceVariant {
  kUnknown,
  kAtrus,
  kOrion,
};

}  // namespace atrusctl

#endif  // ATRUS_DEVICE_H_
