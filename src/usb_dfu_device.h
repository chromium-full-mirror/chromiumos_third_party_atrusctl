// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USB_DFU_DEVICE_H_
#define USB_DFU_DEVICE_H_

#include "usb_device.h"

#include <string>
#include <vector>

namespace atrusctl {

// Implements USB DFU Firmware Upgrade Specification, Revision 1.1
// http://www.usb.org/developers/docs/devclass_docs/DFU_1.1.pdf
class UsbDfuDevice : public UsbDevice {
 public:
  // From Section 6.1.2 in the specification
  enum State {
    kStateAppIdle = 0x0,
    kStateAppDetach = 0x1,
    kStateDfuIdle = 0x2,
    kStateDfuDnloadSync = 0x3,
    kStateDfuDnbusy = 0x4,
    kStateDfuDnloadIdle = 0x5,
    kStateDfuManifestSync = 0x6,
    kStateDfuManifest = 0x7,
    kStateDfuManifestWaitReset = 0x8,
    kStateDfuUploadIdle = 0x9,
    kStateDfuError = 0xA,
  };

  // From Table 4.2 in the specification
  enum FunctionalDescriptorAttributes {
    kCanDnload = (1 << 0),
    kCanUpload = (1 << 1),
    kManifestationTolerant = (1 << 2),
    kWillDetach = (1 << 3),
  };

  // From Table 4.2 in the specification
  struct FunctionalDescriptor {
    uint8_t length;
    uint8_t type;
    uint8_t attributes;
    uint16_t detach_timeout;
    uint16_t transfer_size;
    uint16_t dfu_version;
  };

  struct StatusRequest {
    uint8_t status;
    uint32_t poll_timeout;
    uint8_t state;
    uint8_t string;
  };

  using UsbDevice::UsbDevice;

  bool Open() override;
  void Close() override;

  // Perform a DFU_DETACH request.
  bool Detach(uint16_t timeout = 16) const;

  // Perform a DFU_DNLOAD request.
  bool Download(uint16_t block, uint8_t* data, uint16_t length) const;

  // Perform a DFU_GETSTATUS request.
  bool GetStatus(StatusRequest* status) const;

  // Perform a DFU_CLRSTATUS request.
  bool ClearStatus() const;

  // Perform a DFU_ABORT request.
  bool Abort() const;

  // Set the device in DFU mode. Returns true if the device is already in DFU
  // mode. The field |reenumerate| indicates if the device was re-enumerated, in
  // which case the device must be re-opened.
  bool SetInDfuMode(bool* reenumerate = nullptr);

  // Downloads the specified file contents to the device. The device must
  // already be in DFU mode to succeed.
  bool DownloadFile(const std::vector<char>& file_data) const;

  // Returns true if bitCanDnload is set in bmAttributes of the DFU functional
  // descriptor.
  bool CanDownload() const { return (func_desc_.attributes & kCanDnload); }

  // Returns true if bitCanUpload is set in bmAttributes of the DFU functional
  // descriptor.
  bool CanUpload() const { return (func_desc_.attributes & kCanUpload); }

  // Returns true if bitManifestationTolerant is set in bmAttributes of the DFU
  // functional descriptor.
  bool IsManifestationTolerant() const {
    return (func_desc_.attributes & kManifestationTolerant);
  }

  // Returns true if bitWillDetach is set in bmAttributes of the DFU functional
  // descriptor.
  bool WillDetach() const { return (func_desc_.attributes & kWillDetach); }

  bool in_dfu_mode() const { return in_dfu_mode_; }

 private:
  bool Find() override;

  bool DownloadFileBlock(int16_t block,
                         uint8_t* data,
                         uint16_t length,
                         State expected_state,
                         uint32_t* poll_timeout = nullptr) const;

  void OnDfuError() const;

  bool GetStatusUntilState(State state) const;

  int configuration_ = -1;
  int interface_ = -1;
  bool in_dfu_mode_ = false;
  FunctionalDescriptor func_desc_;
};

}  // namespace atrusctl

#endif  // USB_DFU_DEVICE_H_
