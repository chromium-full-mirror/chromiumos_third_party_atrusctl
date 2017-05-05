// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hidraw_device.h"

#include <vector>

#include <inttypes.h>
#include <libudev.h>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "atrus_device.h"
#include "scoped_udev_handle.h"
#include "util.h"

namespace atrusctl {

namespace {

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

bool VerifyDeviceVidPid(udev_device* device) {
  uint32_t id_vendor = 0, id_product = 0;
  ParseUintFromSysattr(udev_device_get_sysattr_value(device, "idVendor"),
                       &id_vendor);
  ParseUintFromSysattr(udev_device_get_sysattr_value(device, "idProduct"),
                       &id_product);
  if ((id_vendor == 0) || (id_product == 0) || (id_vendor != kAtrusUsbVid) ||
      (id_product != kAtrusUsbPid)) {
    return false;
  }
  return true;
}

const int kMaxQueryReadRetries = 10;
const int kHidMaxOutputReportSizeBytes = 3;
const int kHidMaxInputReportSizeBytes = 61;
const uint8_t kHidReportId = 0x07;

}  // namespace

HIDRawDevice::HIDRawDevice() {}

bool HIDRawDevice::Initialize() {
  return GetDevicePath();
}

bool HIDRawDevice::OpenConnection(const ConnectCallback& callback) {
  HIDConnection connection;
  if (!connection.Open(path_)) {
    return false;
  }
  callback.Run(connection);
  connection.Close();
  return true;
}

void HIDRawDevice::Query(const uint16_t command,
                         const QueryCompleteCallback& callback) {
  HIDRawDevice::QueryResult result;
  HIDMessage request(kHidReportId, command);
  HIDMessage response;
  bool ret = OpenConnection(base::Bind(&HIDRawDevice::ExecuteQueryWrapper,
                                       base::Unretained(this),
                                       base::ConstRef(request),
                                       &response,
                                       &result));
  if (!ret) {
    result = kQueryError;
  }

  callback.Run(result, request, response);
}

void HIDRawDevice::ExecuteQueryWrapper(const HIDMessage& request,
                                       HIDMessage* response,
                                       HIDRawDevice::QueryResult* result,
                                       const HIDConnection& connection) {
  *result = ExecuteQuery(request, response, connection);
}

HIDRawDevice::QueryResult HIDRawDevice::ExecuteQuery(
    const HIDMessage& request,
    HIDMessage* response,
    const HIDConnection& connection) {
  // Write request to HID device
  std::vector<uint8_t> request_buffer(kHidMaxOutputReportSizeBytes);
  if (!request.PackIntoBuffer(&request_buffer) ||
      !connection.Write(request_buffer)) {
    return kQueryError;
  }

  // Read response from HID device, try |kMaxQueryReadRetries| times
  for (int retry = 0; retry < kMaxQueryReadRetries; retry++) {
    if (!connection.WaitUntilReadable()) {
      // Connection timed out, i.e. the device did not respond with any data
      return kQueryTimeout;
    }

    std::vector<uint8_t> response_buffer(kHidMaxInputReportSizeBytes);
    if (!connection.Read(&response_buffer, kHidMaxInputReportSizeBytes) ||
        !response->UnpackFromBuffer(response_buffer)) {
      return kQueryError;
    }

    // Check if response header matches request
    if (response->Validate(request)) {
      return kQuerySuccess;
    }

    // Got unknown response, retry read
  }

  // Reached |kMaxQueryReadRetries|, the device only responded with unknown data
  return kQueryUnknownResponse;
}

bool HIDRawDevice::GetDevicePath() {
  ScopedUdev udev(udev_new());
  if (!udev) {
    LOG(ERROR) << "udev_new() returned nullptr";
    return false;
  }
  ScopedUdevEnumerate enumerate(udev_enumerate_new(udev.get()));
  if (!enumerate) {
    LOG(ERROR) << "udev_enumerate_new() returned nullptr";
    return false;
  }

  udev_enumerate_add_match_subsystem(enumerate.get(), "hidraw");
  udev_enumerate_scan_devices(enumerate.get());

  udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate.get());
  if (!devices) {
    LOG(INFO) << "udev_enumerate_get_list_entry() returned empty list";
    return false;
  }
  udev_list_entry* dev_list_entry;
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char* path = udev_list_entry_get_name(dev_list_entry);
    ScopedUdevDevice device(udev_device_new_from_syspath(udev.get(), path));
    if (!device) {
      PLOG(ERROR) << "udev_device_new_from_syspath()";
      continue;
    }

    // Actual path to hidraw device
    const char* dev_node_path = udev_device_get_devnode(device.get());

    // |parent| does not need a ScopedUdevHandle, since it is owned and unrefd
    // internally by |device|.
    udev_device* parent;
    parent = udev_device_get_parent_with_subsystem_devtype(
        device.get(), "usb", "usb_device");
    if ((parent != nullptr) && VerifyDeviceVidPid(parent)) {
      path_ = dev_node_path;
      return true;
    }
  }

  return false;
}

}  // namespace atrusctl
