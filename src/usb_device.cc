// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb_device.h"

#include <vector>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace atrusctl {

namespace {

const int kControlTransferTimeoutMs = 10000;

}  // namespace

UsbDevice::UsbDevice(uint16_t id_vendor,
                     uint16_t id_product,
                     std::string usb_path,
                     std::string usb_serial)
    : id_vendor_(id_vendor),
      id_product_(id_product),
      usb_path_(usb_path),
      usb_serial_number_(usb_serial) {}

UsbDevice::~UsbDevice() {
  Close();
  libusb_unref_device(dev_);
  libusb_exit(context_);
}

bool UsbDevice::Initialize() {
  int ret = libusb_init(&context_);
  if (ret < 0) {
    LOG(ERROR) << "Could not initialize libusb: " << UsbError(ret);
  }
  return (ret >= 0);
}

static bool is_usb_path_compatible(libusb_device* dev,
                                   const std::string& usb_path) {
  if (usb_path.empty()) {
    // Do not match by given path
    return true;
  }

  const int kBufferSize = 9;
  char path_buffer[kBufferSize];

  if (usb_path.length() < kBufferSize) {
    return false;
  }

  uint8_t bus_number = libusb_get_bus_number(dev);
  uint8_t device_address = libusb_get_device_address(dev);
  snprintf(path_buffer, kBufferSize, "/%03d/%03d", bus_number, device_address);

  return usb_path.compare(usb_path.length() - kBufferSize + 1,
                          std::string::npos, path_buffer) == 0;
}

static bool check_usb_serial_number(libusb_device* usb_device,
                                    uint8_t desc_serial_number,
                                    std::string* serial_number) {
  if (desc_serial_number == 0) {
    // Cannot query serial number from device
    // return true if not searching by serial number i.e is empty
    return serial_number->empty();
  }

  struct libusb_device_handle* dev_handle = nullptr;

  if (libusb_open(usb_device, &dev_handle) < 0) {
    return serial_number->empty();
  }

  char device_serial_number[256];
  memset(device_serial_number, 0, sizeof(device_serial_number));
  int ret = libusb_get_string_descriptor_ascii(
      dev_handle, desc_serial_number, (unsigned char*)device_serial_number,
      sizeof(device_serial_number));

  libusb_close(dev_handle);

  if (serial_number->empty()) {
    // Do not match by serial and update if not previously set
    (*serial_number) += device_serial_number;
    return true;
  }

  if (ret <= 0 || serial_number->compare(device_serial_number) != 0) {
    return false;
  }

  return true;
}

std::string UsbDevice::GetSerialNumber() const {
  return usb_serial_number_;
}

// Limited to one physical device
bool UsbDevice::Find() {
  bool found = false;
  libusb_device** devs;
  ssize_t num_devs = libusb_get_device_list(context_, &devs);
  for (ssize_t i = 0; i < num_devs; ++i) {
    struct libusb_device_descriptor dev_desc;
    libusb_device* dev = devs[i];

    if (!is_usb_path_compatible(dev, usb_path_)) {
      // USB path mismatch
      continue;
    }

    if (libusb_get_device_descriptor(dev, &dev_desc) < 0) {
      continue;
    }

    if ((dev_desc.idVendor != id_vendor_) ||
        (dev_desc.idProduct != id_product_)) {
      // Usb VID PID mismatch
      continue;
    }

    // Extra check for multiple device upgrade issue
    if (!check_usb_serial_number(dev, dev_desc.iSerialNumber,
                                 &usb_serial_number_)) {
      // Serial number check failed
      continue;
    }

    found = true;
    dev_ = dev;
    libusb_ref_device(dev_);
    break;
  }
  if (!found) {
    LOG(ERROR) << "Could not find device " << ToString();
  }
  libusb_free_device_list(devs, true);
  return found;
}

