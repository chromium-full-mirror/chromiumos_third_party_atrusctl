// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_H_
#define DIAGNOSTICS_H_

#include <cstdint>
#include <vector>

#include <base/macros.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "hid_message.h"
#include "hidraw_device.h"

namespace atrusctl {

class Diagnostics {
 public:
  Diagnostics();

  void Start(const base::TimeDelta& diag_interval,
             const base::TimeDelta& ext_diag_interval,
             const std::string& device_path);
  void Stop();
  void UpdateNumberOfDevices(int new_value);

 private:
  void Run();
  void QueryDeviceWithCommands(const std::vector<uint16_t>& commands);
  void HandleQueryResult(HIDRawDevice::QueryResult code,
                         const HIDMessage& request,
                         const HIDMessage& response);
  void UpdateUptime(int new_value);
  void UpdateNumberOfConnectedSlaves(int new_value);
  void ResetSessionState();
  bool SessionStateHasChanged() const;

  const std::vector<uint16_t> commands_;
  const std::vector<uint16_t> commands_extended_;

  HIDRawDevice device_;

  // This timer determines how often Run() should be called, i.e. how often
  // diagnostics should be fetched from the device, after calling Start().
  base::RepeatingTimer diag_timer_;

  // Used to determine how often extended diagnostics should be run, i.e. how
  // often we also send the commands in |commands_extended_| to the device when
  // Run() is called.
  base::TimeDelta ext_diag_interval_;
  base::TimeTicks last_extended_diag_time_;

  // Below state is fetched from the device each time Run() is called. Whenever
  // a change in these values is detected on the device, we use that as a
  // trigger for doing an extended diagnose on the next call of Run() (same
  // effect as explained above for |ext_diag_interval_|).
  struct DiagnosticsValue {
    int value;
    bool changed;
  };
  DiagnosticsValue uptime_ = {0, false};
  DiagnosticsValue number_of_connected_slaves_ = {0, false};
  DiagnosticsValue number_of_connected_devices_ = {0, false};

  DISALLOW_COPY_AND_ASSIGN(Diagnostics);
};

}  // namespace atrusctl

#endif  // DIAGNOSTICS_H_
