// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "upgrade.h"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/sys_byteorder.h>

#include "atrus_device.h"
#include "usb_dfu_device.h"
#include "util.h"

namespace atrusctl {

namespace {

const int kSuffixSizeBytes = 16;
const char kExpectedDfuSignature[] = "DFU";

const int kReEnumerateDelayMs = 1000;

// Represents an USB binary coded decimal
struct UsbBcd {
  explicit UsbBcd(uint16_t bcd) {
    major = (bcd >> 8) & 0xFF;
    minor = (bcd >> 4) & 0xF;
    sub_minor = bcd & 0xF;
  }

  bool operator>=(const UsbBcd& other) const {
    return (std::tie(major, minor, sub_minor) >=
            std::tie(other.major, other.minor, other.sub_minor));
  }

  std::string ToString() const {
    return base::StringPrintf("%d.%d.%d", major, minor, sub_minor);
  }

  uint8_t major;
  uint8_t minor;
  uint8_t sub_minor;
};

// Represents an USB DFU file suffix, don't reorder. According to Appendix B in
// the specification, the suffix is mirrored in relation to the structure.
struct UsbDfuFileSuffix {
  uint16_t bcd_device;
  uint16_t id_product;
  uint16_t id_vendor;
  uint16_t bcd_dfu;
  uint8_t dfu_signature[3];
  uint8_t length;
  uint32_t crc;
};

bool DeviceVersionIsHigherOrEqualFileVersion(const UsbDfuDevice& dev,
                                             const UsbDfuFileSuffix& suffix) {
  uint16_t bcd_device;
  if (!dev.GetBcdDevice(&bcd_device)) {
    LOG(ERROR) << "Could not get bcdDevice field from device";
    return false;
  }
  return (UsbBcd(bcd_device) >= UsbBcd(suffix.bcd_device));
}

bool ParseSuffixFromData(const std::vector<char>& data,
                         UsbDfuFileSuffix* suffix) {
  static_assert(sizeof(*suffix) == kSuffixSizeBytes, "Suffix is wrong size");
  if (data.capacity() <= kSuffixSizeBytes) {
    return false;
  }

  std::copy(data.end() - kSuffixSizeBytes, data.end(),
            reinterpret_cast<char*>(suffix));
  // Only the DFU signature needs to be reversed
  std::reverse(std::begin(suffix->dfu_signature),
               std::end(suffix->dfu_signature));

  // Validate the suffix, return false if it's invalid
  if (std::string(reinterpret_cast<char*>(suffix->dfu_signature),
                  sizeof(suffix->dfu_signature)) != kExpectedDfuSignature) {
    return false;
  }
  uint32_t crc = std::accumulate(
      data.begin(), (data.end() - sizeof(suffix->crc)), 0xFFFFFFFF, Crc32);
  return (crc == suffix->crc);
}

}  // namespace

bool PerformUpgrade(const base::FilePath& upgrade_file,
                    bool* skipped,
                    DeviceVariant device_variant,
                    bool force_upgrade,
                    std::string usb_path) {
  *skipped = false;
  LOG(INFO) << "[Device is entering upgrade mode]";
  LOG(INFO) << "Upgrade file " << upgrade_file.value();

  std::optional<int64_t> raw_file_size = base::GetFileSize(upgrade_file);
  if (!raw_file_size.has_value()) {
    LOG(ERROR) << "Could not get file size";
    return false;
  }
  int64_t file_size = raw_file_size.value();
  std::vector<char> file_contents(file_size);
  int read = base::ReadFile(upgrade_file, file_contents.data(), file_size);
  if (read < 0) {
    LOG(ERROR) << "Error while reading file";
    return false;
  }
  if (read != file_size) {
    LOG(ERROR) << "Could not read whole file";
    return false;
  }

  UsbDfuFileSuffix suffix;
  if (!ParseSuffixFromData(file_contents, &suffix)) {
    LOG(ERROR) << "Could not parse USB DFU file suffix";
    return false;
  }

  uint32_t id_vendor_dfu, id_product_dfu;
  if ((suffix.id_vendor == kAtrusUsbVid) &&
      (suffix.id_product == kAtrusUsbPid)) {
    LOG(INFO) << "Atrus upgrade file detected";
    if (device_variant != DeviceVariant::kAtrus) {
      LOG(ERROR) << "Atrus upgrade file and device variant does not match";
      return false;
    }
    id_vendor_dfu = kAtrusUsbDfuVid;
    id_product_dfu = kAtrusUsbDfuPid;
  } else if ((suffix.id_vendor == kOrionUsbVid) &&
             (suffix.id_product == kOrionUsbPid)) {
    LOG(INFO) << "Orion upgrade file detected";
    if (device_variant != DeviceVariant::kOrion) {
      LOG(ERROR) << "Orion upgrade file and device variant does not match";
      return false;
    }
    id_vendor_dfu = kOrionUsbDfuVid;
    id_product_dfu = kOrionUsbDfuPid;
  } else {
    LOG(ERROR) << "The upgrade file is neither intended for an Atrus nor an "
                  "Orion device";
    return false;
  }
  std::string file_version = UsbBcd(suffix.bcd_device).ToString();
  LOG(INFO) << "Upgrade file version " << file_version;

  uint16_t bcd_device;
  std::string dev_version = "unknown";
  bool run_time_mode_failed = false;

  // Set the device in DFU mode
  UsbDfuDevice dev_run_time(suffix.id_vendor, suffix.id_product, usb_path);
  if (!dev_run_time.Open()) {
    LOG(INFO) << "Could not open device in run-time mode";
    run_time_mode_failed = true;
  } else {
    if (dev_run_time.GetBcdDevice(&bcd_device)) {
      dev_version = UsbBcd(bcd_device).ToString();
    }
    LOG(INFO) << "Current device version " << dev_version;

    if (DeviceVersionIsHigherOrEqualFileVersion(dev_run_time, suffix) &&
        !force_upgrade) {
      *skipped = true;
      return true;
    }
    LOG(INFO) << "Setting device in DFU mode";
    dev_run_time.SetInDfuMode();
    // Wait for device to re-enumerate in DFU mode
    TimeoutMs(kReEnumerateDelayMs);
  }

  // Device should be in DFU mode, start upgrade
  UsbDfuDevice dev_dfu(id_vendor_dfu, id_product_dfu, "",
                       dev_run_time.GetSerialNumber());
  if (!dev_dfu.Open()) {
    LOG(ERROR) << "Could not open device in DFU mode";
    return false;
  }
  if (run_time_mode_failed) {
    if (dev_dfu.GetBcdDevice(&bcd_device)) {
      dev_version = UsbBcd(bcd_device).ToString();
    }
    LOG(INFO) << "Current device version " << dev_version;
  }

  if (DeviceVersionIsHigherOrEqualFileVersion(dev_dfu, suffix) &&
      !force_upgrade) {
    *skipped = true;
    return true;
  }
  LOG(INFO) << "Upgrading from device version " << dev_version << " to "
            << file_version;
  if (!dev_dfu.DownloadFile(file_contents)) {
    return false;
  }
  LOG(INFO) << "Resetting device";
  return dev_dfu.Reset();
}

}  // namespace atrusctl
