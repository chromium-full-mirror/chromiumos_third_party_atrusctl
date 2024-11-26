// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UDEV_DEVICE_MANAGER_H_
#define UDEV_DEVICE_MANAGER_H_

#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/observer_list.h>

#include "atrus_device.h"
#include "scoped_udev_handle.h"
#include "udev_subsystem_observer.h"

namespace atrusctl {

class UdevDeviceManager {
 public:
  UdevDeviceManager();
  UdevDeviceManager(const UdevDeviceManager&) = delete;
  UdevDeviceManager& operator=(const UdevDeviceManager&) = delete;

  // Initialize udev monitoring and start listening on udev socket
  bool Initialize();

  // Add listener for hidraw events that matches rule in "udev-atrus.rules"
  void AddObserver(UdevSubsystemObserver* observer);

  // Remove listener
  void RemoveObserver(UdevSubsystemObserver* observer);

  // Enumerate hidraw devices that matches |kUsbVid| and |kUSbPid| declared in
  // atrus_device.h, call each observer's callback if a device was found
  bool Enumerate();

 private:
  void OnFdReadable();

  void HandleEvent(const std::string& action_str,
                   const std::string& device_path,
                   DeviceVariant device_variant);

  ScopedUdev udev_;
  ScopedUdevMonitor monitor_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  base::ObserverList<UdevSubsystemObserver> observers_;
};

}  // namespace atrusctl

#endif  // UDEV_DEVICE_MANAGER_H_
