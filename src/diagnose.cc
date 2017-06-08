// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnose.h"

#include <argp.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "atrus_device.h"

namespace atrusctl {

struct arguments {
  uint8_t report_id;
  uint16_t diag_id;
  bool extended;
};

struct hidraw_dev {
  char* path;
  struct hidraw_dev* next;
};

struct report {
  uint8_t report_id;
  uint16_t diag_id;
  char* message;
};

static int check_uptime(int fd,
                        const uint16_t cmd,
                        struct report* report,
                        bool* changed);
static int check_num_slaves(int fd,
                            const uint16_t cmd,
                            struct report* report,
                            bool* changed);

struct diag_trigger {
  uint16_t cmd;
  int (*check)(int, const uint16_t, struct report*, bool*);
};

static const struct diag_trigger triggers[] = {
    {0x1604, check_uptime}, {0x1351, check_num_slaves}, {0}};

static const uint16_t commands[] = {0x1500,  // DAISY_DFU_STATUS
                                    0x1600,  // STATE_MUTED
                                    0x1601,  // STATE_HOOK_OFF
                                    0x1602,  // STATE_VOLUME
                                    0x1603,  // STATE_LED
                                    0x1604,  // STATE_UPTIME
                                    0x1605,  // STATE_SHARC_ALIVE
                                    0x1351,  // DAISY_NUM_SLAVES
                                    0x1701,  // USB_VOLTAGE
                                    0x1800,  // TV_MIC_SELECTION
                                    0x1801,  // TV_ERLE_MIN
                                    0x1804,  // TV_ERLE_MAX
                                    0x1805,  // TV_ERLE_MEAN
                                    0x1806,  // TV_ERLE_VARIANCE
                                    0x1807,  // TV_FEEDBACK_EST_MIN
                                    0x1808,  // TV_FEEDBACK_EST_MAX
                                    0x1809,  // TV_FEEDBACK_EST_MEAN
                                    0x180A,  // TV_FEEDBACK_EST_VARIANCE
                                    0x180B,  // TV_NR_MIN
                                    0x180C,  // TV_NR_MAX
                                    0x180D,  // TV_NR_MEAN
                                    0x180E,  // TV_NR_VARIANCE
                                    0x180F,  // TV_MIC_AVE_MIN
                                    0x1810,  // TV_MIC_AVE_MAX
                                    0x1811,  // TV_MIC_AVE_MEAN
                                    0x1812,  // TV_MIC_AVE_VARIANCE
                                    0x1813,  // TV_LS_NOISE_MIN
                                    0x1814,  // TV_LS_NOISE_MAX
                                    0x1815,  // TV_LS_NOISE_MEAN
                                    0x1816,  // TV_LS_NOISE_VARIANCE
                                    0x1817,  // TV_MIC_NOISE_MIN
                                    0x1818,  // TV_MIC_NOISE_MAX
                                    0x1819,  // TV_MIC_NOISE_MEAN
                                    0x181A,  // TV_MIC_NOISE_VARIANCE
                                    0x181B,  // TV_GAMMA_MIN
                                    0x181C,  // TV_GAMMA_MAX
                                    0x181D,  // TV_GAMMA_MEAN
                                    0x181E,  // TV_GAMMA_VARIANCE
                                    0x5000,  // TOUCH_REGISTER_STATUS
                                    0x5001,  // SPI_QUEUE_STATUS
                                    0x5004,  // NR_BUTTON_PRESSES
                                    0};

static const uint16_t commands_extended[] = {
    0x1300,  // VERSION_MASTER
    0x1310,  // VERSION_SLAVE1_UP
    0x1311,  // VERSION_SLAVE2_UP
    0x1312,  // VERSION_SLAVE3_UP
    0x1313,  // VERSION_SLAVE4_UP
    0x1314,  // VERSION_SLAVE1_DOWN
    0x1315,  // VERSION_SLAVE2_DOWN
    0x1316,  // VERSION_SLAVE3_DOWN
    0x1317,  // VERSION_SLAVE4_DOWN
    0x1350,  // DAISY_MIN_VERSION
    0x1700,  // HW_ID
    0x1702,  // MIC_CALIBRATION
    0x1703,  // BOOT_PARTITION
    0x5002,  // TOUCH_ANALOG_CALIBRATION_STATUS
    0x5003,  // RF_NOISE_DETECTION
    0};

static const size_t report_header_length = 3;
static const size_t report_length = 64;

static void report_pack(struct report* report, char* buf) {
  buf[0] = report->report_id;
  buf[1] = report->diag_id;
  buf[2] = report->diag_id >> 8;
}

static void report_unpack(struct report* report, char* buf) {
  report->report_id = buf[0];
  report->diag_id = (buf[2] << 8) | buf[1];
  report->message = &buf[3];
}

static int get_report(int fd, struct report* report) {
  char buf[report_length];

  report_pack(report, buf);
  ssize_t retval = write(fd, buf, report_header_length);
  if (retval < 0) {
    return -1;
  }

  struct report read_report;
  while (true) {
    fd_set readset;
    struct timeval tv;

    do {
      FD_ZERO(&readset);
      FD_SET(fd, &readset);
      tv.tv_sec = 2;
      tv.tv_usec = 200000;
      retval = select(fd + 1, &readset, NULL, NULL, &tv);
      if ((retval <= 0) && (errno != EINTR)) {
        return -1;
      }
    } while ((retval < 0) && (errno == EINTR));

    retval = read(fd, buf, report_length);
    if (retval < 0) {
      return -1;
    }

    report_unpack(&read_report, buf);
    if (read_report.diag_id == report->diag_id) {
      break;
    }
    if (read_report.diag_id == 0x7FF0) {
      // Unknown command response
      break;
    }
  }

  strncpy(report->message,
          read_report.message,
          report_length - report_header_length);

  return 0;
}

static int str_to_uint16(const char* str, uint16_t* val) {
  int retval = sscanf(str, "%" SCNx16, val);
  return (retval == EOF || retval < 0) ? -1 : 0;
}

static int get_hidraw_devices(struct hidraw_dev** head) {
  *head = NULL;

  struct udev* udev = udev_new();
  if (!udev) {
    return -1;
  }
  struct udev_enumerate* enumerate = udev_enumerate_new(udev);
  if (!enumerate) {
    return -1;
  }
  udev_enumerate_add_match_subsystem(enumerate, "hidraw");
  udev_enumerate_scan_devices(enumerate);

  struct hidraw_dev* current = NULL;
  struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
  struct udev_list_entry* dev_list_entry;
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char* path = udev_list_entry_get_name(dev_list_entry);
    struct udev_device* dev = udev_device_new_from_syspath(udev, path);
    const char* dev_node_path = udev_device_get_devnode(dev);

    struct udev_device* parent;
    parent =
        udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
    if (!parent) {
      continue;
    }

    uint16_t idVendor;
    uint16_t idProduct;
    const char* attr = udev_device_get_sysattr_value(parent, "idVendor");
    int retval = str_to_uint16(attr, &idVendor);
    if (retval < 0) {
      goto unref;
    }

    attr = udev_device_get_sysattr_value(parent, "idProduct");
    retval = str_to_uint16(attr, &idProduct);
    if (retval < 0) {
      goto unref;
    }

    if ((idVendor != kAtrusUsbVid) || (idProduct != kAtrusUsbPid)) {
      goto unref;
    }

    // The rest of this code is in a new block so that the above gotos do
    // not skip new variables in the same scope.
    {
      struct hidraw_dev* prev = current;
      current = new struct hidraw_dev;
      memset(current, 0, sizeof(*current));
      if (*head == NULL) {
        *head = current;
      }

      size_t dev_node_length = strlen(dev_node_path) + 1;
      current->path = new char[sizeof(*current->path) * dev_node_length];
      strncpy(current->path, dev_node_path, dev_node_length);

      if (prev) {
        prev->next = current;
      }
    }

  unref:
    udev_device_unref(dev);
  }
  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  return 0;
}

