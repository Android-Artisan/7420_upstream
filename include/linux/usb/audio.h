/*
 * <linux/usb/audio.h> -- USB Audio definitions.
 *
 * Copyright (C) 2006 Thumtronics Pty Ltd.
 * Developed for Thumtronics by Grey Innovation
 * Ben Williamson <ben.williamson@greyinnovation.com>
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 2, as published by the Free Software Foundation.
 *
 * This file holds USB constants and structures defined
 * by the USB Device Class Definition for Audio Devices.
 * Comments below reference relevant sections of that document:
 *
 * http://www.usb.org/developers/devclass_docs/audio10.pdf
 */

#ifndef __LINUX_USB_AUDIO_H
#define __LINUX_USB_AUDIO_H

#include <linux/types.h>

/* A.2 Audio Interface Subclass Codes */
#define USB_SUBCLASS_AUDIOCONTROL	0x01
#define USB_SUBCLASS_AUDIOSTREAMING	0x02
#define USB_SUBCLASS_MIDISTREAMING	0x03

/* 4.3.2  Class-Specific AC Interface Descriptor */
struct usb_ac_header_descriptor {
	__u8  bLength;			/* 8+n */
	__u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	__u8  bDescriptorSubtype;	/* USB_MS_HEADER */
	__le16 bcdADC;			/* 0x0100 */
	__le16 wTotalLength;		/* includes Unit and Terminal desc. */
	__u8  bInCollection;		/* n */
	__u8  baInterfaceNr[];		/* [n] */
} __attribute__ ((packed));

#define USB_DT_AC_HEADER_SIZE(n)	(8+(n))

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_USB_AC_HEADER_DESCRIPTOR(n) 			\
struct usb_ac_header_descriptor_##n {				\
	__u8  bLength;						\
	__u8  bDescriptorType;					\
	__u8  bDescriptorSubtype;				\
	__le16 bcdADC;						\
	__le16 wTotalLength;					\
	__u8  bInCollection;					\
	__u8  baInterfaceNr[n];					\
} __attribute__ ((packed))
 *
 * Types and defines in this file are either specific to version 1.0 of
 * this standard or common for newer versions.
 */
#ifndef __LINUX_USB_AUDIO_H
#define __LINUX_USB_AUDIO_H

#include <uapi/linux/usb/audio.h>


struct usb_audio_control {
	struct list_head list;
	const char *name;
	u8 type;
	int data[5];
	int (*set)(struct usb_audio_control *con, u8 cmd, int value);
	int (*get)(struct usb_audio_control *con, u8 cmd);
};

struct usb_audio_control_selector {
	struct list_head list;
	struct list_head control;
	u8 id;
	const char *name;
	u8 type;
	struct usb_descriptor_header *desc;
};

#endif /* __LINUX_USB_AUDIO_H */
