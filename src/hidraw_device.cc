// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hidraw_device.h"

#include <functional>
#include <utility>
#include <vector>

#include <inttypes.h>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "atrus_device.h"
#include "util.h"

#ifndef __ANDROID__
#include <libudev.h>
#include "scoped_udev_handle.h"
#endif

namespace atrusctl {

namespace {

const int kMaxQueryReadRetries = 10;
const int kHidMaxOutputReportSizeBytes = 3;
const int kHidMaxInputReportSizeBytes = 61;
const uint8_t kHidReportId = 0x07;

}  // namespace

HIDRawDevice::HIDRawDevice() {}

bool HIDRawDevice::OpenConnection(ConnectCallback callback) {
  HIDConnection connection;
  if (!connection.Open(path_)) {
    return false;
  }
  std::move(callback).Run(connection);
  connection.Close();
  return true;
}

void HIDRawDevice::Query(const uint16_t command,
                         QueryCompleteCallback callback) {
  HIDRawDevice::QueryResult result;
  HIDMessage request(kHidReportId, command);
  HIDMessage response;
  bool ret = OpenConnection(
      base::BindOnce(&HIDRawDevice::ExecuteQueryWrapper, base::Unretained(this),
                     std::cref(request), &response, &result));
  if (!ret) {
    result = kQueryError;
  }

  std::move(callback).Run(result, request, response);
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
    bool timeout;
    if (!connection.WaitUntilReadable(&timeout)) {
      return (timeout ? kQueryTimeout : kQueryError);
    }

    std::vector<uint8_t> response_buffer(kHidMaxInputReportSizeBytes);
    if (!connection.Read(&response_buffer, kHidMaxInputReportSizeBytes) ||
        !response->UnpackFromBuffer(response_buffer)) {
      return kQueryError;
    }

    if (response->command_id() == 0x7FF0) {
      return kQueryNotValid;
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

}  // namespace atrusctl
