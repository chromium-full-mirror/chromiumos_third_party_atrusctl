/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "list.h"
#include "device.h"
#include "util.h"

#include <argp.h>
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <string.h>

static void
print_info(libusb_context *context)
{
    libusb_device **devices;

    int length = printf("%-20s %s\n", "Serial number", "Release number");
    for (int i = 0; i < (length - 1); ++i) {
        putchar('-');
    }
    putchar('\n');

    ssize_t num_devices = libusb_get_device_list(context, &devices);
    for (ssize_t i = 0; i < num_devices; ++i) {
        struct libusb_device_descriptor device_desc;
        libusb_device *device = devices[i];

        if (libusb_get_device_descriptor(device, &device_desc) < 0) {
            continue;
        }
        if ((device_desc.idVendor != USB_VID)
            || (device_desc.idProduct != USB_PID)) {
            continue;
        }

        libusb_device_handle *handle;
        int retval = libusb_open(device, &handle);
        if (retval < 0) {
            continue;
        }

        size_t string_length = 256;
        char serial[string_length];
        retval = libusb_get_string_descriptor_ascii(
                handle, device_desc.iSerialNumber, (unsigned char *)serial,
                (int)string_length);
        if (retval == LIBUSB_ERROR_INVALID_PARAM) {
            strncpy(serial, "<unknown>", string_length);
        } else if (retval < 0) {
            continue;
        }

        libusb_close(handle);

        int major, minor, patch;
        bcd_to_version(device_desc.bcdDevice, &major, &minor, &patch);

        printf("%-20s %d.%d.%d\n", serial, major, minor, patch);
    }
}

int
list(int argc, char **argv)
{
    int retval = argp_parse(NULL, argc, argv, 0, NULL, NULL);
    if (retval < 0) {
        goto exit;
    }

    libusb_context *context;
    retval = libusb_init(&context);
    if (retval < 0) {
        goto exit;
    }

    libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_WARNING);
    libusb_set_debug(context, LIBUSB_LOG_LEVEL_WARNING);

    print_info(context);

    libusb_exit(context);

exit:
    return retval;
}
