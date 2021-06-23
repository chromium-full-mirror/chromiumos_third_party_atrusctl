// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid_connection.h"

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <base/check.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/posix/safe_strerror.h>

namespace atrusctl {

bool HIDConnection::Open(const std::string& path) {
  int descriptor = HANDLE_EINTR(open(path.c_str(), O_RDWR));
  if (descriptor < 0) {
    PLOG(ERROR) << path;
    return false;
  }
  fd_.reset(descriptor);
  return true;
}

void HIDConnection::Close() {
  if (fd_.is_valid()) {
    fd_.reset();
  }
}

// Monitors the file descriptor |fd_| for when it becomes ready for read
// operations. This is done by passing |fd_| through |readset| to the second
// argument of select() (see manpage for select(2) for more details).
// We use a timeout of 2 seconds, after that we assume that the device is
// unresponsive and that there is no read operation possible at the time.
//
// If select() is interrupted by a signal handler (and returns EINTR), we want
// to retry the operation. Since select() modifies the timeout structure
// |tv| when this happens, we need to reset it before each retry. Hence the
// do-while loop.
bool HIDConnection::WaitUntilReadable(bool* timeout) const {
  DCHECK(fd_.is_valid());
  *timeout = false;

  fd_set readset;
  struct timeval tv;
  int retval;
  do {
    FD_ZERO(&readset);
    FD_SET(fd_.get(), &readset);
    tv.tv_sec = 2;
    tv.tv_usec = 200000;
    retval = select(fd_.get() + 1, &readset, NULL, NULL, &tv);
    if ((retval < 0) && (errno != EINTR)) {
      PLOG(ERROR) << "select";
      return false;
    }
    if (retval == 0) {
      *timeout = true;
      return false;
    }
  } while ((retval < 0) && (errno == EINTR));

  return true;
}

bool HIDConnection::Write(const std::vector<uint8_t>& buffer) const {
  DCHECK(fd_.is_valid());

  ssize_t size_written = write(fd_.get(), buffer.data(), buffer.size());
  if (size_written < 0) {
    PLOG(ERROR) << "write";
    return false;
  }
  return true;
}

bool HIDConnection::Read(std::vector<uint8_t>* buffer,
                         size_t buffer_size) const {
  DCHECK(fd_.is_valid());
  CHECK(buffer);

  buffer->resize(buffer_size);

  ssize_t size_read = read(fd_.get(), buffer->data(), buffer->size());
  if (size_read < 0) {
    PLOG(ERROR) << "read";
    return false;
  }
  return true;
}

}  // namespace atrusctl
