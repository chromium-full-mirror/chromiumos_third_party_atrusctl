// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <brillo/daemons/daemon.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "atrus_controller.h"
#include "dbus_adaptor.h"
#include "udev_device_manager.h"
#include "util.h"

namespace {

const char kDaemonName[] = "atrusd";

}  // namespace

namespace atrusctl {

class Atrusd : public brillo::DBusServiceDaemon {
 public:
  Atrusd(const base::FilePath& atrus_firmware_path,
         const base::FilePath& orion_firmware_path)
      : DBusServiceDaemon(kAtrusctlName, dbus::ObjectPath(kAtrusctlObjectPath)),
        controller_(
            new AtrusController(atrus_firmware_path, orion_firmware_path)) {}
  Atrusd(const Atrusd&) = delete;
  Atrusd& operator=(const Atrusd&) = delete;

 private:
  // brillo::Daemon
  int OnInit() override {
    LOG(INFO) << "Starting " << kDaemonName;

    if (!device_manager_.Initialize()) {
      LOG(ERROR) << "Could not initialize device manager";
      return 1;
    }

    device_manager_.AddObserver(controller_.get());

    // Enumerate in case an Atrus device was connected before we started
    device_manager_.Enumerate();

    return brillo::DBusServiceDaemon::OnInit();
  }

  // brillo::Daemon
  void OnShutdown(int* exit_code) override {
    LOG(INFO) << "Exiting " << kDaemonName;

    device_manager_.RemoveObserver(controller_.get());
    dbus_adaptor_.reset();
    brillo::DBusServiceDaemon::OnShutdown(exit_code);
  }

  // brillo::DBusServiceDaemon
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    dbus_adaptor_.reset(new DBusAdaptor(controller_.get()));
    dbus_adaptor_->RegisterAsync(object_manager_.get(), sequencer);
  }

  UdevDeviceManager device_manager_;
  std::unique_ptr<DBusAdaptor> dbus_adaptor_;
  std::unique_ptr<AtrusController> controller_;
};

}  // namespace atrusctl

int main(int argc, char* argv[]) {
  DEFINE_string(atrus_upgrade_file_path, "",
                "Path to Atrus firmware upgrade file");
  DEFINE_string(orion_upgrade_file_path, "",
                "Path to Orion firmware upgrade file");
  brillo::FlagHelper::Init(argc, argv, kDaemonName);
  CHECK(!(FLAGS_atrus_upgrade_file_path.empty() &&
          FLAGS_orion_upgrade_file_path.empty()))
      << "at least one upgrade file is required";

  brillo::InitLog(brillo::InitFlags::kLogToSyslog);

  atrusctl::Atrusd atrusd((base::FilePath(FLAGS_atrus_upgrade_file_path)),
                          (base::FilePath(FLAGS_orion_upgrade_file_path)));

  return atrusd.Run();
}
