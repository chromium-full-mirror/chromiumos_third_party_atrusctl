// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "udev_device_manager.h"

#include <inttypes.h>
#include <libudev.h>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "atrus_device.h"

namespace atrusctl {

namespace {

enum class UdevAction {
  kAdd,
  kRemove,
  kChange,
  kOnline,
  kOffline,
  kUnknown,
};

// Watch for udev events matching this tag
const char kAtrusTag[] = "atrus";

UdevAction StringToAction(const std::string& action_str) {
  if (action_str == "add") {
    return UdevAction::kAdd;
  }
  if (action_str == "remove") {
    return UdevAction::kRemove;
  }
  if (action_str == "change") {
    return UdevAction::kChange;
  }
  if (action_str == "online") {
    return UdevAction::kOnline;
  }
  if (action_str == "offline") {
    return UdevAction::kOffline;
  }
  return UdevAction::kUnknown;
}

// Because this is used to parse a string containing a number with trailing
// characters, check |new_value| rather then the return value of
// base::HexStringToUInt to determine if a value was read. We assume that the
// string read does not contain the number 0.
bool ParseUintFromSysattr(const std::string& str, uint32_t* val) {
  uint32_t new_value = 0;
  base::HexStringToUInt(str, &new_value);
  if (new_value == 0) {
    LOG(ERROR) << "Failed to parse int from string: " << str;
    return false;
  }

  *val = new_value;
  return true;
}

// Uses udev_device_get_sysattr_value() to check that |device| has vid/pid
// that matches the expected vid/pid of Atrus or Orion, defined in
// atrus_device.h
DeviceVariant VerifyDeviceVidPid(udev_device* device) {
  uint32_t id_vendor, id_product;
  if (!ParseUintFromSysattr(udev_device_get_sysattr_value(device, "idVendor"),
                            &id_vendor)) {
    return DeviceVariant::kUnknown;
  }
  if (!ParseUintFromSysattr(udev_device_get_sysattr_value(device, "idProduct"),
                            &id_product)) {
    return DeviceVariant::kUnknown;
  }
  if ((id_vendor == kAtrusUsbVid) && (id_product == kAtrusUsbPid)) {
    return DeviceVariant::kAtrus;
  }

  if ((id_vendor == kOrionUsbVid) && (id_product == kOrionUsbPid)) {
    return DeviceVariant::kOrion;
  }
  return DeviceVariant::kUnknown;
}

}  // namespace

UdevDeviceManager::UdevDeviceManager()
    : udev_(udev_new()),
      monitor_(udev_monitor_new_from_netlink(udev_.get(), "udev")) {
  CHECK(udev_.get()) << "udev_new() returned nullptr";
  CHECK(monitor_.get()) << "udev_monitor_new_from_netlink() returned nullptr";
}

bool UdevDeviceManager::Initialize() {
  int retval = udev_monitor_filter_add_match_tag(monitor_.get(), kAtrusTag);
  if (retval != 0) {
    LOG(ERROR) << "udev_monitor_filter_add_match_tag() returned " << retval;
    return false;
  }

  retval = udev_monitor_enable_receiving(monitor_.get());
  if (retval != 0) {
    VLOG(1) << "udev_monitor_enable_receiving() returned " << retval;
    return false;
  }

  int fd = udev_monitor_get_fd(monitor_.get());
  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&UdevDeviceManager::OnFdReadable,
                              base::Unretained(this)));
  if (!watcher_) {
    LOG(ERROR) << "Unable to watch FD " << fd;
    return false;
  }

  VLOG(2) << base::StringPrintf("Watching FD %d for udev events with tag: %s",
                                fd, kAtrusTag);

  return true;
}

void UdevDeviceManager::AddObserver(UdevSubsystemObserver* observer) {
  observers_.AddObserver(observer);
}

void UdevDeviceManager::RemoveObserver(UdevSubsystemObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool UdevDeviceManager::Enumerate() {
  ScopedUdevEnumerate enumerate(udev_enumerate_new(udev_.get()));
  if (!enumerate) {
    LOG(ERROR) << "udev_enumerate_new() returned nullptr";
    return false;
  }

  udev_enumerate_add_match_subsystem(enumerate.get(), "hidraw");
  udev_enumerate_scan_devices(enumerate.get());

  udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate.get());
  if (!devices) {
    VLOG(1) << "udev_enumerate_get_list_entry() returned empty list";
    return false;
  }
  udev_list_entry* dev_list_entry;
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char* path = udev_list_entry_get_name(dev_list_entry);
    ScopedUdevDevice device(udev_device_new_from_syspath(udev_.get(), path));
    if (!device) {
      PLOG(ERROR) << "udev_device_new_from_syspath()";
      continue;
    }

    // Actual path to hidraw device
    const char* dev_node_path = udev_device_get_devnode(device.get());

    // |parent| does not need a ScopedUdevHandle, since it is owned and unrefd
    // internally by |device|.
    udev_device* parent = udev_device_get_parent_with_subsystem_devtype(
        device.get(), "usb", "usb_device");
    if (parent) {
      DeviceVariant device_variant = VerifyDeviceVidPid(parent);
      if (device_variant != DeviceVariant::kUnknown) {
        HandleEvent("add", dev_node_path, device_variant);
      }
    }
  }

  return true;
}

void UdevDeviceManager::OnFdReadable() {
  ScopedUdevDevice device(udev_monitor_receive_device(monitor_.get()));
  if (!device) {
    LOG(WARNING) << "Ignore device event with no associated udev device.";
    return;
  }

  const char* dev_node_path = udev_device_get_devnode(device.get());
  const char* action_str = udev_device_get_action(device.get());

  VLOG(1) << base::StringPrintf(
      "Received event: dev_node_path=%s action_str=%s", dev_node_path,
      action_str);

  udev_device* parent = udev_device_get_parent_with_subsystem_devtype(
      device.get(), "usb", "usb_device");
  if (parent) {
    HandleEvent(action_str, dev_node_path, VerifyDeviceVidPid(parent));
  }
}

void UdevDeviceManager::HandleEvent(const std::string& action_str,
                                    const std::string& device_path,
                                    DeviceVariant device_variant) {
  UdevAction action = StringToAction(action_str);
  switch (action) {
    case UdevAction::kAdd:
      for (auto& observer : observers_)
        observer.OnDeviceAdded(device_path, device_variant);
      break;
    case UdevAction::kRemove:
      for (auto& observer : observers_)
        observer.OnDeviceRemoved(device_path);
      break;
    default:
      VLOG(1) << "Unhandled udev event received for tag '" << kAtrusTag
              << "': " << action_str;
      break;
  }
}

}  // namespace atrusctl
