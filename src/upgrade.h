// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPGRADE_H_
#define UPGRADE_H_

#include <string>

#include <base/files/file_path.h>

#include "atrus_device.h"

namespace atrusctl {

bool PerformUpgrade(const base::FilePath& upgrade_file,
                    bool* skipped,
                    DeviceVariant device_variant,
                    bool force_upgrade = false,
                    std::string usb_path = "");

}  // namespace atrusctl

#endif  // UPGRADE_H_
