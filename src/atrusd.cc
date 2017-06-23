// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/files/file_path.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "atrus_device.h"
#include "diagnostics.h"
#include "udev_device_manager.h"
#include "upgrade.h"
#include "util.h"

namespace {

const char kDaemonName[] = "atrusd";

}  // namespace

namespace atrusctl {

class Atrusd : public brillo::Daemon, public UdevSubsystemObserver {
 public:
  Atrusd(const base::FilePath& firmware_path,
         const base::TimeDelta& diag_interval,
         const base::TimeDelta& ext_diag_interval)
      : firmware_path_(firmware_path),
        diag_interval_(diag_interval),
        ext_diag_interval_(ext_diag_interval) {}

 private:
  // brillo::Daemon
  int OnInit() override {
    LOG(INFO) << "Starting " << kDaemonName;

    if (!device_manager_.Initialize()) {
      LOG(ERROR) << "Could not initialize device manager";
      return 1;
    }

    device_manager_.AddObserver(this);

    // Enumerate in case an Atrus device was connected before we started
    device_manager_.Enumerate();

    return brillo::Daemon::OnInit();
  }

  // brillo::Daemon
  void OnShutdown(int* exit_code) override { DisableDiagnostics(); }

  // UdevSubsystemObserver
  void OnDeviceAdded(const std::string& device_path) override {
    bool skipped;
    bool ret = PerformUpgrade(firmware_path_, &skipped);
    LOG(INFO) << "Upgrade "
              << (ret ? (skipped ? "skipped" : "succeeded") : "failed");

    EnableDiagnostics(device_path);
  }

  // UdevSubsystemObserver
  void OnDeviceRemoved(const std::string& device_path) override {
    DisableDiagnostics();
  }

  void EnableDiagnostics(const std::string& device_path) {
    LOG(INFO) << "Polling diagnostics every " << diag_interval_;

    diagnostics_.Start(diag_interval_, ext_diag_interval_, device_path);
  }

  void DisableDiagnostics() {
    LOG(INFO) << "Diagnostics disabled";

    diagnostics_.Stop();
  }

  UdevDeviceManager device_manager_;
  Diagnostics diagnostics_;
  base::FilePath firmware_path_;
  base::TimeDelta diag_interval_;
  base::TimeDelta ext_diag_interval_;

  DISALLOW_COPY_AND_ASSIGN(Atrusd);
};

}  // namespace atrusctl

int main(int argc, char* argv[]) {
  DEFINE_string(upgrade_file_path, "", "Path to firmware upgrade file");
  DEFINE_string(diag_interval, "60", "Polling interval for diagnostics (s)");
  DEFINE_string(ext_diag_interval,
                "1800",
                "Polling interval for extended diagnostics (s)");

  brillo::InitLog(brillo::InitFlags::kLogToSyslog);

  brillo::FlagHelper::Init(argc, argv, kDaemonName);
  CHECK(!FLAGS_upgrade_file_path.empty()) << "--upgrade_file_path is required";

  unsigned int diag_interval_s = 0, ext_diag_interval_s = 0;
  base::StringToUint(FLAGS_diag_interval, &diag_interval_s);
  base::StringToUint(FLAGS_ext_diag_interval, &ext_diag_interval_s);
  if (diag_interval_s == 0 || ext_diag_interval_s == 0) {
    LOG(WARNING) << "diag_interval and ext_diag_interval needs to be an "
                 << "integer larger than 0";
    return EX_USAGE;
  }

  atrusctl::Atrusd atrusd(base::FilePath(FLAGS_upgrade_file_path),
                          base::TimeDelta::FromSeconds(diag_interval_s),
                          base::TimeDelta::FromSeconds(ext_diag_interval_s));
  return atrusd.Run();
}
