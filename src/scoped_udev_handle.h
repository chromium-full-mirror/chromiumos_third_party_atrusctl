// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCOPED_UDEV_HANDLE_H_
#define SCOPED_UDEV_HANDLE_H_

#include <libudev.h>

namespace atrusctl {

// This class takes a newly created udev object and handles unref:ing them when
// going out of scope. Candiates are pointers to udev, udev_enumerate and
// udev_device, created by the libudev.h functions udev_new(),
// udev_enumerate_new() and the family of udev_device_new_...() functions.
template <typename T, T* (*UnrefFunc)(T*)>
class ScopedUdevHandle {
 public:
  explicit ScopedUdevHandle(T* obj) : obj_(obj) {}
  ScopedUdevHandle(const ScopedUdevHandle&) = delete;
  ScopedUdevHandle& operator=(const ScopedUdevHandle&) = delete;

  ~ScopedUdevHandle() {
    if (obj_)
      UnrefFunc(obj_);
  }

  T* get() const { return obj_; }
  operator bool() const { return obj_; }

 private:
  T* obj_;
};

using ScopedUdev = ScopedUdevHandle<struct udev, udev_unref>;
using ScopedUdevEnumerate =
    ScopedUdevHandle<struct udev_enumerate, udev_enumerate_unref>;
using ScopedUdevDevice =
    ScopedUdevHandle<struct udev_device, udev_device_unref>;
using ScopedUdevMonitor =
    ScopedUdevHandle<struct udev_monitor, udev_monitor_unref>;

}  // namespace atrusctl

#endif  // SCOPED_UDEV_HANDLE_H_
