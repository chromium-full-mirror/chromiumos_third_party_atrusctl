/*
 * Copyright 2017 Limes Audio AB. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "util.h"

void
bcd_to_version(uint16_t bcd, int *major, int *minor, int *patch)
{
    *major = (bcd >> 8) & 0xFF;
    *minor = (bcd >> 4) & 0xF;
    *patch = bcd & 0xF;
}
