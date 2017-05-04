/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "upgrade.h"
#include "crc32.h"
#include "device.h"
#include "util.h"

#include <argp.h>
#include <libusb-1.0/libusb.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

enum dfu_state {
    STATE_appIDLE = 0x0,
    STATE_appDETACH = 0x1,
    STATE_dfuIDLE = 0x2,
    STATE_dfuDNLOAD_SYNC = 0x3,
    STATE_dfuDNBUSY = 0x4,
    STATE_dfuDNLOAD_IDLE = 0x5,
    STATE_dfuMANIFEST_SYNC = 0x6,
    STATE_dfuMANIFEST = 0x7,
    STATE_dfuMANIFEST_WAIT_RESET = 0x8,
    STATE_dfuUPLOAD_IDLE = 0x9,
    STATE_dfuERROR = 0xA
};

enum dfu_status {
    STATUS_OK = 0x00,
    STATUS_errTARGET = 0x01,
    STATUS_errFILE = 0x02,
    STATUS_errWRITE = 0x03,
    STATUS_errERASE = 0x04,
    STATUS_errCHECK_ERASED = 0x05,
    STATUS_errPROG = 0x06,
    STATUS_errVERIFY = 0x07,
    STATUS_errADDRESS = 0x08,
    STATUS_errNOTDONE = 0x09,
    STATUS_errFIRMWARE = 0x0A,
    STATUS_errVENDOR = 0x0B,
    STATUS_errUSBR = 0x0C,
    STATUS_errPOR = 0x0D,
    STATUS_errUNKNOWN = 0x0E,
    STATUS_errSTALLEDPKT = 0x0F
};

struct dfu_suffix {
    uint32_t dwCRC;
    uint8_t bLength;
    uint8_t ucDfuSignature[3];
    uint16_t bcdDFU;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
};

struct dfu_descriptor_attributes {
    uint8_t bitCanDnload : 1;
    uint8_t bitCanUpload : 1;
    uint8_t bitManifestationTolerant : 1;
    uint8_t bitWillDetach : 1;
};

struct dfu_descriptor {
    uint8_t bLength;
    uint8_t bDescriptorType;
    struct dfu_descriptor_attributes bmAttributes;
    uint16_t wDetachTimeOut;
    uint16_t wTransferSize;
    uint16_t bcdDFUVersion;
};

struct dfu_status_request {
    uint8_t bStatus;
    uint32_t bwPollTimeout;
    uint8_t bState;
    uint8_t iString;
};

struct arguments {
    FILE *file;
    struct dfu_suffix dfu_suffix;
    bool force;
    bool retry;
};

static const char *
status_to_string(enum dfu_status status)
{
    switch (status) {
        case STATUS_OK:
            return "No error condition is present";
        case STATUS_errTARGET:
            return "File is not targeted for use by this device";
        case STATUS_errFILE:
            return "File is for this device but fails some vendor-specific "
                   "verification test";
        case STATUS_errWRITE:
            return "Device is unable to write memory";
        case STATUS_errERASE:
            return "Memory erase function failed";
        case STATUS_errCHECK_ERASED:
            return "Memory erase check failed";
        case STATUS_errPROG:
            return "Program memory function failed";
        case STATUS_errVERIFY:
            return "Programmed memory failed verification";
        case STATUS_errADDRESS:
            return "Cannot program memory due to received address that is out "
                   "of range";
        case STATUS_errNOTDONE:
            return "Received DFU_DNLOAD with wLength = 0, but device does not "
                   "think it has all of the data yet";
        case STATUS_errFIRMWARE:
            return "Device's firmware is corrupt. It cannot return to run-time "
                   "(non-DFU) operations";
        case STATUS_errVENDOR:
            return "errVENDOR";
        case STATUS_errUSBR:
            return "Device detected unexpected USB reset signaling";
        case STATUS_errPOR:
            return "Device detected unexpected power on reset";
        case STATUS_errUNKNOWN:
            return "Something went wrong, but the device does not know what it "
                   "was";
        case STATUS_errSTALLEDPKT:
            return "Device stalled an unexpected request";
    }

    return "";
}

