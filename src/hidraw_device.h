// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HIDRAW_DEVICE_H_
#define HIDRAW_DEVICE_H_

#include <string>

#include <base/files/file.h>
#include <base/functional/callback.h>

#include "hid_connection.h"
#include "hid_message.h"

namespace atrusctl {

class HIDRawDevice {
 public:
  enum QueryResult {
    kQueryError,
    kQuerySuccess,
    kQueryTimeout,
    kQueryUnknownResponse,
    kQueryNotValid,
  };

  HIDRawDevice();
  HIDRawDevice(const HIDRawDevice&) = delete;
  HIDRawDevice& operator=(const HIDRawDevice&) = delete;

  using QueryCompleteCallback =
      base::Callback<void(HIDRawDevice::QueryResult,
                          const HIDMessage& request,
                          const HIDMessage& response)>;
  using ConnectCallback = base::Callback<void(const HIDConnection& connection)>;

  bool OpenConnection(const ConnectCallback& callback);
  void Query(const uint16_t command, const QueryCompleteCallback& callback);

  void set_path(const std::string& path) { path_ = path; }

 private:
  void ExecuteQueryWrapper(const HIDMessage& request,
                           HIDMessage* response,
                           QueryResult* result,
                           const HIDConnection& connection);
  QueryResult ExecuteQuery(const HIDMessage& request,
                           HIDMessage* response,
                           const HIDConnection& connection);

  std::string path_;
};

}  // namespace atrusctl

#endif  // HIDRAW_DEVICE_H_
