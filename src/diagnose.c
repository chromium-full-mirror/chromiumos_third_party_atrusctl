/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "diagnose.h"
#include "device.h"

#include <argp.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

struct arguments {
    uint8_t report_id;
    uint16_t diag_id;
};

struct hidraw_dev {
    char *path;
    char *serial;
    struct hidraw_dev *next;
};

struct diag_command {
    uint16_t id;
    const char *str;
};

static struct diag_command commands[] = {{0x1230, "SERIAL_MASTER"},
                                         {0x1231, "SERIAL_SLAVE_UP0"},
                                         {0x1232, "SERIAL_SLAVE_UP1"},
                                         {0x1233, "SERIAL_SLAVE_UP2"},
                                         {0x1234, "SERIAL_SLAVE_UP3"},
                                         {0x1235, "SERIAL_SLAVE_DOWN0"},
                                         {0x1236, "SERIAL_SLAVE_DOWN1"},
                                         {0x1237, "SERIAL_SLAVE_DOWN2"},
                                         {0x1238, "SERIAL_SLAVE_DOWN3"},
                                         {0x1300, "VERSION_MASTER"},
                                         {0x1310, "VERSION_SLAVE_UP0"},
                                         {0x1311, "VERSION_SLAVE_UP1"},
                                         {0x1312, "VERSION_SLAVE_UP2"},
                                         {0x1313, "VERSION_SLAVE_UP3"},
                                         {0x1314, "VERSION_SLAVE_DOWN0"},
                                         {0x1315, "VERSION_SLAVE_DOWN1"},
                                         {0x1316, "VERSION_SLAVE_DOWN2"},
                                         {0x1317, "VERSION_SLAVE_DOWN3"},
                                         {0x1350, "DAISY_MIN_VERSION"},
                                         {0x1351, "DAISY_NUM_SLAVES"},
                                         {0x1500, "DAISY_DFU_STATUS"},
                                         {0x1600, "STATE_MUTED"},
                                         {0x1601, "STATE_HOOK_OFF"},
                                         {0x1602, "STATE_VOLUME"},
                                         {0x1603, "STATE_LED"},
                                         {0x1604, "STATE_UPTIME"},
                                         {0x1605, "STATE_SHARC_ALIVE"},
                                         {0x5001, "SPI_QUEUE_STATUS"},
                                         {0}};

struct report {
    uint8_t report_id;
    uint16_t diag_id;
    char *message;
};

static const size_t report_header_length = 3;
static const size_t report_length = 64;

static void
report_pack(struct report *report, char *buf)
{
    buf[0] = report->report_id;
    buf[1] = report->diag_id;
    buf[2] = report->diag_id >> 8;
}

static void
report_unpack(struct report *report, char *buf)
{
    report->report_id = buf[0];
    report->diag_id = (buf[2] << 8) | buf[1];
    report->message = &buf[3];
}

static int
get_report(int fd, struct report *report)
{
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
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
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

    strncpy(report->message, read_report.message,
            report_length - report_header_length);

    return 0;
}

static int
str_to_uint16(const char *str, uint16_t *val)
{
    int retval = sscanf(str, "%" SCNx16, val);
    return (retval == EOF || retval < 0) ? -1 : 0;
}

static int
get_hidraw_devices(struct hidraw_dev **head)
{
    *head = NULL;

    struct udev *udev = udev_new();
    if (!udev) {
        return -1;
    }
    struct udev_enumerate *enumerate = udev_enumerate_new(udev);
    if (!enumerate) {
        return -1;
    }
    udev_enumerate_add_match_subsystem(enumerate, "hidraw");
    udev_enumerate_scan_devices(enumerate);

    struct hidraw_dev *current = NULL;
    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices)
    {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(udev, path);
        const char *dev_node_path = udev_device_get_devnode(dev);

        struct udev_device *parent;
        parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb",
                                                               "usb_device");
        if (!parent) {
            continue;
        }

        uint16_t idVendor;
        const char *attr = udev_device_get_sysattr_value(parent, "idVendor");
        int retval = str_to_uint16(attr, &idVendor);
        if (retval < 0) {
            goto unref;
        }

        uint16_t idProduct;
        attr = udev_device_get_sysattr_value(parent, "idProduct");
        retval = str_to_uint16(attr, &idProduct);
        if (retval < 0) {
            goto unref;
        }

        if ((idVendor != USB_VID) || (idProduct != USB_PID)) {
            goto unref;
        }

        struct hidraw_dev *prev = current;
        current = calloc(1, sizeof(*current));
        if (*head == NULL) {
            *head = current;
        }

        size_t dev_node_length = strlen(dev_node_path) + 1;
        current->path = malloc(sizeof(*current->path) * dev_node_length);
        strncpy(current->path, dev_node_path, dev_node_length);

        const char *serial = udev_device_get_sysattr_value(parent, "serial");
        size_t serial_length = strlen(serial) + 1;
        current->serial = malloc(sizeof(*current->serial) * serial_length);
        strncpy(current->serial, serial, serial_length);

        if (prev) {
            prev->next = current;
        }

    unref:
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);

    return 0;
}

static void
get_and_print_cmd(int fd, struct diag_command *cmd, struct report *report)
{
    const char *message = "Error reading report";
    int retval = get_report(fd, report);
    if (retval == 0) {
        message = report->message;
    }
    printf("0x%04X %s: %s\n", cmd->id, cmd->str, message);
}

static error_t
parser(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments = state->input;

    switch (key) {
        case 'd':
            arguments->diag_id = (uint16_t)strtol(arg, NULL, 0);
            break;

        case 'r':
            arguments->report_id = (uint8_t)strtol(arg, NULL, 0);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

int
diagnose(int argc, char **argv)
{
    char doc[] =
            "\n"
            "Options:";
    struct argp_option options[] = {
            {"diag", 'd', "id", 0, "Diagnostic ID", 0},
            {"report", 'r', "id", 0, "HID report ID", 0},
            {0}};
    struct argp argp = {options, parser, NULL, doc, NULL, NULL, NULL};
    struct arguments arguments = {
            .report_id = 0x07, .diag_id = 0,
    };

    int retval = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
    if (retval < 0) {
        return -1;
    }

    struct hidraw_dev *devs;
    get_hidraw_devices(&devs);

    struct hidraw_dev *current = devs;
    while (current) {
        int fd = open(current->path, O_RDWR);
        if (fd < 0) {
            perror(current->path);
            current = current->next;
            continue;
        }

        printf("Diagnostics from %s\n", current->serial);

        struct report report;
        report.report_id = arguments.report_id;
        char buf[report_length - report_header_length];
        report.message = buf;
        struct diag_command *cmd;
        if (arguments.diag_id != 0) {
            struct diag_command custom_cmd = {.id = arguments.diag_id,
                                              .str = "UNKNOWN"};
            for (cmd = commands; (*cmd).id; ++cmd) {
                if (cmd->id == custom_cmd.id) {
                    custom_cmd.str = cmd->str;
                }
            }
            report.diag_id = custom_cmd.id;
            get_and_print_cmd(fd, &custom_cmd, &report);
        } else {
            for (cmd = commands; (*cmd).id; ++cmd) {
                report.diag_id = cmd->id;
                get_and_print_cmd(fd, cmd, &report);
            };
        }

        close(fd);

        current = current->next;
    }

    current = devs;
    while (current) {
        struct hidraw_dev *next = current->next;
        free(current->path);
        free(current->serial);
        free(current);
        current = next;
    }

    return 0;
}