static int
parse_dfu_suffix(FILE *file, struct dfu_suffix *suffix)
{
    if (fseek(file, -16, SEEK_END) < 0) {
        return -1;
    }
    size_t s;
    s = fread(&suffix->bcdDevice, sizeof(suffix->bcdDevice), 1, file);
    s = fread(&suffix->idProduct, sizeof(suffix->idProduct), 1, file);
    s = fread(&suffix->idVendor, sizeof(suffix->idVendor), 1, file);
    s = fread(&suffix->bcdDFU, sizeof(suffix->bcdDFU), 1, file);
    s = fread(&suffix->ucDfuSignature[2], sizeof(*suffix->ucDfuSignature), 1,
              file);
    s = fread(&suffix->ucDfuSignature[1], sizeof(*suffix->ucDfuSignature), 1,
              file);
    s = fread(&suffix->ucDfuSignature[0], sizeof(*suffix->ucDfuSignature), 1,
              file);
    s = fread(&suffix->bLength, sizeof(suffix->bLength), 1, file);
    s = fread(&suffix->dwCRC, sizeof(suffix->dwCRC), 1, file);

    long to_read = ftell(file) - 4;
    rewind(file);
    uint32_t crc = 0xFFFFFFFF;
    for (long i = 0; i < to_read; ++i) {
        crc = crc32(crc, (uint8_t)fgetc(file));
    }
    if (suffix->dwCRC != crc) {
        return -1;
    }

    if (!((suffix->ucDfuSignature[0] == 'D')
          && (suffix->ucDfuSignature[1] == 'F')
          && (suffix->ucDfuSignature[2] == 'U'))) {
        return -1;
    }

    return 0;
}

static bool
is_correct_usb_id(struct dfu_suffix *suffix)
{
    return ((suffix->idVendor == USB_VID) && (suffix->idProduct == USB_PID));
}

