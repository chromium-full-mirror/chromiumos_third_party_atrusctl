// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid_message.h"

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace atrusctl {

namespace {

const int kHeaderSize = 3;

}  // namespace

HIDMessage::HIDMessage(uint8_t report_id, uint16_t command_id)
    : report_id_(report_id), command_id_(command_id) {}

bool HIDMessage::PackIntoBuffer(std::vector<uint8_t>* buffer) const {
  CHECK(buffer);

  *buffer = {report_id_, static_cast<uint8_t>(command_id_),
             static_cast<uint8_t>(command_id_ >> 8)};

  return true;
}

bool HIDMessage::UnpackFromBuffer(const std::vector<uint8_t>& buffer) {
  if (buffer.size() < kHeaderSize + 1) {  // + 1 for body
    LOG(ERROR) << "HIDMessage::UnpackFromBuffer needs a vector with a minimum"
               << "size of " << kHeaderSize + 1
               << ", actual: " << buffer.size();
    return false;
  }

  static_assert(
      sizeof(report_id_) + sizeof(command_id_) == kHeaderSize,
      "sizeof(report_id_) + sizeof(command_id_) should equal kHeaderSize");

  report_id_ = buffer[0];
  command_id_ = (buffer[2] << 8) | buffer[1];
  // The body of the message comes after the header
  const char* buffer_data = reinterpret_cast<const char*>(buffer.data());
  body_.assign(buffer_data + kHeaderSize, buffer_data + buffer.size());

  return true;
}

bool HIDMessage::Validate(const HIDMessage& other) {
  return (command_id_ == other.command_id());
}

std::string HIDMessage::ToString() const {
  return (base::StringPrintf("[0x%04X]: %s", command_id_, body_.c_str()));
}

}  // namespace atrusctl
