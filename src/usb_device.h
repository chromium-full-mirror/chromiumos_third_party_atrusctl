// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USB_DEVICE_H_
#define USB_DEVICE_H_

#include <libusb.h>
#include <cstdint>
#include <ostream>
#include <string>

namespace atrusctl {

class UsbDevice {
 public:
  explicit UsbDevice(uint16_t id_vendor,
                     uint16_t id_product,
                     std::string usb_path = "",
                     std::string usb_serial = "");
  UsbDevice(const UsbDevice&) = delete;
  UsbDevice& operator=(const UsbDevice&) = delete;

  virtual ~UsbDevice();

  virtual bool Open();
  virtual void Close();

  // Writes the bConfigurationValue of the currently active configuration to
  // |config|.
  bool GetConfiguration(int* config) const;

  // Set the active configuration |config| for the device.
  bool SetConfiguration(int config);

  // Claim the specified interface |iface|. This must be done before any I/O is
  // performed on any of its endpoints.
  bool ClaimInterface(int iface);

  // Release a previously claimed interface |iface|.
  bool ReleaseInterface(int iface);

  // Performs an USB reset.
  bool Reset(bool* reenumerate = nullptr);

  // Performs an USB control transfer. The fields |request_type|, |request|,
  // |value|, |index|, and |length| corresponds to bmRequestType, bRequest,
  // wValue, wIndex and wLength, respectively, in the setup packet. Depending on
  // how |request_type| is set, |data| is either input or output.
  int ControlTransfer(uint8_t request_type,
                      uint8_t request,
                      uint16_t value,
                      uint16_t index,
                      unsigned char* data,
                      uint16_t length) const;

  bool GetBcdDevice(uint16_t* bcd_device) const;
  bool GetStringDescriptor(uint8_t index, std::string* str) const;

  std::string ToString() const;
  std::string GetSerialNumber() const;

 protected:
  virtual bool Find();

  std::string UsbError(int code) const;

  const uint16_t id_vendor_;
  const uint16_t id_product_;
  const std::string usb_path_;
  std::string usb_serial_number_;
  libusb_context* context_ = nullptr;
  libusb_device* dev_ = nullptr;
  libusb_device_handle* handle_ = nullptr;

 private:
  bool Initialize();
};

}  // namespace atrusctl

#endif  // USB_DEVICE_H_
