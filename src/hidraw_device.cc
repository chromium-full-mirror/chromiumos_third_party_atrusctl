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

const int kMaxQueryReadRetries = 10;
const int kHidMaxOutputReportSizeBytes = 3;
const int kHidMaxInputReportSizeBytes = 61;
const uint8_t kHidReportId = 0x07;

}  // namespace

HIDRawDevice::HIDRawDevice() {}

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
  bool ret = OpenConnection(
      base::Bind(&HIDRawDevice::ExecuteQueryWrapper, base::Unretained(this),
                 base::ConstRef(request), &response, &result));
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
    bool timeout;
    if (!connection.WaitUntilReadable(&timeout)) {
      return (timeout ? kQueryTimeout : kQueryError);
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

}  // namespace atrusctl
