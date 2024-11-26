// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "dbus_adaptor.h"

namespace atrusctl {

DBusAdaptor::DBusAdaptor(AtrusControllerInterface* controller)
    : org::chromium::AtrusctlAdaptor(this), controller_(controller) {
  CHECK(controller_);
}

DBusAdaptor::~DBusAdaptor() {}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::ExportedObjectManager* object_manager,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  CHECK(!dbus_object_) << "Already registered";
  scoped_refptr<dbus::Bus> bus =
      object_manager ? object_manager->GetBus() : nullptr;

  dbus_object_.reset(new brillo::dbus_utils::DBusObject(
      object_manager, bus, dbus::ObjectPath(kAtrusctlPath)));

  RegisterWithDBusObject(dbus_object_.get());

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed.", true));
}

bool DBusAdaptor::ForceFirmwareUpgrade(brillo::ErrorPtr* error,
                                       const std::string& firmware_path,
                                       bool* success) {
  if (firmware_path.empty()) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kAtrusctlError, "firmware_path is empty");
    return false;
  }

  LOG(INFO) << "(DBus) Forcing upgrade with file: " << firmware_path;
  *success = controller_->UpgradeDeviceFirmware(base::FilePath(firmware_path),
                                                DeviceVariant::kUnknown, true);
  return true;
}

bool DBusAdaptor::EnableDiagnostics(brillo::ErrorPtr* error,
                                    int diag_interval,
                                    int ext_diag_interval) {
  LOG(INFO) << "(DBus) Enabling diagnostics with intervals:" << diag_interval
            << "/" << ext_diag_interval;

  if (diag_interval <= 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kAtrusctlError,
        "Argument |diag_interval| must be larger than 0");
    return false;
  }

  if (ext_diag_interval <= 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kAtrusctlError,
        "Argument |ext_diag_interval| must be larger than 0");
    return false;
  }

  controller_->EnableDiagnostics(base::Seconds(diag_interval),
                                 base::Seconds(ext_diag_interval));

  return true;
}

bool DBusAdaptor::DisableDiagnostics(brillo::ErrorPtr* error) {
  LOG(INFO) << "(DBus) Disabling diagnostics";
  controller_->DisableDiagnostics();
  return true;
}

}  // namespace atrusctl