static int get_int_value_cmd(int fd,
                             const uint16_t cmd_id,
                             struct report* report,
                             int* out) {
  report->diag_id = cmd_id;
  int retval = get_report(fd, report);
  if (retval < 0) {
    return -1;
  }

  char* endptr;
  errno = 0;
  long result = strtol(report->message, &endptr, 10);
  if (endptr == report->message || *endptr != '\0' ||
      ((result == LONG_MIN || result == LONG_MAX) && errno == ERANGE)) {
    LOG(INFO) << "Error parsing report: " << report->message;
    return -1;
  }
  *out = (int)result;
  return 0;
}

static void get_and_print_cmd(int fd,
                              const uint16_t cmd_id,
                              struct report* report) {
  const char* message = "Error reading report";
  report->diag_id = cmd_id;
  int retval = get_report(fd, report);
  if (retval == 0) {
    message = report->message;
  }
  LOG(INFO) << base::StringPrintf("0x%04X %s", cmd_id, message);
}

static int check_uptime(int fd,
                        const uint16_t cmd_id,
                        struct report* report,
                        bool* changed) {
  static int last_uptime = -1;
  int out, retval;
  retval = get_int_value_cmd(fd, cmd_id, report, &out);
  if (retval < 0) {
    return -1;
  }
  *changed = (out < last_uptime);
  last_uptime = out;
  return 0;
}

