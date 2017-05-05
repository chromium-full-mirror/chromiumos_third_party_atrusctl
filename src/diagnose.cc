// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnose.h"

#include <string>

#include <base/bind.h>
#include <base/callback.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "util.h"

namespace atrusctl {

namespace {

const uint16_t kCommandStateUptime = 0x1604;
const uint16_t kCommandDaisySlaves = 0x1351;

const uint16_t kCommands[] = {
    kCommandStateUptime,  // STATE_UPTIME
    kCommandDaisySlaves,  // DAISY_NUM_SLAVES
    0x1500,               // DAISY_DFU_STATUS
    0x1600,               // STATE_MUTED
    0x1601,               // STATE_HOOK_OFF
    0x1602,               // STATE_VOLUME
    0x1603,               // STATE_LED
    0x1605,               // STATE_SHARC_ALIVE
    0x1804,               // TV_ERLE_MAX
    0x1805,               // TV_ERLE_MEAN
    0x1806,               // TV_ERLE_VARIANCE
    0x1807,               // TV_FEEDBACK_EST_MIN
    0x1808,               // TV_FEEDBACK_EST_MAX
    0x1809,               // TV_FEEDBACK_EST_MEAN
    0x180A,               // TV_FEEDBACK_EST_VARIANCE
    0x180B,               // TV_NR_MIN
    0x180C,               // TV_NR_MAX
    0x180D,               // TV_NR_MEAN
    0x180E,               // TV_NR_VARIANCE
    0x180F,               // TV_MIC_AVE_MIN
    0x1810,               // TV_MIC_AVE_MAX
    0x1811,               // TV_MIC_AVE_MEAN
    0x1812,               // TV_MIC_AVE_VARIANCE
    0x1813,               // TV_LS_NOISE_MIN
    0x1814,               // TV_LS_NOISE_MAX
    0x1815,               // TV_LS_NOISE_MEAN
    0x1816,               // TV_LS_NOISE_VARIANCE
    0x1817,               // TV_MIC_NOISE_MIN
    0x1818,               // TV_MIC_NOISE_MAX
    0x1819,               // TV_MIC_NOISE_MEAN
    0x181A,               // TV_MIC_NOISE_VARIANCE
    0x181B,               // TV_GAMMA_MIN
    0x181C,               // TV_GAMMA_MAX
    0x181D,               // TV_GAMMA_MEAN
    0x181E,               // TV_GAMMA_VARIANCE
    0x5000,               // TOUCH_REGISTER_STATUS
    0x5001,               // SPI_QUEUE_STATUS
    0x5004,               // NR_BUTTON_PRESSES
};

const uint16_t kCommandsExtended[] = {
    0x1300,  // VERSION_MASTER
    0x1310,  // VERSION_SLAVE1_UP
    0x1311,  // VERSION_SLAVE2_UP
    0x1312,  // VERSION_SLAVE3_UP
    0x1313,  // VERSION_SLAVE4_UP
    0x1314,  // VERSION_SLAVE1_DOWN
    0x1315,  // VERSION_SLAVE2_DOWN
    0x1316,  // VERSION_SLAVE3_DOWN
    0x1317,  // VERSION_SLAVE4_DOWN
    0x1350,  // DAISY_MIN_VERSION
    0x1700,  // HW_ID
    0x1702,  // MIC_CALIBRATION
    0x1703,  // BOOT_PARTITION
    0x5002,  // TOUCH_ANALOG_CALIBRATION_STATUS
    0x5003,  // RF_NOISE_DETECTION
};

// TODO(karl@limesaudio.com): Remove this when fixed in firmware
const int64_t kDiagDelay = 10;

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

}  // namespace

Diagnostics::Diagnostics()
    : commands_(kCommands, kCommands + arraysize(kCommands)),
      commands_extended_(kCommandsExtended,
                         kCommandsExtended + arraysize(kCommandsExtended)) {}

bool Diagnostics::Initialize() {
  return device_.Initialize();
}

void Diagnostics::Run() {
  QueryDeviceWithCommands(commands_);
}

void Diagnostics::RunExtended() {
  QueryDeviceWithCommands(commands_extended_);
}

bool Diagnostics::SessionStateHasChanged() const {
  return uptime_.changed || number_of_connected_slaves_.changed;
}

void Diagnostics::QueryDeviceWithCommands(
    const std::vector<uint16_t>& commands) {
  for (const uint16_t command : commands) {
    device_.Query(
        command,
        base::Bind(&Diagnostics::HandleQueryResult, base::Unretained(this)));
    // Workaround for firmware < v0.7.0
    // TODO(karl@limesaudio.com): Remove this when fixed in firmware
    TimeoutMs(kDiagDelay);
  }
}

void Diagnostics::HandleQueryResult(HIDRawDevice::QueryResult code,
                                    const HIDMessage& request,
                                    const HIDMessage& response) {
  switch (code) {
    case HIDRawDevice::QueryResult::kQueryUnknownResponse:
      LOG(INFO) << base::StringPrintf("0x%04X: Error: unknown response",
                                      request.command_id());
      break;
    case HIDRawDevice::QueryResult::kQueryError:
      LOG(INFO) << base::StringPrintf("0x%04X: Error: report failed",
                                      request.command_id());
      break;
    case HIDRawDevice::QueryResult::kQueryTimeout:
      LOG(INFO) << base::StringPrintf("0x%04X: Error: timeout",
                                      request.command_id());
      break;
    case HIDRawDevice::QueryResult::kQuerySuccess: {
      LOG(INFO) << response.ToString();

      int new_value;
      switch (response.command_id()) {
        case kCommandStateUptime:
          if (ParseIntFromResponseBody(response.body(), &new_value)) {
            UpdateUptime(new_value);
          }
          break;
        case kCommandDaisySlaves:
          if (ParseIntFromResponseBody(response.body(), &new_value)) {
            UpdateNumberOfConnectedSlaves(new_value);
          }
          break;
      }
      break;
    }
  }
}

void Diagnostics::UpdateUptime(int new_value) {
  uptime_.changed = (new_value < uptime_.value);
  uptime_.value = new_value;
}

void Diagnostics::UpdateNumberOfConnectedSlaves(int new_value) {
  number_of_connected_slaves_.changed =
      (new_value != number_of_connected_slaves_.value);
  number_of_connected_slaves_.value = new_value;
}

}  // namespace atrusctl
