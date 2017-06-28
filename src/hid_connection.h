// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_CONNECTION_H_
#define HID_CONNECTION_H_

#include <string>
#include <vector>

#include <base/files/scoped_file.h>

namespace atrusctl {

class HIDConnection {
 public:
  bool Open(const std::string& path);
  void Close();
  bool WaitUntilReadable(bool* timeout) const;
  bool Write(const std::vector<uint8_t>& buffer) const;
  bool Read(std::vector<uint8_t>* buffer, size_t buffer_size) const;

 private:
  base::ScopedFD fd_;
};

}  // namespace atrusctl

#endif  // HID_CONNECTION_H_
