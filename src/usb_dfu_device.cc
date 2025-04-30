// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_dfu_device.h"

#include <algorithm>
#include <vector>

#include <base/logging.h>
#include <base/numerics/byte_conversions.h>
#include <base/strings/stringprintf.h>

#include "util.h"

namespace atrusctl {

namespace {

// From Tables 4.1 and 4.4 in the specification
const int kInterfaceSubClassDfu = 1;
const int kInterfaceProtocolDfuMode = 2;

// From Table 3.2 in the specification
enum DfuRequest {
  kDetach = 0x0,
  kDnload = 0x1,
  kUpload = 0x2,
  kGetStatus = 0x3,
  kClrStatus = 0x4,
  kGetState = 0x5,
  kAbort = 0x6,
};

// From Section 6.1.2 in the specification
enum Status {
  kStatusOk = 0x00,
  kStatusErrTarget = 0x01,
  kStatusErrFile = 0x02,
  kStatusErrWrite = 0x03,
  kStatusErrErase = 0x04,
  kStatusErrCheckErased = 0x05,
  kStatusErrProg = 0x06,
  kStatusErrVerify = 0x07,
  kStatusErrAddress = 0x08,
  kStatusErrNotDone = 0x09,
  kStatusErrFirmware = 0x0A,
  kStatusErrVendor = 0x0B,
  kStatusErrUsbR = 0x0C,
  kStatusErrPor = 0x0D,
  kStatusErrUnknown = 0x0E,
  kStatusErrStalledPkt = 0x0F,
};

std::string StateToString(UsbDfuDevice::State state) {
  switch (state) {
    case UsbDfuDevice::kStateAppIdle:
      return "appIDLE";
    case UsbDfuDevice::kStateAppDetach:
      return "appDETACH";
    case UsbDfuDevice::kStateDfuIdle:
      return "dfuIDLE";
    case UsbDfuDevice::kStateDfuDnloadSync:
      return "dfuDNLOADSYNC";
    case UsbDfuDevice::kStateDfuDnbusy:
      return "dfuDNBUSY";
    case UsbDfuDevice::kStateDfuDnloadIdle:
      return "dfuDNLOADIDLE";
    case UsbDfuDevice::kStateDfuManifestSync:
      return "dfuMANIFESTSYNC";
    case UsbDfuDevice::kStateDfuManifest:
      return "dfuMANIFEST";
    case UsbDfuDevice::kStateDfuManifestWaitReset:
      return "dfuMANIFESTWAIT-RESET";
    case UsbDfuDevice::kStateDfuUploadIdle:
      return "dfuUPLOADIDLE";
    case UsbDfuDevice::kStateDfuError:
      return "dfuERROR";
  }
}

std::string StatusToString(Status status) {
  // Strings are taken from Section 6.1.2 in the specification
  switch (status) {
    case kStatusOk:
      return "No error condition is present";
    case kStatusErrTarget:
      return "File is not targeted for use by this device";
    case kStatusErrFile:
      return "File is for this device but fails some vendor-specific "
             "verification test";
    case kStatusErrWrite:
      return "Device is unable to write memory";
    case kStatusErrErase:
      return "Memory erase function failed";
    case kStatusErrCheckErased:
      return "Memory erase check failed";
    case kStatusErrProg:
      return "Program memory function failed";
    case kStatusErrVerify:
      return "Programmed memory failed verification";
    case kStatusErrAddress:
      return "Cannot program memory due to received address that is out of "
             "range";
    case kStatusErrNotDone:
      return "Received DFU_DNLOAD with wLength = 0, but device does not think "
             "it has all of the data yet";
    case kStatusErrFirmware:
      return "Device's firmware is corrupt. It cannot return to run-time "
             "(non-DFU) operations";
    case kStatusErrVendor:
      return "errVendor";
    case kStatusErrUsbR:
      return "Device detected unexpected USB reset signaling";
    case kStatusErrPor:
      return "Device detected unexpected power on reset";
    case kStatusErrUnknown:
      return "Something went wrong, but the device does not know what it was";
    case kStatusErrStalledPkt:
      return "Device stalled an unexpected request";
  }
}

uint32_t ByteSwapToLE16(uint16_t n) {
  return base::U16FromLittleEndian(base::U16ToNativeEndian(n));
}

}  // namespace

bool UsbDfuDevice::Find() {
  struct libusb_device_descriptor dev_desc;
  if (!UsbDevice::Find() ||
      (libusb_get_device_descriptor(dev_, &dev_desc) < 0)) {
    return false;
  }

  struct libusb_config_descriptor* config_desc;
  for (int i = 0; i < dev_desc.bNumConfigurations; ++i) {
    if (libusb_get_config_descriptor(dev_, i, &config_desc) < 0) {
      continue;
    }
    for (int j = 0; j < config_desc->bNumInterfaces; ++j) {
      const struct libusb_interface* iface = &config_desc->interface[j];
      if (!iface) {
        break;
      }
      const uint8_t* extra_desc = nullptr;
      const struct libusb_interface_descriptor* iface_desc;
      for (int k = 0; k < iface->num_altsetting; ++k) {
        iface_desc = &iface->altsetting[k];
        if (iface_desc->bInterfaceClass == LIBUSB_CLASS_APPLICATION &&
            iface_desc->bInterfaceSubClass == kInterfaceSubClassDfu &&
            iface_desc->extra_length <= sizeof(func_desc_)) {
          extra_desc = iface_desc->extra;
          break;
        }
      }
      if (!extra_desc) {
        continue;
      }

      func_desc_.length = extra_desc[0];
      func_desc_.type = extra_desc[1];
      func_desc_.attributes = extra_desc[2];
      func_desc_.detach_timeout =
          ByteSwapToLE16((extra_desc[4] << 8) | extra_desc[3]);
      func_desc_.transfer_size =
          ByteSwapToLE16((extra_desc[6] << 8) | extra_desc[5]);
      func_desc_.dfu_version =
          ByteSwapToLE16((extra_desc[8] << 8) | extra_desc[7]);

      configuration_ = config_desc->bConfigurationValue;
      interface_ = iface_desc->bInterfaceNumber;

      in_dfu_mode_ =
          (iface_desc->bInterfaceProtocol == kInterfaceProtocolDfuMode);

      libusb_free_config_descriptor(config_desc);
      return true;
    }
  }
  libusb_free_config_descriptor(config_desc);

  return false;
}

bool UsbDfuDevice::Open() {
  if (!UsbDevice::Open()) {
    return false;
  }
  SetConfiguration(configuration_);
  if (!ClaimInterface(interface_)) {
    return false;
  }
  return true;
}

void UsbDfuDevice::Close() {
  UsbDevice::Close();
}

bool UsbDfuDevice::Detach(uint16_t timeout) const {
  uint8_t type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS |
                 LIBUSB_RECIPIENT_INTERFACE;
  int ret = UsbDevice::ControlTransfer(type, kDetach, timeout, interface_,
                                       nullptr, 0);
  return (ret >= 0);
}

bool UsbDfuDevice::Download(uint16_t block,
                            uint8_t* data,
                            uint16_t length) const {
  uint8_t type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS |
                 LIBUSB_RECIPIENT_INTERFACE;
  int ret = UsbDevice::ControlTransfer(type, kDnload, block, interface_, data,
                                       length);
  return (ret >= 0);
}

bool UsbDfuDevice::GetStatus(StatusRequest* status) const {
  if (!status) {
    return false;
  }
  uint8_t buf[6];
  uint8_t type = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS |
                 LIBUSB_RECIPIENT_INTERFACE;
  int ret = UsbDevice::ControlTransfer(type, kGetStatus, 0, interface_, buf,
                                       sizeof(buf));
  status->status = buf[0];
  status->poll_timeout = (uint32_t)((buf[3] << 16) | (buf[2] << 8) | buf[1]);
  status->state = buf[4];
  status->string = buf[5];
  return (ret >= 0);
}

bool UsbDfuDevice::ClearStatus() const {
  uint8_t type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS |
                 LIBUSB_RECIPIENT_INTERFACE;
  int ret =
      UsbDevice::ControlTransfer(type, kClrStatus, 0, interface_, nullptr, 0);
  return (ret >= 0);
}

bool UsbDfuDevice::Abort() const {
  uint8_t type = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS |
                 LIBUSB_RECIPIENT_INTERFACE;
  int ret =
      UsbDevice::ControlTransfer(type, kGetState, 0, interface_, nullptr, 0);
  return (ret >= 0);
}

bool UsbDfuDevice::SetInDfuMode(bool* reenumerate) {
  if (in_dfu_mode_) {
    return true;
  }
  if (!Detach()) {
    return false;
  }
  // Not tested with bitWillDetach == 1
  if (!WillDetach()) {
    if (!Reset(reenumerate)) {
      return false;
    }
  }
  return true;
}

bool UsbDfuDevice::DownloadFile(const std::vector<char>& file_data) const {
  if (!in_dfu_mode_) {
    LOG(ERROR) << "Device must be in DFU mode before download";
    return false;
  }

  StatusRequest status_req;
  if (!GetStatus(&status_req) || status_req.state != kStateDfuIdle) {
    LOG(ERROR) << "Device is not in a proper state for download: "
               << StateToString(static_cast<State>(status_req.state));
    return false;
  }

  LOG(INFO) << "Starting download";
  // Download must be done in blocks of max |func_desc_.transfer_size|
  const size_t kBlockSize = func_desc_.transfer_size;
  // Download() takes a non-const uint8_t* argument
  std::vector<char> local_file_data(file_data);
  size_t file_data_size = file_data.size();
  uint16_t block_index = 0;
  while (block_index * kBlockSize < file_data_size) {
    size_t offset = kBlockSize * block_index;
    size_t size_to_download = std::min(kBlockSize, file_data_size - offset);
    if (!DownloadFileBlock(
            block_index++, reinterpret_cast<uint8_t*>(&local_file_data[offset]),
            size_to_download, kStateDfuDnloadIdle, &status_req.poll_timeout)) {
      return false;
    }
  }
  // After the whole file is downloaded, issue a final download request with
  // length = 0 to inform the device of this. See section A.2.6 in the
  // specification for details.
  if (!DownloadFileBlock(block_index, nullptr, 0, kStateDfuManifest,
                         &status_req.poll_timeout)) {
    return false;
  }
  LOG(INFO) << "Download completed";

  LOG(INFO) << "Starting manifestation";
  State expected_state =
      IsManifestationTolerant() ? kStateDfuIdle : kStateDfuManifestWaitReset;
  if (!GetStatusUntilState(expected_state)) {
    // GetStatusUntilState() will log if an error occurs
    return false;
  }
  LOG(INFO) << "Manifestation completed";
  return true;
}

bool UsbDfuDevice::DownloadFileBlock(int16_t block,
                                     uint8_t* data,
                                     uint16_t length,
                                     State expected_state,
                                     uint32_t* poll_timeout) const {
  if (!Download(block, data, length)) {
    return false;
  }
  TimeoutMs(poll_timeout ? *poll_timeout : 0);
  StatusRequest status_req;
  if (!GetStatus(&status_req)) {
    LOG(ERROR) << "Could not get status from device during download";
    return false;
  }
  if (poll_timeout) {
    *poll_timeout = status_req.poll_timeout;
  }
  if (status_req.state == kStateDfuError) {
    OnDfuError();
    return false;
  }
  if (status_req.state == kStateDfuDnbusy) {
    LOG(WARNING) << "Device reported too short timeout [block=" << block
                 << ", bwPollTimeout=" << status_req.poll_timeout
                 << "], waiting some more";
    TimeoutMs(status_req.poll_timeout);
    return DownloadFileBlock(block, data, length, expected_state);
  }
  if ((block > 0) && (status_req.state != expected_state)) {
    LOG(ERROR) << "Device got to an unexpected state during download: "
               << StateToString(static_cast<State>(status_req.state));
    return false;
  }
  return true;
}

void UsbDfuDevice::OnDfuError() const {
  StatusRequest status_req;
  if (!GetStatus(&status_req)) {
    LOG(ERROR) << "Could not get status of the device on DFU error";
    return;
  }
  if ((status_req.state != kStateDfuError) ||
      (status_req.status == kStatusOk)) {
    LOG(WARNING) << "Tried to handle error while not in an error state";
    return;
  }

  std::string msg = StatusToString(static_cast<Status>(status_req.status));
  if (status_req.status == kStatusErrVendor) {
    GetStringDescriptor(status_req.string, &msg);
  }
  LOG(ERROR) << "DFU Error: " << msg;

  ClearStatus();
}

bool UsbDfuDevice::GetStatusUntilState(State state) const {
  StatusRequest status_req;
  while (true) {
    if (!GetStatus(&status_req)) {
      LOG(ERROR) << "Could not get status of the device";
      return false;
    }
    if (status_req.state == state) {
      return true;
    }
    if (status_req.state == kStateDfuError) {
      OnDfuError();
      return false;
    }
    TimeoutMs(status_req.poll_timeout);
  }
}

}  // namespace atrusctl