static int check_num_slaves(int fd,
                            const uint16_t cmd_id,
                            struct report* report,
                            bool* changed) {
  static int last_num_slaves = -1;
  int out, retval;
  retval = get_int_value_cmd(fd, cmd_id, report, &out);
  if (retval < 0) {
    return -1;
  }
  *changed = (out != last_num_slaves);
  last_num_slaves = out;
  return 0;
}

static error_t parser(int key, char* arg, struct argp_state* state) {
  struct arguments* arguments =
      reinterpret_cast<struct arguments*>(state->input);

  switch (key) {
    case 'd':
      arguments->diag_id = (uint16_t)strtol(arg, NULL, 0);
      break;

    case 'r':
      arguments->report_id = (uint8_t)strtol(arg, NULL, 0);
      break;

    case 'e':
      arguments->extended = true;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }

  return 0;
}

static void diag_sleep(long ns) {
  struct timespec req = {.tv_sec = 0, .tv_nsec = ns};
  struct timespec rem;

  while ((nanosleep(&req, &rem) == 0) && (errno == EINTR)) {
    req = rem;
  }
}

int cmd_diagnose(int argc, char** argv) {
  char doc[] =
      "\n"
      "Options:";
  struct argp_option options[] = {
      {"diag", 'd', "id", 0, "Diagnostic ID", 0},
      {"report", 'r', "id", 0, "HID report ID", 0},
      {"extended", 'e', NULL, 0, "Extended report", 0},
      {0}};
  struct argp argp = {options, parser, NULL, doc, NULL, NULL, NULL};
  struct arguments arguments = {
      .report_id = 0x07, .diag_id = 0, .extended = false};

  int retval = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
  if (retval < 0) {
    return -1;
  }

  struct hidraw_dev* devs;
  get_hidraw_devices(&devs);

  struct hidraw_dev* current = devs;
  while (current) {
    int fd = open(current->path, O_RDWR);
    if (fd < 0) {
      perror(current->path);
      current = current->next;
      continue;
    }

    struct report report;
    report.report_id = arguments.report_id;
    char buf[report_length - report_header_length];
    report.message = buf;
    const uint16_t* cmd;
    if (arguments.diag_id != 0) {
      // custom command
      get_and_print_cmd(fd, arguments.diag_id, &report);
    } else {
      bool changed;
      const struct diag_trigger* trig;
      for (trig = triggers; trig->check; ++trig) {
        retval = trig->check(fd, trig->cmd, &report, &changed);
        if (retval == 0 && changed) {
          arguments.extended = true;
        }
      }

      long kDiagnosticDelayNs = 1000000;
      for (cmd = commands; *cmd != 0; ++cmd) {
        get_and_print_cmd(fd, *cmd, &report);
        // Temporary hack to mitigate problem with firmware <= 0.6.7
        diag_sleep(kDiagnosticDelayNs);
      }
      if (arguments.extended) {
        for (cmd = commands_extended; *cmd != 0; ++cmd) {
          get_and_print_cmd(fd, *cmd, &report);
          // Temporary hack to mitigate problem with firmware <= 0.6.7
          diag_sleep(kDiagnosticDelayNs);
        }
      }
    }

    close(fd);

    current = current->next;
  }

  current = devs;
  while (current) {
    struct hidraw_dev* next = current->next;
    delete[] current->path;
    delete current;
    current = next;
  }

  return 0;
}

}  // namespace atrusctl
