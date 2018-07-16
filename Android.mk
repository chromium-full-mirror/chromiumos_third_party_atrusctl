# Copyright 2019 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libatrusctl
LOCAL_CPP_EXTENSION := .cc
LOCAL_CLANG := true

LOCAL_SRC_FILES := \
  src/hid_connection.cc \
  src/hidraw_device.cc \
  src/hid_message.cc \
  src/diagnostics.cc \
  src/usb_device.cc \
  src/usb_dfu_device.cc \
  src/util.cc \
  src/upgrade.cc \
  src/atrus_controller.cc

LOCAL_C_INCLUDES := \
  $(LOCAL_PATH)/src \
  external/libusb/libusb

LOCAL_SHARED_LIBRARIES += \
  libchrome \
  libusb1.0

LOCAL_CFLAGS := \
  -Wall \
  -Werror \
  -Wno-narrowing \
  -Wno-return-type \
  -Wno-sign-compare \
  -Wno-unused-parameter

include $(BUILD_SHARED_LIBRARY)

