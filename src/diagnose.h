// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSE_H_
#define DIAGNOSE_H_

#include <cstdint>
#include <vector>

#include "hid_message.h"
#include "hidraw_device.h"

namespace atrusctl {

class Diagnostics {
 public:
  Diagnostics();

  bool Initialize();
  void Run();
  void RunExtended();

  bool SessionStateHasChanged() const;

 private:
  void QueryDeviceWithCommands(const std::vector<uint16_t>& commands);
  void HandleQueryResult(HIDRawDevice::QueryResult code,
                         const HIDMessage& request,
                         const HIDMessage& response);

  void UpdateUptime(int new_value);
  void UpdateNumberOfConnectedSlaves(int new_value);

  std::vector<uint16_t> commands_;
  std::vector<uint16_t> commands_extended_;

  // Only handle one device for now
  HIDRawDevice device_;

  struct DiagnosticsValue {
    int value;
    bool changed;
  };
  DiagnosticsValue uptime_ = {0, false};
  DiagnosticsValue number_of_connected_slaves_ = {0, false};
};

}  // namespace atrusctl

#endif  // DIAGNOSE_H_
