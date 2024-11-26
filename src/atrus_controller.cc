// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "atrus_controller.h"

#include <base/logging.h>

#include "atrus_device.h"
#include "upgrade.h"

namespace atrusctl {

namespace {

// Default values for diagnostic intervals
const int kDefaultDiagIntervalSec = 60;
const int kDefaultExtDiagIntervalSec = 1800;

}  // namespace

AtrusController::AtrusController(const base::FilePath& atrus_firmware_path,
                                 const base::FilePath& orion_firmware_path)
    : atrus_firmware_path_(atrus_firmware_path),
      orion_firmware_path_(orion_firmware_path) {}

void AtrusController::OnDeviceAdded(const std::string& device_path,
                                    DeviceVariant device_variant) {
  diagnostics_.UpdateNumberOfDevices(++device_counter_);
  if (!current_device_path_.empty()) {
    LOG(INFO) << "Device " << device_path
              << " added, but it's ignored in favor of "
              << current_device_path_;
    return;
  } else {
    LOG(INFO) << "Device " << device_path << " added";
  }
  current_device_path_ = device_path;

  // Upgrade firmware (if needed) and start diagnostics
  if (device_variant == DeviceVariant::kAtrus) {
    LOG(INFO) << "Device variant is Atrus";
    UpgradeDeviceFirmware(atrus_firmware_path_, device_variant);
  } else if (device_variant == DeviceVariant::kOrion) {
    LOG(INFO) << "Device variant is Orion";
    UpgradeDeviceFirmware(orion_firmware_path_, device_variant);
  } else {
    LOG(INFO) << "Device variant is Unknown";
  }
}

void AtrusController::OnDeviceRemoved(const std::string& device_path) {
  diagnostics_.UpdateNumberOfDevices(--device_counter_);
  if (device_path != current_device_path_) {
    return;
  }
  LOG(INFO) << "Atrus device removed";
  DisableDiagnostics();
  current_device_path_.clear();
  LOG(INFO) << "(" << diagnostics_.number_of_connected_slaves() << ")"
            << " daisy-chained devices disconnected as a result";
}

bool AtrusController::UpgradeDeviceFirmware(const base::FilePath& firmware_path,
                                            DeviceVariant device_variant,
                                            bool force) {
  DisableDiagnostics();

  bool skipped;
  bool ret = PerformUpgrade(firmware_path, &skipped, device_variant, force);
  LOG(INFO) << "Upgrade "
            << (ret ? (skipped ? "skipped" : "succeeded") : "failed");
  LOG(INFO) << "[Device is entering usage mode]";

  EnableDiagnostics(base::Seconds(kDefaultDiagIntervalSec),
                    base::Seconds(kDefaultExtDiagIntervalSec));

  return ret;
}

void AtrusController::EnableDiagnostics(base::TimeDelta diag_interval,
                                        base::TimeDelta ext_diag_interval) {
  LOG(INFO) << "Polling diagnostics every " << diag_interval;
  VLOG(1) << "Diagnostics specification can be found here: go/atrus-diag";

  diagnostics_.Start(diag_interval, ext_diag_interval, current_device_path_);
}

void AtrusController::DisableDiagnostics() {
  LOG(INFO) << "Diagnostics disabled";

  diagnostics_.Stop();
}

}  // namespace atrusctl
