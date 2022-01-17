// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_ADAPTOR_H_
#define DBUS_ADAPTOR_H_

#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>

#include "atrusctl/dbus_adaptors/org.chromium.Atrusctl.h"
#include "atrus_controller.h"

namespace atrusctl {

// TODO(karl@limesaudio.com): move these to system_api later
const char kAtrusctlError[] = "org.chromium.Atrusctl.Error";
const char kAtrusctlPath[] = "/org/chromium/Atrusctl";
const char kAtrusctlObjectPath[] = "/org/chromium/Atrusctl/ObjectManager";
const char kAtrusctlName[] = "org.chromium.Atrusctl";

// Exposes an Atrusctl interface over DBus. Recieves messages and performs
// various actions via |controller_|.
class DBusAdaptor : public org::chromium::AtrusctlAdaptor,
                    public org::chromium::AtrusctlInterface {
 public:
  // DBusAdaptor does not own |controller| and will not delete it
  explicit DBusAdaptor(AtrusControllerInterface* controller);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  virtual ~DBusAdaptor();

  void RegisterAsync(brillo::dbus_utils::ExportedObjectManager* object_manager,
                     brillo::dbus_utils::AsyncEventSequencer* sequencer);

  // Initiates a firmware upgrade for the current Atrus device with the file
  // specified in |firmware_path|. The upgrade will start even if the supplied
  // firmware is older than what is already on the device.
  bool ForceFirmwareUpgrade(brillo::ErrorPtr* error,
                            const std::string& firmware_path,
                            bool* success) override;

  bool EnableDiagnostics(brillo::ErrorPtr* error,
                         int diag_interval,
                         int ext_diag_interval) override;

  bool DisableDiagnostics(brillo::ErrorPtr* error) override;

 private:
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  AtrusControllerInterface* controller_;  // Not owned
};

}  // namespace atrusctl

#endif  // DBUS_ADAPTOR_H_
