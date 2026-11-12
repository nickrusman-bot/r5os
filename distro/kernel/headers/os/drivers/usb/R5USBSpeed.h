/*
 * Copyright 2026, R5OS Contributors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * The bus speed of a USB device, through the existing usb_ioctl() of the
 * USB module (v3). Added by R5; the module structure is unchanged.
 *
 *	uint32 value = (uint32)device;	// in: the usb_device
 *	if (gUSBModule->usb_ioctl(B_USB_IOCTL_GET_DEVICE_SPEED, &value,
 *			sizeof(value)) == B_OK) {
 *		// out: value is one of the B_USB_SPEED_* constants
 *	}
 *
 * A stack without this request returns B_DEV_INVALID_IOCTL. The numbers
 * match the bus manager's own usb_speed and do not change.
 */
#ifndef _R5_USB_SPEED_H
#define _R5_USB_SPEED_H


#define B_USB_IOCTL_GET_DEVICE_SPEED	'DSPD'

enum {
	B_USB_SPEED_LOW = 0,
	B_USB_SPEED_FULL = 1,
	B_USB_SPEED_HIGH = 2,
	B_USB_SPEED_SUPER = 3,
	B_USB_SPEED_SUPER_PLUS = 4
};


#endif	// _R5_USB_SPEED_H