static error_t
parser(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments =
        reinterpret_cast<struct arguments*>(state->input);

    switch (key) {
        case 'f':
            arguments->force = true;
            break;

        case 'r':
            arguments->retry = true;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) {
                argp_usage(state);
            }

            arguments->file = fopen(arg, "rb");
            if (!arguments->file) {
                argp_failure(state, 1, errno, "%s", arg);
            }
            if (parse_dfu_suffix(arguments->file, &arguments->dfu_suffix) < 0) {
                argp_error(state, "%s: Not a valid firmware bundle", arg);
            }
            if (!is_correct_usb_id(&arguments->dfu_suffix)) {
                argp_error(state,
                           "%s: The firmware bundle is not for an Atrus device",
                           arg);
            }
            break;

        case ARGP_KEY_END:
            if (state->arg_num < 1) {
                argp_usage(state);
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static int
dfu_detach(libusb_device_handle *device, uint16_t interface, uint16_t timeout)
{
    uint8_t bmRequestType = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS
                            | LIBUSB_RECIPIENT_INTERFACE;
    return libusb_control_transfer(device, bmRequestType, 0x0, timeout,
                                   interface, NULL, 0, 10000);
}

static int
dfu_dnload(libusb_device_handle *device,
           uint16_t interface,
           uint16_t block,
           uint8_t *data,
           uint16_t length)
{
    uint8_t bmRequestType = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS
                            | LIBUSB_RECIPIENT_INTERFACE;
    return libusb_control_transfer(device, bmRequestType, 0x1, block, interface,
                                   data, length, 10000);
}

static int
dfu_get_status(libusb_device_handle *device,
               uint16_t interface,
               struct dfu_status_request *status)
{
    uint8_t buffer[6] = {0};

    uint8_t bmRequestType = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS
                            | LIBUSB_RECIPIENT_INTERFACE;
    int retval = libusb_control_transfer(device, bmRequestType, 0x3, 0,
                                         interface, buffer, 6, 10000);

    status->bStatus = buffer[0];
    status->bwPollTimeout =
            (uint32_t)((buffer[3] << 16) | (buffer[2] << 8) | buffer[1]);
    status->bState = buffer[4];
    status->iString = buffer[5];

    if (is_debug) {
        psyslog(LOG_DEBUG, "DFU retval=%d, state=0x%X, status=0x%02X", retval,
                status->bState, status->bStatus);
    }

    return retval;
}

static int
dfu_clr_status(libusb_device_handle *device, uint16_t interface)
{
    uint8_t bmRequestType = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS
                            | LIBUSB_RECIPIENT_INTERFACE;
    return libusb_control_transfer(device, bmRequestType, 0x4, 0, interface,
                                   NULL, 0, 10000);
}

static int
dfu_abort(libusb_device_handle *device, uint16_t interface)
{
    uint8_t bmRequestType = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS
                            | LIBUSB_RECIPIENT_INTERFACE;
    return libusb_control_transfer(device, bmRequestType, 0x6, 0, interface,
                                   NULL, 0, 10000);
}

static int
version_compare(uint16_t a, uint16_t b)
{
    int va[3];
    int vb[3];

    bcd_to_version(a, &va[0], &va[1], &va[2]);
    bcd_to_version(b, &vb[0], &vb[1], &vb[2]);

    for (int i = 0; i < 3; ++i) {
        if (va[i] > vb[i]) {
            return 1;
        } else if (va[i] < vb[i]) {
            return -1;
        }
    }

    return 0;
}

static bool
get_dfu_descriptor(libusb_device *device,
                   struct libusb_device_descriptor *device_desc,
                   uint8_t *interface,
                   struct dfu_descriptor *dfu_desc)
{
    for (uint8_t i = 0; i != device_desc->bNumConfigurations; ++i) {
        struct libusb_config_descriptor *config_desc;
        if (libusb_get_config_descriptor(device, i, &config_desc) < 0) {
            continue;
        }
        for (uint8_t j = 0; j < config_desc->bNumInterfaces; ++j) {
            const struct libusb_interface *iface = &config_desc->interface[j];
            if (!iface) {
                break;
            }
            for (uint8_t k = 0; k < iface->num_altsetting; ++k) {
                const struct libusb_interface_descriptor *iface_desc;
                iface_desc = &iface->altsetting[k];
                if (iface_desc->bInterfaceClass == LIBUSB_CLASS_APPLICATION
                    && iface_desc->bInterfaceSubClass == 1) {
                    if (sizeof(*dfu_desc) < (size_t)iface_desc->extra_length) {
                        return false;
                    }

                    *interface = j;
                    memcpy(dfu_desc, iface_desc->extra,
                           (size_t)iface_desc->extra_length);

                    return true;
                }
            }
        }
        libusb_free_config_descriptor(config_desc);
    }

    return false;
}

static void
set_devices_in_dfu_mode(libusb_context *context,
                        struct dfu_suffix *dfu_suffix,
                        bool only_lower_version)
{
    libusb_device **devices;
    ssize_t num_devices = libusb_get_device_list(context, &devices);
    for (ssize_t i = 0; i < num_devices; ++i) {
        struct libusb_device_descriptor device_desc;
        libusb_device *device = devices[i];

        if (libusb_get_device_descriptor(device, &device_desc) < 0) {
            continue;
        }
        if ((device_desc.idVendor != dfu_suffix->idVendor)
            || (device_desc.idProduct != dfu_suffix->idProduct)) {
            continue;
        }
        uint8_t interface;
        struct dfu_descriptor dfu_desc;
        if (!get_dfu_descriptor(device, &device_desc, &interface, &dfu_desc)) {
            continue;
        }

        libusb_device_handle *handle;
        int retval = libusb_open(device, &handle);
        if (retval < 0) {
            continue;
        }

        int config;
        if (only_lower_version
            && (version_compare(device_desc.bcdDevice, dfu_suffix->bcdDevice)
                >= 0)) {
            int major, minor, patch;
            bcd_to_version(device_desc.bcdDevice, &major, &minor, &patch);
            psyslog(LOG_INFO, "Skipping device with version %d.%d.%d", major,
                    minor, patch);
            goto release_handle;
        }

        if (libusb_get_configuration(handle, &config) < 0) {
            goto release_handle;
        }
        if (config == 0) {
            retval = libusb_set_configuration(handle, 1);
            if (retval < 0) {
                goto release_handle;
            }
        }
        retval = libusb_claim_interface(handle, interface);
        if (retval < 0) {
            goto release_handle;
        }

        psyslog(LOG_INFO, "Setting device in DFU mode");
        dfu_detach(handle, interface, 16);
        libusb_reset_device(handle);

        libusb_release_interface(handle, interface);
    release_handle:
        libusb_close(handle);
    }
}

static void
on_dfu_error(libusb_device_handle *handle, uint16_t interface)
{
    struct dfu_status_request status_req;

    int retval = dfu_get_status(handle, interface, &status_req);
    if (retval < 0) {
        psyslog(LOG_ERR, "Could not get status of the device");
        return;
    }

    enum dfu_state state = (enum dfu_state)status_req.bState;
    if (state != STATE_dfuERROR) {
        return;
    }
    enum dfu_status status = (enum dfu_status)status_req.bStatus;
    if (status == STATUS_OK) {
        return;
    }

    size_t string_length = 256;
    char status_string[string_length];
    if (status == STATUS_errVENDOR) {
        retval = libusb_get_string_descriptor_ascii(
                handle, status_req.iString, (unsigned char *)status_string,
                (int)string_length);
        if (retval < 0) {
            strncpy(status_string, "Could not get vendor defined status string",
                    string_length);
        }
    } else {
        strncpy(status_string, status_to_string(status), string_length);
    }
    psyslog(LOG_ERR, "%s", status_string);

    dfu_clr_status(handle, interface);
}

static int
get_status_until_state(libusb_device_handle *handle,
                       uint16_t interface,
                       struct dfu_status_request *status_req,
                       enum dfu_state state)
{
    int retries = 0;
    const int retry_limit = 100;

    while (true) {
        int retval = dfu_get_status(handle, interface, status_req);
        if (retval < 0) {
            return -1;
        }
        if (status_req->bState == STATE_dfuERROR) {
            on_dfu_error(handle, interface);
            return -1;
        }

        if (status_req->bState == state) {
            break;
        }

        retries += 1;
        if (retries >= retry_limit) {
            return -1;
        }

        timeout(status_req->bwPollTimeout);
    };

    return 0;
}

static int
download_file(libusb_device_handle *handle, uint16_t interface, FILE *file)
{
    struct dfu_status_request status_req;

    int retval = dfu_get_status(handle, interface, &status_req);
    if ((retval < 0) || (status_req.bState != STATE_dfuIDLE)) {
        return -1;
    }

    uint16_t block = 0;
    size_t buffer_length = 64;
    enum dfu_state expected_state = STATE_dfuDNLOAD_IDLE;
    size_t bytes_read;

    rewind(file);
    do {
        uint8_t buffer[buffer_length];
        bytes_read = fread(buffer, sizeof(*buffer), buffer_length, file);
        if (bytes_read < buffer_length) {
            if (ferror(file) < 0) {
                return -1;
            }
            if (feof(file)) {
                expected_state = STATE_dfuIDLE;
            }
        }

        retval = dfu_get_status(handle, interface, &status_req);
        if (retval < 0) {
            return -1;
        }
        if ((enum dfu_state)status_req.bState == STATE_dfuERROR) {
            on_dfu_error(handle, interface);
            return -1;
        }

        retval = dfu_dnload(handle, interface, block++, buffer,
                            (uint16_t)bytes_read);
        if (retval < 0) {
            return -1;
        }

        timeout(status_req.bwPollTimeout);
    } while (bytes_read != 0);

    retval = get_status_until_state(handle, interface, &status_req,
                                    expected_state);

    return retval;
}

static int
perform_dfu(libusb_context *context,
            struct dfu_suffix *dfu_suffix,
            FILE *file,
            int *num_successful)
{
    int num_found = 0;
    *num_successful = 0;

    libusb_device **devices;
    ssize_t num_devices = libusb_get_device_list(context, &devices);
    for (ssize_t i = 0; i < num_devices; ++i) {
        struct libusb_device_descriptor device_desc;
        libusb_device *device = devices[i];

        if (libusb_get_device_descriptor(device, &device_desc) < 0) {
            continue;
        }
        if ((device_desc.idVendor != dfu_suffix->idVendor)
            || (device_desc.idProduct != (dfu_suffix->idProduct + 1))) {
            continue;
        }
        uint8_t interface;
        struct dfu_descriptor dfu_desc;
        if (!get_dfu_descriptor(device, &device_desc, &interface, &dfu_desc)) {
            continue;
        }

        ++num_found;

        libusb_device_handle *handle;
        int retval = libusb_open(device, &handle);
        if (retval < 0) {
            continue;
        }

        if (!dfu_desc.bmAttributes.bitCanDnload
            || !dfu_desc.bmAttributes.bitManifestationTolerant
            || dfu_desc.bmAttributes.bitWillDetach) {
            psyslog(LOG_ERR,
                    "Found device with unsupported USB DFU interface "
                    "descriptor");
            goto release_handle;
        }

        retval = libusb_set_configuration(handle, 1);
        if (retval < 0) {
            goto release_handle;
        }
        retval = libusb_claim_interface(handle, interface);
        if (retval < 0) {
            goto release_handle;
        }

        // The rest of this code is in a new block so that the above gotos do
        // not skip new variables in the same scope.
        {
            int major, minor, patch;
            bcd_to_version(device_desc.bcdDevice, &major, &minor, &patch);
            psyslog(LOG_INFO, "Upgrading device from version %d.%d.%d", major,
                    minor, patch);
            const int max_retries = 3;
            int retries = 0;
            do {
                // Check that the device is in a proper state for beginning
                // download
                struct dfu_status_request status_req;
                dfu_get_status(handle, interface, &status_req);
                switch (status_req.bState) {
                    case STATE_dfuIDLE:
                    case STATE_dfuDNLOAD_SYNC:
                    case STATE_dfuDNLOAD_IDLE:
                    case STATE_dfuMANIFEST_SYNC:
                    case STATE_dfuUPLOAD_IDLE:
                        dfu_abort(handle, interface);
                        break;
                    default:
                        break;
                }
                dfu_get_status(handle, interface, &status_req);
                if (status_req.bState == STATE_dfuERROR) {
                    // Silently ignore any previous error state and reset to
                    // dfuIDLE
                    dfu_clr_status(handle, interface);
                    dfu_get_status(handle, interface, &status_req);
                }
                if (status_req.bState != STATE_dfuIDLE) {
                    psyslog(LOG_ERR,
                            "Device is not in a proper state for upgrading");
                    goto usb_reset;
                }

                retval = download_file(handle, interface, file);
                if (retval < 0) {
                    psyslog(LOG_WARNING, "Upgrade failed on device");
                    ++retries;
                    if (retries <= max_retries) {
                        psyslog(LOG_WARNING,
                                "Retrying upgrade on device, attempt %d/%d",
                                retries, max_retries);
                    }
                } else {
                    psyslog(LOG_INFO, "Upgrade successful on device");
                    ++(*num_successful);
                }
            } while ((retval < 0) && (retries <= max_retries));
        }

    usb_reset:
        libusb_reset_device(handle);
        libusb_release_interface(handle, interface);
    release_handle:
        libusb_close(handle);
    }

    return num_found;
}

int
cmd_upgrade(int argc, char **argv)
{
    char args_doc[] = "FILE";
    char doc[] =
            "\n"
            "Options:";
    struct argp_option options[] = {
            {"force", 'f', NULL, 0,
             "Try to upgrade devices with newer version than FILE", 0},
            {"retry", 'r', NULL, 0, "Retry if no devices are found", 0},
            {0}};
    struct argp argp = {options, parser, args_doc, doc, NULL, NULL, NULL};
    struct arguments arguments = {0};
    libusb_context *context;

    int retval = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &arguments);
    if (retval < 0) {
        goto close_file;
    }
    retval = libusb_init(&context);
    if (retval < 0) {
        goto close_file;
    }

    libusb_set_debug(NULL, LIBUSB_LOG_LEVEL_WARNING);
    libusb_set_debug(context, LIBUSB_LOG_LEVEL_WARNING);

    // The rest of this code is in a new block so that the above gotos do not
    // skip new variables in the same scope.
    {
        int major, minor, patch;
        bcd_to_version(arguments.dfu_suffix.bcdDevice, &major, &minor, &patch);
        psyslog(LOG_INFO, "Starting upgrade to version %d.%d.%d", major, minor,
                patch);

        struct dfu_suffix suffix = arguments.dfu_suffix;
        set_devices_in_dfu_mode(context, &suffix, !arguments.force);

        // Make sure devices have time to re-enumerate in DFU mode
        timeout(1000);

        unsigned int backoff = 1;
        const unsigned int limit = 64;
        int num_successful = 0;
        do {
            retval =
                perform_dfu(context, &suffix, arguments.file, &num_successful);
            if (retval > 0) {
                break;
            }
            if (!arguments.retry) {
                break;
            }

            psyslog(LOG_WARNING,
                    "No devices in DFU mode found, retrying in %u s", backoff);
            sleep(backoff);
            backoff *= 2;
        } while ((retval <= 0) && (backoff < limit));

        if (retval > 0) {
            retval = 0;
        }
        psyslog(LOG_INFO, "Upgrade succeeded on %d device(s)", num_successful);
    }

    libusb_exit(context);

close_file:
    if (arguments.file) {
        fclose(arguments.file);
    }

    return retval;
}
