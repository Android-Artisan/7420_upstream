/*
 * Project Name JPEG DRIVER IN Linux
 * Copyright  2007 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __JPEG_DRIVER_H__
#define __JPEG_DRIVER_H__

#define MAX_INSTANCE_NUM	10

#define IOCTL_JPG_DECODE				0x00000002
#define IOCTL_JPG_ENCODE				0x00000003
#define IOCTL_JPG_SET_STRBUF			0x00000004
#define IOCTL_JPG_SET_FRMBUF			0x00000005
#define IOCTL_JPG_SET_THUMB_STRBUF		0x0000000A
#define IOCTL_JPG_SET_THUMB_FRMBUF		0x0000000B

#endif /*__JPEG_DRIVER_H__*/
