// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_MESSAGE_H_
#define HID_MESSAGE_H_

#include <string>
#include <vector>

namespace atrusctl {

class HIDMessage {
 public:
  HIDMessage() = default;
  HIDMessage(uint8_t report_id, uint16_t command_id);

  bool PackIntoBuffer(std::vector<uint8_t>* buffer) const;
  bool UnpackFromBuffer(const std::vector<uint8_t>& buffer);
  bool Validate(const HIDMessage& other);

  uint8_t report_id() const { return report_id_; }
  uint16_t command_id() const { return command_id_; }
  const std::string& body() const { return body_; }

  std::string ToString() const;

 private:
  // The message is constructed as follows: <header><body>, where <header> is
  // |report_id_| followed by |command_id_|. The body is empty for a request,
  // i.e. sending a message. On a response, the body contains the actual
  // response-string from the hid device
  uint8_t report_id_;
  uint16_t command_id_;
  std::string body_;
};

}  // namespace atrusctl

#endif  // HID_MESSAGE_H_
