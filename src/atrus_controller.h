// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATRUS_CONTROLLER_H_
#define ATRUS_CONTROLLER_H_

#include <base/files/file_path.h>

#include "atrus_device.h"
#include "diagnostics.h"
#ifndef __ANDROID__
// udev does not exist in Android
#include "udev_subsystem_observer.h"
#endif

namespace atrusctl {

// Interface that exposes certain functions for AtrusController that are used by
// DBusAdaptor.
class AtrusControllerInterface {
 public:
  virtual ~AtrusControllerInterface() = default;

  virtual void EnableDiagnostics(base::TimeDelta diag_interval,
                                 base::TimeDelta ext_diag_interval) = 0;
  virtual void DisableDiagnostics() = 0;
  virtual bool UpgradeDeviceFirmware(const base::FilePath& firmware_path,
                                     DeviceVariant device_variant,
                                     bool force = false) = 0;
};

// Entry point for all actions available for a connected Atrus device;
// enabling/disabling diagnostics and initiating firmware upgrades.
class AtrusController :
#ifndef __ANDROID__
    public UdevSubsystemObserver,
#endif
    public AtrusControllerInterface {
 public:
  AtrusController(const base::FilePath& atrus_firmware_path,
                  const base::FilePath& orion_firmware_path);

#ifdef __ANDROID__
  // UdevSubsystemObserver
  void OnDeviceAdded(const std::string& device_path,
                     DeviceVariant device_variant);

  // UdevSubsystemObserver
  void OnDeviceRemoved(const std::string& device_path);
#else
  // UdevSubsystemObserver
  void OnDeviceAdded(const std::string& device_path,
                     DeviceVariant device_variant) override;

  // UdevSubsystemObserver
  void OnDeviceRemoved(const std::string& device_path) override;
#endif
  // AtrusControllerInterface
  bool UpgradeDeviceFirmware(const base::FilePath& firmware_path,
                             DeviceVariant device_variant,
                             bool force = false) override;

  // AtrusControllerInterface
  void EnableDiagnostics(base::TimeDelta diag_interval,
                         base::TimeDelta ext_diag_interval) override;

  // AtrusControllerInterface
  void DisableDiagnostics() override;

 private:
  Diagnostics diagnostics_;

  std::string current_device_path_;
  int device_counter_ = 0;

  base::FilePath atrus_firmware_path_;
  base::FilePath orion_firmware_path_;
};

}  // namespace atrusctl

#endif  // ATRUS_CONTROLLER_H_
