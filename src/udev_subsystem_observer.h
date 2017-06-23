// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UDEV_SUBSYSTEM_OBSERVER_H_
#define UDEV_SUBSYSTEM_OBSERVER_H_

#include <string>

namespace atrusctl {

class UdevSubsystemObserver {
 public:
  virtual ~UdevSubsystemObserver() = default;

  virtual void OnDeviceAdded(const std::string& device_path) = 0;
  virtual void OnDeviceRemoved(const std::string& device_path) = 0;
};

}  // namespace atrusctl

#endif  // UDEV_SUBSYSTEM_OBSERVER_H_
