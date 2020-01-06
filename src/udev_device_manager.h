// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UDEV_DEVICE_MANAGER_H_
#define UDEV_DEVICE_MANAGER_H_

#include <string>

#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/observer_list.h>

#include "scoped_udev_handle.h"
#include "udev_subsystem_observer.h"

namespace atrusctl {

class UdevDeviceManager : public base::MessagePumpLibevent::FdWatcher {
 public:
  UdevDeviceManager();

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
  // base::MessageLoopForIO::Watcher
  void OnFileCanReadWithoutBlocking(int fd) override;

  // base::MessageLoopForIO::Watcher
  void OnFileCanWriteWithoutBlocking(int fd) override;

  void HandleEvent(const std::string& action_str,
                   const std::string& device_path);

  ScopedUdev udev_;
  ScopedUdevMonitor monitor_;
  base::MessagePumpLibevent::FdWatchController watcher_;
  base::ObserverList<UdevSubsystemObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(UdevDeviceManager);
};

}  // namespace atrusctl

#endif  // UDEV_DEVICE_MANAGER_H_
