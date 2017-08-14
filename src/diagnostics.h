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
  struct DiagCommand {
    uint16_t id;
    const char* description;
  };

  Diagnostics();

  void Start(const base::TimeDelta& diag_interval,
             const base::TimeDelta& ext_diag_interval,
             const std::string& device_path);
  void Stop();
  void UpdateNumberOfDevices(int new_value);

  int number_of_connected_slaves() const {
    return number_of_connected_slaves_;
  };

 private:
  void Run();
  void QueryDeviceWithCommands(const std::vector<DiagCommand>& commands);
  void Poll();
  void LogResponse(const std::string& cmd_desc, const HIDMessage& response);
  void LogQueryError(HIDRawDevice::QueryResult code, const HIDMessage& request);
  void HandleQueryResult(const std::string& cmd_desc,
                         HIDRawDevice::QueryResult code,
                         const HIDMessage& request,
                         const HIDMessage& response);
  void HandlePollQueryResult(const std::string& cmd_desc,
                             HIDRawDevice::QueryResult code,
                             const HIDMessage& request,
                             const HIDMessage& response);
  void UpdateUptime(int new_value);
  void UpdateNumberOfConnectedSlaves(int new_value);
  void ResetSessionState();
  bool SessionStateHasChanged() const;

  const std::vector<DiagCommand> commands_;
  const std::vector<DiagCommand> commands_extended_;
  const std::vector<DiagCommand> commands_poll_;
  const std::vector<DiagCommand> commands_truevoice_;

  HIDRawDevice device_;

  // Determines if the next call of Run() should perform an extended diagnose
  bool should_do_extended_diagnose_ = true;

  // This timer determines how often Run() should be called, i.e. how often
  // diagnostics should be fetched from the device, after calling Start().
  base::RepeatingTimer diag_timer_;

  // Used to determine how often extended diagnostics should be run per default,
  // i.e. how often we also send the commands in |commands_extended_| to the
  // device when Run() is called.
  base::TimeDelta ext_diag_interval_;
  base::TimeTicks last_extended_diag_time_;

  // Below state is fetched from the device each time Run() is called. Whenever
  // a certain change in these values is detected on the device, we flag for
  // doing an extended diagnose on the next call of Run() (same
  // effect as explained above for |ext_diag_interval_|).
  int uptime_ = 0;
  int number_of_connected_devices_ = 0;
  int number_of_connected_slaves_ = 0;
  std::string mic_selected_;
  std::string spk_selected_;
  std::string spk_silence_;
  std::string call_status_;

  base::RepeatingTimer poll_timer_;

  DISALLOW_COPY_AND_ASSIGN(Diagnostics);
};

}  // namespace atrusctl

#endif  // DIAGNOSTICS_H_