bool UsbDevice::Open() {
  if ((!context_) && !Initialize()) {
    return false;
  }
  if (!Find()) {
    return false;
  }
  int ret = libusb_open(dev_, &handle_);
  if (ret < 0) {
    LOG(ERROR) << "Could not open device " << ToString() << ": "
               << UsbError(ret);
  }
  return (ret >= 0);
}

void UsbDevice::Close() {
  libusb_close(handle_);
}

bool UsbDevice::GetConfiguration(int* config) const {
  int ret = libusb_get_configuration(handle_, config);
  if (ret < 0) {
    LOG(ERROR) << "Could not get configuration: " << UsbError(ret);
  }
  return (ret >= 0);
}

bool UsbDevice::SetConfiguration(int config) {
  int current_config;
  GetConfiguration(&current_config);
  if (config == current_config) {
    return true;
  }
  int ret = libusb_set_configuration(handle_, config);
  if (ret < 0) {
    LOG(ERROR) << "Could not set configuration " << config << ": "
               << UsbError(ret);
  }
  return (ret >= 0);
}

bool UsbDevice::ClaimInterface(int iface) {
  int ret = libusb_claim_interface(handle_, iface);
  if (ret < 0) {
    LOG(ERROR) << "Could not claim interface " << iface << ": "
               << UsbError(ret);
  }
  return (ret >= 0);
}

bool UsbDevice::ReleaseInterface(int iface) {
  int ret = libusb_release_interface(handle_, iface);
  if (ret < 0) {
    LOG(ERROR) << "Could not release interface " << iface << ": "
               << UsbError(ret);
  }
  return (ret >= 0);
}

bool UsbDevice::Reset(bool* reenumerate) {
  if (reenumerate) {
    *reenumerate = false;
  }
  int ret = libusb_reset_device(handle_);
  if (ret < 0) {
    if (ret != LIBUSB_ERROR_NOT_FOUND) {
      LOG(ERROR) << "Could not reset device " << ToString() << ": "
                 << UsbError(ret);
      return false;
    }
    if (reenumerate) {
      *reenumerate = true;
    }
  }
  return true;
}

int UsbDevice::ControlTransfer(uint8_t request_type,
                               uint8_t request,
                               uint16_t value,
                               uint16_t index,
                               unsigned char* data,
                               uint16_t length) const {
  int ret =
      libusb_control_transfer(handle_, request_type, request, value, index,
                              data, length, kControlTransferTimeoutMs);
  if (ret < 0) {
    std::string params = base::StringPrintf(
        "[bmRequestType=0x%X, bRequest=0x%X, wValue=0x%X, wIndex=0x%X, "
        "wLength=0x%X]",
        request_type, request, value, index, length);
    LOG(ERROR) << "Could not perform control transfer " << params << ": "
               << UsbError(ret);
  }
  return ret;
}

bool UsbDevice::GetBcdDevice(uint16_t* bcd_device) const {
  struct libusb_device_descriptor dev_desc;
  int ret = libusb_get_device_descriptor(dev_, &dev_desc);
  if (ret < 0) {
    LOG(ERROR) << "Could not get device descriptor";
    return false;
  }
  *bcd_device = dev_desc.bcdDevice;
  return true;
}

bool UsbDevice::GetStringDescriptor(uint8_t index, std::string* str) const {
  const int kMaxStringSizeBytes = 256;
  std::vector<char> buffer(kMaxStringSizeBytes);
  int ret = libusb_get_string_descriptor_ascii(
      handle_, index, reinterpret_cast<unsigned char*>(buffer.data()),
      kMaxStringSizeBytes);
  if (ret >= 0) {
    *str = std::string(buffer.data());
  }
  return (ret >= 0);
}

std::string UsbDevice::UsbError(int code) const {
  return libusb_strerror(static_cast<enum libusb_error>(code));
}

std::string UsbDevice::ToString() const {
  return base::StringPrintf("%04X:%04X", id_vendor_, id_product_);
}

}  // namespace atrusctl
