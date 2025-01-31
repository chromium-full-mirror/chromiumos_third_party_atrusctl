// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics.h"

#include <functional>
#include <string>
#include <vector>

#include <absl/strings/str_split.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "util.h"

namespace atrusctl {

namespace {

using QueryResult = HIDRawDevice::QueryResult;

const uint16_t kCommandDaisySlaves = 0x1351;
const uint16_t kCommandHookStatus = 0x1601;
const uint16_t kCommandStateUptime = 0x1604;
const uint16_t kCommandFbEndpointStat = 0x170B;

const Diagnostics::DiagCommand kCommands[] = {
    {0x1300, kCommandTypeExtended, "VERSION_MASTER"},     // VERSION_MASTER
    {0x1310, kCommandTypeExtended, "VERSION_SLAVE1_UP"},  // VERSION_SLAVE1_UP
    {0x1311, kCommandTypeExtended, "VERSION_SLAVE2_UP"},  // VERSION_SLAVE2_UP
    {0x1312, kCommandTypeExtended, "VERSION_SLAVE3_UP"},  // VERSION_SLAVE3_UP
    {0x1313, kCommandTypeExtended, "VERSION_SLAVE4_UP"},  // VERSION_SLAVE4_UP
    {0x1314, kCommandTypeExtended,
     "VERSION_SLAVE1_DOWN"},  // VERSION_SLAVE1_DOWN
    {0x1315, kCommandTypeExtended,
     "VERSION_SLAVE2_DOWN"},  // VERSION_SLAVE2_DOWN
    {0x1316, kCommandTypeExtended,
     "VERSION_SLAVE3_DOWN"},  // VERSION_SLAVE3_DOWN
    {0x1317, kCommandTypeExtended,
     "VERSION_SLAVE4_DOWN"},                              // VERSION_SLAVE4_DOWN
    {0x1350, kCommandTypeExtended, "DAISY_MIN_VERSION"},  // DAISY_MIN_VERSION
    {kCommandDaisySlaves, kCommandTypePoll,
     "Number of connected slaves"},             // DAISY_NUM_SLAVES
    {0x1352, kCommandTypePoll, "Daisy power"},  // DAISY_POWER
    {0x1500, kCommandTypePoll, "DFU status"},
    {0x1600, kCommandTypePoll, "Mute status"},
    {kCommandHookStatus, kCommandTypePoll, "Hook status"},
    {0x1602, kCommandTypePoll, "Volume"},      // STATE_VOLUME
    {0x1603, kCommandTypePoll, "LED status"},  // STATE_LED
    {kCommandStateUptime, kCommandTypeExtended,
     "Device uptime seconds"},                   // STATE_UPTIME
    {0x1605, kCommandTypePoll, "SHARC status"},  // STATE_SHARC_ALIVE
    {0x1700, kCommandTypeExtended, "HW_ID"},
    {0x1702, kCommandTypeExtended, "MIC_CALIBRATION"},        // MIC_CALIBRATION
    {0x1703, kCommandTypeExtended, "Active boot partition"},  // BOOT_PARTITION
    {0x1704, kCommandTypeExtended, "FGPA firmware version"},  // FGPA_FIRMWARE
    {0x1706, kCommandTypePoll, "Mic selected"},
    {0x1707, kCommandTypePoll, "Speaker selected"},
    {0x1708, kCommandTypePoll, "Speaker audio"},
    {0x1709, kCommandTypePoll,
     "Number of detected ASRC mem errors"},    // ASRC_FIFO_MEM_ERRORS
    {0x170A, kCommandTypePoll,
     "Maximum ASRC mem error size"},           // ASRC_FIFO_ERROR_SIZE
    {kCommandFbEndpointStat, kCommandTypePoll,
     "Feedback endpoint stats"},    // FEEDBACK_ENDPOINT_STATISTICS
    {0x5001, kCommandTypePoll,
     "SPI queue status"},           // SPI_QUEUE_STATUS
    {0x5002, kCommandTypePoll,
     "Touchpad status"},            // TOUCH_ANALOG_CALIBRATION_STATUS
    {0x5004, kCommandTypePoll,
     "Button presses"},             // NR_BUTTON_PRESSES

    // Truevoice commands
    {0x1800, kCommandTypeTruevoice, ""},  // TV_MIC_SELECTION
    {0x1801, kCommandTypeTruevoice, ""},  // TV_ERLE_MIN
    {0x1804, kCommandTypeTruevoice, ""},  // TV_ERLE_MAX
    {0x1805, kCommandTypeTruevoice, ""},  // TV_ERLE_MEAN
    {0x1807, kCommandTypeTruevoice, ""},  // TV_FEEDBACK_EST_MIN
    {0x1808, kCommandTypeTruevoice, ""},  // TV_FEEDBACK_EST_MAX
    {0x1809, kCommandTypeTruevoice, ""},  // TV_FEEDBACK_EST_MEAN
    {0x180B, kCommandTypeTruevoice, ""},  // TV_NR_MIN
    {0x180C, kCommandTypeTruevoice, ""},  // TV_NR_MAX
    {0x180D, kCommandTypeTruevoice, ""},  // TV_NR_MEAN
    {0x180F, kCommandTypeTruevoice, ""},  // TV_MIC_AVE_MIN
    {0x1810, kCommandTypeTruevoice, ""},  // TV_MIC_AVE_MAX
    {0x1811, kCommandTypeTruevoice, ""},  // TV_MIC_AVE_MEAN
    {0x1813, kCommandTypeTruevoice, ""},  // TV_LS_NOISE_MIN
    {0x1814, kCommandTypeTruevoice, ""},  // TV_LS_NOISE_MAX
    {0x1815, kCommandTypeTruevoice, ""},  // TV_LS_NOISE_MEAN
    {0x1817, kCommandTypeTruevoice, ""},  // TV_MIC_NOISE_MIN
    {0x1818, kCommandTypeTruevoice, ""},  // TV_MIC_NOISE_MAX
    {0x1819, kCommandTypeTruevoice, ""},  // TV_MIC_NOISE_MEAN
    {0x181B, kCommandTypeTruevoice, ""},  // TV_GAMMA_MIN
    {0x181C, kCommandTypeTruevoice, ""},  // TV_GAMMA_MAX
    {0x181D, kCommandTypeTruevoice, ""},  // TV_GAMMA_MEAN
    {0x181F, kCommandTypeTruevoice, ""},  // TV_LS_MIN
    {0x1820, kCommandTypeTruevoice, ""},  // TV_LS_MAX
    {0x1821, kCommandTypeTruevoice, ""},  // TV_LS_MEAN
    {0x1823, kCommandTypeTruevoice, ""},  // TV_TX_MIN
    {0x1824, kCommandTypeTruevoice, ""},  // TV_TX_MAX
    {0x1825, kCommandTypeTruevoice, ""},  // TV_TX_MEAN
    {0x1827, kCommandTypeTruevoice, ""},  // TV_TX_AGC_MIN
    {0x1828, kCommandTypeTruevoice, ""},  // TV_TX_AGC_MAX
    {0x1829, kCommandTypeTruevoice, ""},  // TV_TX_AGC_MEAN
    {0x182B, kCommandTypeTruevoice, ""},  // TV_TX_LIMITER_MIN
    {0x182C, kCommandTypeTruevoice, ""},  // TV_TX_LIMITER_MAX
    {0x182D, kCommandTypeTruevoice, ""},  // TV_TX_LIMITER_MEAN

};

// TODO(karl@limesaudio.com):
// Remove this when fixed in firmware
const int64_t kDiagDelay = 10;

const int kPollIntervalSeconds = 5;

// Feedback endpoint diagnostics log interval
// (Default log interval @5s when overflow/underflow occurs)
constexpr base::TimeDelta kFbEndpLogInterval = base::Minutes(10);

// Expected response when querying an Atrus device with the command
// |kCommandHookStatus| is either "Offhook" or "Onhook". We check for this
// string and assume the status is "Onhook" if the condition is false.
const char kCallOffHook[] = "Offhook";

// Because this is used to parse a string containing a number and trailing
// characters, check |new_value| rather than the return value of
// base::StringToInt to determine if a value was read.
bool ParseIntFromResponseBody(const std::string& str, int* val) {
  int new_value = -1;
  base::StringToInt(str, &new_value);
  if (new_value == -1) {
    LOG(ERROR) << "Failed to parse int from response: " << str;
    return false;
  }

  *val = new_value;
  return true;
}

// Helper function for extracting commands of a specific type from
// |kCommands|.
std::vector<Diagnostics::DiagCommand> GetDiagCommandsSubset(
    std::function<bool(const Diagnostics::DiagCommand&)> cmpFn) {
  std::vector<Diagnostics::DiagCommand> vec;
  std::copy_if(std::begin(kCommands), std::end(kCommands),
               std::back_inserter(vec), cmpFn);
  return vec;
}

}  // namespace

Diagnostics::Diagnostics()
    : commands_poll_(GetDiagCommandsSubset([](const DiagCommand& cmd) {
        return (cmd.type == kCommandTypePoll);
      })),
      commands_extended_(GetDiagCommandsSubset([](const DiagCommand& cmd) {
        return (cmd.type == kCommandTypeExtended) ||
               (cmd.type == kCommandTypePoll);
      })),
      commands_truevoice_(GetDiagCommandsSubset([](const DiagCommand& cmd) {
        return (cmd.type == kCommandTypeTruevoice);
      })) {
  ResetSessionState();
}

Diagnostics::~Diagnostics() {
  Stop();
}

void Diagnostics::Start(const base::TimeDelta& diag_interval,
                        const base::TimeDelta& ext_diag_interval,
                        const std::string& device_path) {
  if (diag_timer_.IsRunning()) {
    LOG(INFO) << "Diagnostics started while already running";
    diag_timer_.Stop();
    poll_timer_.Stop();
  }

  ResetSessionState();
  ext_diag_interval_ = ext_diag_interval;
  // Perform an extended diagnostic the first time
  last_extended_diag_time_ = base::TimeTicks();

  device_.set_path(device_path);

  poll_timer_.Start(
      FROM_HERE, base::Seconds(kPollIntervalSeconds),
      base::BindRepeating(&Diagnostics::Poll, base::Unretained(this),
                          commands_poll_));

  // Run a diagnostic immediately, then do it periodically
  Run();
  diag_timer_.Start(
      FROM_HERE, diag_interval,
      base::BindRepeating(&Diagnostics::Run, base::Unretained(this)));

  // TODO(karlpeterson@google.com): fetch all truevoice parameters on start
  // to reset internal state
}

void Diagnostics::Stop() {
  if (diag_timer_.IsRunning()) {
    diag_timer_.Stop();
  }
  if (poll_timer_.IsRunning()) {
    poll_timer_.Stop();
  }
}

void Diagnostics::Run() {
  const base::TimeTicks now = base::TimeTicks::Now();

  // If a call is active, log TrueVoice parameters
  if (diagnostic_values_[kCommandHookStatus].find(kCallOffHook) !=
      std::string::npos) {
    VLOG(1) << "Internal TrueVoice parameters: ";
    QueryDeviceWithCommands(commands_truevoice_);
  }

  if (now - last_extended_diag_time_ > ext_diag_interval_) {
    should_do_extended_diagnose_ = true;
  }

  if (should_do_extended_diagnose_) {
    should_do_extended_diagnose_ = false;
    last_extended_diag_time_ = now;
    LOG(INFO) << "-- Extended report --";
    QueryDeviceWithCommands(commands_extended_);
    LOG(INFO) << "Number of USB devices connected: "
              << number_of_connected_devices_;
  }
}

void Diagnostics::QueryDeviceWithCommands(
    const std::vector<DiagCommand>& commands) {
  for (const DiagCommand& command : commands) {
    device_.Query(command.id, base::BindOnce(&Diagnostics::HandleQueryResult,
                                             base::Unretained(this), command));
    // Workaround for firmware < v0.7.0
    // TODO(karl@limesaudio.com): Remove this when fixed in firmware
    // TODO(karl@limesaudio.com) Update: Due to issues with USB performance
    // for fast volume events, keep this until we can verify that it does not
    // stress the Atrus further
    TimeoutMs(kDiagDelay);
  }
}

void Diagnostics::Poll(const std::vector<DiagCommand>& commands) {
  for (const DiagCommand& command : commands) {
    device_.Query(command.id,
                  base::BindOnce(&Diagnostics::HandlePollQueryResult,
                                 base::Unretained(this), command));
    // Workaround for firmware < v0.7.0
    // TODO(karl@limesaudio.com): Remove this when fixed in firmware
    // TODO(karl@limesaudio.com) Update: Due to issues with USB performance
    // for fast volume events, keep this until we can verify that it does not
    // stress the Atrus further
    TimeoutMs(kDiagDelay);
  }
}

void Diagnostics::LogResponse(const std::string& cmd_desc,
                              const HIDMessage& response) {
  if (VLOG_IS_ON(1)) {
    LOG(INFO) << cmd_desc << " " << response.ToString();
  } else {
    LOG(INFO) << response.ToString();
  }
}

void Diagnostics::LogQueryError(QueryResult code, const HIDMessage& request) {
  switch (code) {
    case QueryResult::kQueryUnknownResponse:
      LOG(INFO) << base::StringPrintf("0x%04X: Error: unknown response",
                                      request.command_id());
      break;
    case QueryResult::kQueryError:
      LOG(INFO) << base::StringPrintf("0x%04X: Error: report failed",
                                      request.command_id());
      break;
    case QueryResult::kQueryNotValid:
      LOG(INFO) << base::StringPrintf(
          "0x%04X: Error: Command not supported by device.",
          request.command_id());
      break;
    case QueryResult::kQueryTimeout:
      LOG(INFO) << base::StringPrintf("0x%04X: Error: timeout",
                                      request.command_id());
      break;
    case QueryResult::kQuerySuccess:
    default:
      LOG(INFO) << base::StringPrintf("0x%04X: Unhandled error",
                                      request.command_id());
      break;
  }
}

bool Diagnostics::GetFeedbackEndpDiagnosticContents(
    const std::string& response_body,
    int *cb_overflow,
    int *cb_underflow) {
  int overflow = -1;
  int underflow = -1;

  if (response_body.empty()) {
    return false;
  }

  std::vector<std::string> fb_stats = absl::StrSplit(response_body, ' ');
  if (fb_stats.size() < 3) {
    return false;
  }

  std::vector<std::string> overflow_stat = absl::StrSplit(fb_stats[1], '=');
  std::vector<std::string> underflow_stat = absl::StrSplit(fb_stats[2], '=');
  if ((overflow_stat.size() < 2) || (underflow_stat.size() < 2)) {
    return false;
  }

  base::StringToInt(overflow_stat[1], &overflow);
  base::StringToInt(underflow_stat[1], &underflow);
  if (overflow == -1 || underflow == -1) {
    LOG(ERROR) << "Failed to parse int from feedback diags: "
               << overflow_stat[1] << ", " << underflow_stat[1];
    return false;
  }

  *cb_overflow = overflow;
  *cb_underflow = underflow;
  return true;
}

bool Diagnostics::HasFeedbackOverflowUnderflowValuesChanged(
    const int& cb_overflow_curr,
    const int& cb_underflow_curr) {
  int cb_overflow_prev, cb_underflow_prev;

  const std::string& cached_response_body =
      diagnostic_values_[kCommandFbEndpointStat];
  if (GetFeedbackEndpDiagnosticContents(cached_response_body,
                                        &cb_overflow_prev,
                                        &cb_underflow_prev)) {
    if ((cb_overflow_prev != cb_overflow_curr) ||
        (cb_underflow_prev != cb_underflow_curr)) {
      return true;
    }
  }
  return false;
}

bool Diagnostics::StoreFeedbackEndpDiagnosticResult(
    const std::string& response_body) {
  int cb_overflow, cb_underflow;

  if (GetFeedbackEndpDiagnosticContents(response_body,
                                        &cb_overflow,
                                        &cb_underflow)) {
    // Check for any overflow/underflow so we could log it right away.
    // If there were no changes in overflow/underflow values,
    // wait for 10 minutes from last logged time if we need to cache
    // and log again.
    if (HasFeedbackOverflowUnderflowValuesChanged(cb_overflow,
                                                  cb_underflow)) {
      diagnostic_values_[kCommandFbEndpointStat] = response_body;
      fb_endp_last_logged_time_ = base::TimeTicks::Now();
      return true;
    }
  }

  if (fb_endp_last_logged_time_.is_null()) {    // First poll
    diagnostic_values_[kCommandFbEndpointStat] = response_body;
    fb_endp_last_logged_time_ = base::TimeTicks::Now();
    return true;
  } else {
    // Check if 10 minutes have passed from previous log and
    // a change in values has been detected.
    if (((base::TimeTicks::Now() - fb_endp_last_logged_time_) >=
          kFbEndpLogInterval) &&
         (diagnostic_values_[kCommandFbEndpointStat] !=
          response_body)) {
      diagnostic_values_[kCommandFbEndpointStat] = response_body;
      fb_endp_last_logged_time_ = base::TimeTicks::Now();
      return true;
    }
  }

  return false;
}

bool Diagnostics::StoreDiagnosticResult(const DiagCommand& command,
                                        const HIDMessage& response) {
  bool value_changed = false;

  uint16_t response_id = response.command_id();
  if (diagnostic_values_.count(response_id) <= 0) {
    LOG(INFO) << base::StringPrintf(
        "0x%04X is not expected to have it's value cached (is it a non-poll "
        "command?)",
        response_id);
    return false;
  }

  const std::string& response_body = response.body();
  if ((diagnostic_values_[response_id] != response_body) &&
      (response_id != kCommandFbEndpointStat)) {
    diagnostic_values_[response_id] = response_body;
    value_changed = true;
  }

  int new_value;
  switch (response_id) {
    case kCommandStateUptime:
      if (ParseIntFromResponseBody(response_body, &new_value)) {
        UpdateUptime(new_value);
      }
      break;
    case kCommandDaisySlaves:
      if (ParseIntFromResponseBody(response_body, &new_value)) {
        UpdateNumberOfConnectedSlaves(new_value);
      }
      break;
    case kCommandFbEndpointStat:
      return StoreFeedbackEndpDiagnosticResult(response_body);
  }

  return value_changed;
}

void Diagnostics::HandleQueryResult(const DiagCommand& command,
                                    QueryResult code,
                                    const HIDMessage& request,
                                    const HIDMessage& response) {
  if (code != QueryResult::kQuerySuccess) {
    return LogQueryError(code, request);
  }

  if (command.type == kCommandTypePoll) {
    StoreDiagnosticResult(command, response);
  }

  LogResponse(command.description, response);
}

void Diagnostics::HandlePollQueryResult(const DiagCommand& command,
                                        QueryResult code,
                                        const HIDMessage& request,
                                        const HIDMessage& response) {
  // Only log these if their values changed since the last poll.
  if (code != QueryResult::kQuerySuccess) {
    return;  // don't log errors when we poll, that would bloat the log
  }
  if (StoreDiagnosticResult(command, response)) {
    LogResponse(command.description, response);
  }
}

void Diagnostics::UpdateNumberOfDevices(int new_value) {
  if (new_value != number_of_connected_devices_) {
    should_do_extended_diagnose_ = true;
  }
  number_of_connected_devices_ = new_value;
}

void Diagnostics::UpdateUptime(int new_value) {
  if (new_value < uptime_) {
    should_do_extended_diagnose_ = true;
  }
  uptime_ = new_value;
}

bool Diagnostics::UpdateNumberOfConnectedSlaves(int new_value) {
  if (new_value != number_of_connected_slaves_) {
    should_do_extended_diagnose_ = true;
    number_of_connected_slaves_ = new_value;
    return true;
  }
  return false;
}

void Diagnostics::ResetSessionState() {
  uptime_ = 0;
  number_of_connected_slaves_ = 0;
  // don't reset |number_of_connected_devices_|, it's updated on udev events

  for (const DiagCommand& cmd : commands_poll_) {
    diagnostic_values_[cmd.id] = "";
  }
}

}  // namespace atrusctl
