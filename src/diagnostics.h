// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_H_
#define DIAGNOSTICS_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <base/time/time.h>
#include <base/timer/timer.h>

#include "hid_message.h"
#include "hidraw_device.h"

namespace atrusctl {

enum DiagCommandType {
  kCommandTypeExtended,
  kCommandTypePoll,
  kCommandTypeTruevoice,
};

class Diagnostics {
 public:
  struct DiagCommand {
    uint16_t id;
    DiagCommandType type;
    const char* description;
  };

  Diagnostics();
  Diagnostics(const Diagnostics&) = delete;
  Diagnostics& operator=(const Diagnostics&) = delete;

  ~Diagnostics();

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
  void Poll(const std::vector<DiagCommand>& commands);
  void QueryDeviceWithCommands(const std::vector<DiagCommand>& commands);
  void LogResponse(const std::string& cmd_desc, const HIDMessage& response);
  void LogQueryError(HIDRawDevice::QueryResult code, const HIDMessage& request);
  void HandleQueryResult(const DiagCommand& command,
                         HIDRawDevice::QueryResult code,
                         const HIDMessage& request,
                         const HIDMessage& response);
  // Store the value from a diagnostic response and update internal state if
  // |number_of_connected_slaves_| or |uptime_| changed
  void HandlePollQueryResult(const DiagCommand& command,
                             HIDRawDevice::QueryResult code,
                             const HIDMessage& request,
                             const HIDMessage& response);
  bool StoreDiagnosticResult(const DiagCommand& command,
                             const HIDMessage& response);
  void UpdateUptime(int new_value);
  // Returns true if |number_of_connected_slaves_| was set to |new_value|
  bool UpdateNumberOfConnectedSlaves(int new_value);
  void ResetSessionState();
  bool SessionStateHasChanged() const;

  bool GetFeedbackEndpDiagnosticContents(const std::string& response_body,
                                         int *cb_overflow,
                                         int *cb_underflow);
  bool HasFeedbackOverflowUnderflowValuesChanged(const int& cb_overflow_curr,
                                                 const int& cb_underflow_curr);
  bool StoreFeedbackEndpDiagnosticResult(const std::string& response_body);

  const std::vector<DiagCommand> commands_poll_;
  const std::vector<DiagCommand> commands_extended_;
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

  base::RepeatingTimer poll_timer_;

  // Stores the latest values of some diagnostics. When a change in these
  // values is detected, the updated value is written to syslog.
  std::map<uint16_t, std::string> diagnostic_values_;

  // How often feedback endpoint diagnostics should be logged.
  // Polling/logging interval is 5s by default for all commands
  // that are of poll type.
  // For feedback endpoint diagnostics, only log results
  // for every 10 minutes and when a change in values is detected,
  // or when there's overflow/underflow to avoid bloating the log file.
  base::TimeTicks fb_endp_last_logged_time_;
};

}  // namespace atrusctl

#endif  // DIAGNOSTICS_H_
