/*
 * Copyright (C) 2009 by Bart Hartgers (bart.hartgers+ark3116@gmail.com)
 * Original version:
 * Copyright (C) 2006
 *   Simon Schulz (ark3116_driver <at> auctionant.de)
 *
 * ark3116
 * - implements a driver for the arkmicro ark3116 chipset (vendor=0x6547,
 *   productid=0x0232) (used in a datacable called KQ-U8A)
 *
 * - based on code by krisfx -> thanks !!
 *   (see http://www.linuxquestions.org/questions/showthread.php?p=2184457#post2184457)
 *
 *  - based on logs created by usbsnoopy
 * Supports full modem status lines, break, hardware flow control. Does not
 * support software flow control, since I do not know how to enable it in hw.
 *
 * This driver is a essentially new implementation. I initially dug
 * into the old ark3116.c driver and suddenly realized the ark3116 is
 * a 16450 with a USB interface glued to it. See comments at the
 * bottom of this file.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/ioctl.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include <linux/uaccess.h>


static int debug;

static struct usb_device_id id_table [] = {
	{ USB_DEVICE(0x6547, 0x0232) },
#include <linux/serial_reg.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#define DRIVER_AUTHOR "Bart Hartgers <bart.hartgers+ark3116@gmail.com>"
#define DRIVER_DESC "USB ARK3116 serial/IrDA driver"
#define DRIVER_DEV_DESC "ARK3116 RS232/IrDA"
#define DRIVER_NAME "ark3116"

/* usb timeout of 1 second */
#define ARK_TIMEOUT 1000

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x6547, 0x0232) },
	{ USB_DEVICE(0x18ec, 0x3118) },		/* USB to IrDA adapter */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct ark3116_private {
	spinlock_t lock;
	u8 termios_initialized;
};

static inline void ARK3116_SND(struct usb_serial *serial, int seq,
			       __u8 request, __u8 requesttype,
			       __u16 value, __u16 index)
{
	int result;
	result = usb_control_msg(serial->dev,
				 usb_sndctrlpipe(serial->dev, 0),
				 request, requesttype, value, index,
				 NULL, 0x00, 1000);
	dbg("%03d > ok", seq);
}

static inline void ARK3116_RCV(struct usb_serial *serial, int seq,
			       __u8 request, __u8 requesttype,
			       __u16 value, __u16 index, __u8 expected,
			       char *buf)
{
	int result;
	result = usb_control_msg(serial->dev,
				 usb_rcvctrlpipe(serial->dev, 0),
				 request, requesttype, value, index,
				 buf, 0x0000001, 1000);
	if (result)
		dbg("%03d < %d bytes [0x%02X]", seq, result,
		    ((unsigned char *)buf)[0]);
	else
		dbg("%03d < 0 bytes", seq);
}

static inline void ARK3116_RCV_QUIET(struct usb_serial *serial,
				     __u8 request, __u8 requesttype,
				     __u16 value, __u16 index, char *buf)
{
	usb_control_msg(serial->dev,
			usb_rcvctrlpipe(serial->dev, 0),
			request, requesttype, value, index,
			buf, 0x0000001, 1000);
static int is_irda(struct usb_serial *serial)
{
	struct usb_device *dev = serial->dev;
	if (le16_to_cpu(dev->descriptor.idVendor) == 0x18ec &&
			le16_to_cpu(dev->descriptor.idProduct) == 0x3118)
		return 1;
	return 0;
}

struct ark3116_private {
	int			irda;	/* 1 for irda device */

	/* protects hw register updates */
	struct mutex		hw_lock;

	int			quot;	/* baudrate divisor */
	__u32			lcr;	/* line control register value */
	__u32			hcr;	/* handshake control register (0x8)
					 * value */
	__u32			mcr;	/* modem control register value */

	/* protects the status values below */
	spinlock_t		status_lock;
	__u32			msr;	/* modem status register value */
	__u32			lsr;	/* line status register value */
};

static int ark3116_write_reg(struct usb_serial *serial,
			     unsigned reg, __u8 val)
{
	int result;
	 /* 0xfe 0x40 are magic values taken from original driver */
	result = usb_control_msg(serial->dev,
				 usb_sndctrlpipe(serial->dev, 0),
				 0xfe, 0x40, val, reg,
				 NULL, 0, ARK_TIMEOUT);
	return result;
}

static int ark3116_read_reg(struct usb_serial *serial,
			    unsigned reg, unsigned char *buf)
{
	int result;
	/* 0xfe 0xc0 are magic values taken from original driver */
	result = usb_control_msg(serial->dev,
				 usb_rcvctrlpipe(serial->dev, 0),
				 0xfe, 0xc0, 0, reg,
				 buf, 1, ARK_TIMEOUT);
	if (result < 1) {
		dev_err(&serial->interface->dev,
				"failed to read register %u: %d\n",
				reg, result);
		if (result >= 0)
			result = -EIO;

		return result;
	}

	return buf[0];
}

static inline int calc_divisor(int bps)
{
	/* Original ark3116 made some exceptions in rounding here
	 * because windows did the same. Assume that is not really
	 * necessary.
	 * Crystal is 12MHz, probably because of USB, but we divide by 4?
	 */
	return (12000000 + 2*bps) / (4*bps);
}

static int ark3116_attach(struct usb_serial *serial)
{
	char *buf;
	struct ark3116_private *priv;
	int i;

	for (i = 0; i < serial->num_ports; ++i) {
		priv = kzalloc(sizeof(struct ark3116_private), GFP_KERNEL);
		if (!priv)
			goto cleanup;
		spin_lock_init(&priv->lock);

		usb_set_serial_port_data(serial->port[i], priv);
	}

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc -> out of mem?");
		goto cleanup;
	}

	/* 3 */
	ARK3116_SND(serial, 3, 0xFE, 0x40, 0x0008, 0x0002);
	ARK3116_SND(serial, 4, 0xFE, 0x40, 0x0008, 0x0001);
	ARK3116_SND(serial, 5, 0xFE, 0x40, 0x0000, 0x0008);
	ARK3116_SND(serial, 6, 0xFE, 0x40, 0x0000, 0x000B);

	/* <-- seq7 */
	ARK3116_RCV(serial,  7, 0xFE, 0xC0, 0x0000, 0x0003, 0x00, buf);
	ARK3116_SND(serial,  8, 0xFE, 0x40, 0x0080, 0x0003);
	ARK3116_SND(serial,  9, 0xFE, 0x40, 0x001A, 0x0000);
	ARK3116_SND(serial, 10, 0xFE, 0x40, 0x0000, 0x0001);
	ARK3116_SND(serial, 11, 0xFE, 0x40, 0x0000, 0x0003);

	/* <-- seq12 */
	ARK3116_RCV(serial, 12, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);
	ARK3116_SND(serial, 13, 0xFE, 0x40, 0x0000, 0x0004);

	/* 14 */
	ARK3116_RCV(serial, 14, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);
	ARK3116_SND(serial, 15, 0xFE, 0x40, 0x0000, 0x0004);

	/* 16 */
	ARK3116_RCV(serial, 16, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);
	/* --> seq17 */
	ARK3116_SND(serial, 17, 0xFE, 0x40, 0x0001, 0x0004);

	/* <-- seq18 */
	ARK3116_RCV(serial, 18, 0xFE, 0xC0, 0x0000, 0x0004, 0x01, buf);

	/* --> seq19 */
	ARK3116_SND(serial, 19, 0xFE, 0x40, 0x0003, 0x0004);

	/* <-- seq20 */
	/* seems like serial port status info (RTS, CTS, ...) */
	/* returns modem control line status?! */
	ARK3116_RCV(serial, 20, 0xFE, 0xC0, 0x0000, 0x0006, 0xFF, buf);

	/* set 9600 baud & do some init?! */
	ARK3116_SND(serial, 147, 0xFE, 0x40, 0x0083, 0x0003);
	ARK3116_SND(serial, 148, 0xFE, 0x40, 0x0038, 0x0000);
	ARK3116_SND(serial, 149, 0xFE, 0x40, 0x0001, 0x0001);
	ARK3116_SND(serial, 150, 0xFE, 0x40, 0x0003, 0x0003);
	ARK3116_RCV(serial, 151, 0xFE, 0xC0, 0x0000, 0x0004, 0x03, buf);
	ARK3116_SND(serial, 152, 0xFE, 0x40, 0x0000, 0x0003);
	ARK3116_RCV(serial, 153, 0xFE, 0xC0, 0x0000, 0x0003, 0x00, buf);
	ARK3116_SND(serial, 154, 0xFE, 0x40, 0x0003, 0x0003);

	kfree(buf);
	return 0;

cleanup:
	for (--i; i >= 0; --i) {
		kfree(usb_get_serial_port_data(serial->port[i]));
		usb_set_serial_port_data(serial->port[i], NULL);
	}
	return -ENOMEM;
	/* make sure we have our end-points */
	if ((serial->num_bulk_in == 0) ||
	    (serial->num_bulk_out == 0) ||
	    (serial->num_interrupt_in == 0)) {
		dev_err(&serial->dev->dev,
			"%s - missing endpoint - "
			"bulk in: %d, bulk out: %d, int in %d\n",
			KBUILD_MODNAME,
			serial->num_bulk_in,
			serial->num_bulk_out,
			serial->num_interrupt_in);
		return -EINVAL;
	}

	return 0;
}

static int ark3116_port_probe(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct ark3116_private *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->hw_lock);
	spin_lock_init(&priv->status_lock);

	priv->irda = is_irda(serial);

	usb_set_serial_port_data(port, priv);

	/* setup the hardware */
	ark3116_write_reg(serial, UART_IER, 0);
	/* disable DMA */
	ark3116_write_reg(serial, UART_FCR, 0);
	/* handshake control */
	priv->hcr = 0;
	ark3116_write_reg(serial, 0x8     , 0);
	/* modem control */
	priv->mcr = 0;
	ark3116_write_reg(serial, UART_MCR, 0);

	if (!(priv->irda)) {
		ark3116_write_reg(serial, 0xb , 0);
	} else {
		ark3116_write_reg(serial, 0xb , 1);
		ark3116_write_reg(serial, 0xc , 0);
		ark3116_write_reg(serial, 0xd , 0x41);
		ark3116_write_reg(serial, 0xa , 1);
	}

	/* setup baudrate */
	ark3116_write_reg(serial, UART_LCR, UART_LCR_DLAB);

	/* setup for 9600 8N1 */
	priv->quot = calc_divisor(9600);
	ark3116_write_reg(serial, UART_DLL, priv->quot & 0xff);
	ark3116_write_reg(serial, UART_DLM, (priv->quot>>8) & 0xff);

	priv->lcr = UART_LCR_WLEN8;
	ark3116_write_reg(serial, UART_LCR, UART_LCR_WLEN8);

	ark3116_write_reg(serial, 0xe, 0);

	if (priv->irda)
		ark3116_write_reg(serial, 0x9, 0);

	dev_info(&serial->dev->dev,
		"%s using %s mode\n",
		KBUILD_MODNAME,
		priv->irda ? "IrDA" : "RS232");
	return 0;
}

static int ark3116_port_remove(struct usb_serial_port *port)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* device is closed, so URBs and DMA should be down */
	mutex_destroy(&priv->hw_lock);
	kfree(priv);

	return 0;
}

static void ark3116_init_termios(struct tty_struct *tty)
{
	struct ktermios *termios = &tty->termios;
	*termios = tty_std_termios;
	termios->c_cflag = B9600 | CS8
				      | CREAD | HUPCL | CLOCAL;
	termios->c_ispeed = 9600;
	termios->c_ospeed = 9600;
}

static void ark3116_set_termios(struct tty_struct *tty,
				struct usb_serial_port *port,
				struct ktermios *old_termios)
{
	struct usb_serial *serial = port->serial;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct ktermios *termios = tty->termios;
	unsigned int cflag = termios->c_cflag;
	unsigned long flags;
	int baud;
	int ark3116_baud;
	char *buf;
	char config;

	config = 0;

	dbg("%s - port %d", __func__, port->number);

	spin_lock_irqsave(&priv->lock, flags);
	if (!priv->termios_initialized) {
		*termios = tty_std_termios;
		termios->c_cflag = B9600 | CS8
					      | CREAD | HUPCL | CLOCAL;
		termios->c_ispeed = 9600;
		termios->c_ospeed = 9600;
		priv->termios_initialized = 1;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	cflag = termios->c_cflag;
	termios->c_cflag &= ~(CMSPAR|CRTSCTS);

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc");
		*termios = *old_termios;
		return;
	}

	/* set data bit count (8/7/6/5) */
	if (cflag & CSIZE) {
		switch (cflag & CSIZE) {
		case CS5:
			config |= 0x00;
			dbg("setting CS5");
			break;
		case CS6:
			config |= 0x01;
			dbg("setting CS6");
			break;
		case CS7:
			config |= 0x02;
			dbg("setting CS7");
			break;
		default:
			dbg("CSIZE was set but not CS5-CS8, using CS8!");
			/* fall through */
		case CS8:
			config |= 0x03;
			dbg("setting CS8");
			break;
		}
	}

	/* set parity (NONE/EVEN/ODD) */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			config |= 0x08;
			dbg("setting parity to ODD");
		} else {
			config |= 0x18;
			dbg("setting parity to EVEN");
		}
	} else {
		dbg("setting parity to NONE");
	}

	/* set stop bit (1/2) */
	if (cflag & CSTOPB) {
		config |= 0x04;
		dbg("setting 2 stop bits");
	} else {
		dbg("setting 1 stop bit");
	}

	/* set baudrate */
	baud = tty_get_baud_rate(tty);

	switch (baud) {
	case 75:
	case 150:
	case 300:
	case 600:
	case 1200:
	case 1800:
	case 2400:
	case 4800:
	case 9600:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
	case 230400:
	case 460800:
		/* Report the resulting rate back to the caller */
		tty_encode_baud_rate(tty, baud, baud);
		break;
	/* set 9600 as default (if given baudrate is invalid for example) */
	default:
		tty_encode_baud_rate(tty, 9600, 9600);
	case 0:
		baud = 9600;
	}

	/*
	 * found by try'n'error, be careful, maybe there are other options
	 * for multiplicator etc! (3.5 for example)
	 */
	if (baud == 460800)
		/* strange, for 460800 the formula is wrong
		 * if using round() then 9600baud is wrong) */
		ark3116_baud = 7;
	else
		ark3116_baud = 3000000 / baud;

	/* ? */
	ARK3116_RCV(serial, 0, 0xFE, 0xC0, 0x0000, 0x0003, 0x03, buf);

	/* offset = buf[0]; */
	/* offset = 0x03; */
	/* dbg("using 0x%04X as target for 0x0003:", 0x0080 + offset); */

	/* set baudrate */
	dbg("setting baudrate to %d (->reg=%d)", baud, ark3116_baud);
	ARK3116_SND(serial, 147, 0xFE, 0x40, 0x0083, 0x0003);
	ARK3116_SND(serial, 148, 0xFE, 0x40,
			    (ark3116_baud & 0x00FF), 0x0000);
	ARK3116_SND(serial, 149, 0xFE, 0x40,
			    (ark3116_baud & 0xFF00) >> 8, 0x0001);
	ARK3116_SND(serial, 150, 0xFE, 0x40, 0x0003, 0x0003);

	/* ? */
	ARK3116_RCV(serial, 151, 0xFE, 0xC0, 0x0000, 0x0004, 0x03, buf);
	ARK3116_SND(serial, 152, 0xFE, 0x40, 0x0000, 0x0003);

	/* set data bit count, stop bit count & parity: */
	dbg("updating bit count, stop bit or parity (cfg=0x%02X)", config);
	ARK3116_RCV(serial, 153, 0xFE, 0xC0, 0x0000, 0x0003, 0x00, buf);
	ARK3116_SND(serial, 154, 0xFE, 0x40, config, 0x0003);

	if (cflag & CRTSCTS)
		dbg("CRTSCTS not supported by chipset?!");

	/* TEST ARK3116_SND(154, 0xFE, 0x40, 0xFFFF, 0x0006); */

	kfree(buf);

	return;
}

static int ark3116_open(struct tty_struct *tty, struct usb_serial_port *port,
					struct file *filp)
{
	struct ktermios tmp_termios;
	struct usb_serial *serial = port->serial;
	char *buf;
	int result = 0;

	dbg("%s - port %d", __func__, port->number);

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc -> out of mem?");
		return -ENOMEM;
	}

	result = usb_serial_generic_open(tty, port, filp);
	if (result)
		goto err_out;

	/* open */
	ARK3116_RCV(serial, 111, 0xFE, 0xC0, 0x0000, 0x0003, 0x02, buf);

	ARK3116_SND(serial, 112, 0xFE, 0x40, 0x0082, 0x0003);
	ARK3116_SND(serial, 113, 0xFE, 0x40, 0x001A, 0x0000);
	ARK3116_SND(serial, 114, 0xFE, 0x40, 0x0000, 0x0001);
	ARK3116_SND(serial, 115, 0xFE, 0x40, 0x0002, 0x0003);

	ARK3116_RCV(serial, 116, 0xFE, 0xC0, 0x0000, 0x0004, 0x03, buf);
	ARK3116_SND(serial, 117, 0xFE, 0x40, 0x0002, 0x0004);

	ARK3116_RCV(serial, 118, 0xFE, 0xC0, 0x0000, 0x0004, 0x02, buf);
	ARK3116_SND(serial, 119, 0xFE, 0x40, 0x0000, 0x0004);

	ARK3116_RCV(serial, 120, 0xFE, 0xC0, 0x0000, 0x0004, 0x00, buf);

	ARK3116_SND(serial, 121, 0xFE, 0x40, 0x0001, 0x0004);

	ARK3116_RCV(serial, 122, 0xFE, 0xC0, 0x0000, 0x0004, 0x01, buf);

	ARK3116_SND(serial, 123, 0xFE, 0x40, 0x0003, 0x0004);

	/* returns different values (control lines?!) */
	ARK3116_RCV(serial, 124, 0xFE, 0xC0, 0x0000, 0x0006, 0xFF, buf);

	/* initialise termios */
	if (tty)
		ark3116_set_termios(tty, port, &tmp_termios);

err_out:
	kfree(buf);

	return result;
}

static int ark3116_ioctl(struct tty_struct *tty, struct file *file,
	struct ktermios *termios = &tty->termios;
	unsigned int cflag = termios->c_cflag;
	int bps = tty_get_baud_rate(tty);
	int quot;
	__u8 lcr, hcr, eval;

	/* set data bit count */
	switch (cflag & CSIZE) {
	case CS5:
		lcr = UART_LCR_WLEN5;
		break;
	case CS6:
		lcr = UART_LCR_WLEN6;
		break;
	case CS7:
		lcr = UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		lcr = UART_LCR_WLEN8;
		break;
	}
	if (cflag & CSTOPB)
		lcr |= UART_LCR_STOP;
	if (cflag & PARENB)
		lcr |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		lcr |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (cflag & CMSPAR)
		lcr |= UART_LCR_SPAR;
#endif
	/* handshake control */
	hcr = (cflag & CRTSCTS) ? 0x03 : 0x00;

	/* calc baudrate */
	dev_dbg(&port->dev, "%s - setting bps to %d\n", __func__, bps);
	eval = 0;
	switch (bps) {
	case 0:
		quot = calc_divisor(9600);
		break;
	default:
		if ((bps < 75) || (bps > 3000000))
			bps = 9600;
		quot = calc_divisor(bps);
		break;
	case 460800:
		eval = 1;
		quot = calc_divisor(bps);
		break;
	case 921600:
		eval = 2;
		quot = calc_divisor(bps);
		break;
	}

	/* Update state: synchronize */
	mutex_lock(&priv->hw_lock);

	/* keep old LCR_SBC bit */
	lcr |= (priv->lcr & UART_LCR_SBC);

	dev_dbg(&port->dev, "%s - setting hcr:0x%02x,lcr:0x%02x,quot:%d\n",
		__func__, hcr, lcr, quot);

	/* handshake control */
	if (priv->hcr != hcr) {
		priv->hcr = hcr;
		ark3116_write_reg(serial, 0x8, hcr);
	}

	/* baudrate */
	if (priv->quot != quot) {
		priv->quot = quot;
		priv->lcr = lcr; /* need to write lcr anyway */

		/* disable DMA since transmit/receive is
		 * shadowed by UART_DLL
		 */
		ark3116_write_reg(serial, UART_FCR, 0);

		ark3116_write_reg(serial, UART_LCR,
				  lcr|UART_LCR_DLAB);
		ark3116_write_reg(serial, UART_DLL, quot & 0xff);
		ark3116_write_reg(serial, UART_DLM, (quot>>8) & 0xff);

		/* restore lcr */
		ark3116_write_reg(serial, UART_LCR, lcr);
		/* magic baudrate thingy: not sure what it does,
		 * but windows does this as well.
		 */
		ark3116_write_reg(serial, 0xe, eval);

		/* enable DMA */
		ark3116_write_reg(serial, UART_FCR, UART_FCR_DMA_SELECT);
	} else if (priv->lcr != lcr) {
		priv->lcr = lcr;
		ark3116_write_reg(serial, UART_LCR, lcr);
	}

	mutex_unlock(&priv->hw_lock);

	/* check for software flow control */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		dev_warn(&serial->dev->dev,
			 "%s: don't know how to do software flow control\n",
			 KBUILD_MODNAME);
	}

	/* Don't rewrite B0 */
	if (tty_termios_baud_rate(termios))
		tty_termios_encode_baud_rate(termios, bps, bps);
}

static void ark3116_close(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	/* disable DMA */
	ark3116_write_reg(serial, UART_FCR, 0);

	/* deactivate interrupts */
	ark3116_write_reg(serial, UART_IER, 0);

	usb_serial_generic_close(port);
	if (serial->num_interrupt_in)
		usb_kill_urb(port->interrupt_in_urb);
}

static int ark3116_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	struct usb_serial *serial = port->serial;
	unsigned char *buf;
	int result;

	buf = kmalloc(1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	result = usb_serial_generic_open(tty, port);
	if (result) {
		dev_dbg(&port->dev,
			"%s - usb_serial_generic_open failed: %d\n",
			__func__, result);
		goto err_free;
	}

	/* remove any data still left: also clears error state */
	ark3116_read_reg(serial, UART_RX, buf);

	/* read modem status */
	result = ark3116_read_reg(serial, UART_MSR, buf);
	if (result < 0)
		goto err_close;
	priv->msr = *buf;

	/* read line status */
	result = ark3116_read_reg(serial, UART_LSR, buf);
	if (result < 0)
		goto err_close;
	priv->lsr = *buf;

	result = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev, "submit irq_in urb failed %d\n",
			result);
		goto err_close;
	}

	/* activate interrupts */
	ark3116_write_reg(port->serial, UART_IER, UART_IER_MSI|UART_IER_RLSI);

	/* enable DMA */
	ark3116_write_reg(port->serial, UART_FCR, UART_FCR_DMA_SELECT);

	/* setup termios */
	if (tty)
		ark3116_set_termios(tty, port, NULL);

	kfree(buf);

	return 0;

err_close:
	usb_serial_generic_close(port);
err_free:
	kfree(buf);

	return result;
}

static int ark3116_ioctl(struct tty_struct *tty,
			 unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct serial_struct serstruct;
	void __user *user_arg = (void __user *)arg;

	switch (cmd) {
	case TIOCGSERIAL:
		/* XXX: Some of these values are probably wrong. */
		memset(&serstruct, 0, sizeof(serstruct));
		serstruct.type = PORT_16654;
		serstruct.line = port->serial->minor;
		serstruct.port = port->number;
		serstruct.line = port->minor;
		serstruct.port = port->port_number;
		serstruct.custom_divisor = 0;
		serstruct.baud_base = 460800;

		if (copy_to_user(user_arg, &serstruct, sizeof(serstruct)))
			return -EFAULT;

		return 0;
	case TIOCSSERIAL:
		if (copy_from_user(&serstruct, user_arg, sizeof(serstruct)))
			return -EFAULT;
		return 0;
	default:
		dbg("%s cmd 0x%04x not supported", __func__, cmd);
		break;
	}

	return -ENOIOCTLCMD;
}

static int ark3116_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	char *buf;
	char temp;

	/* seems like serial port status info (RTS, CTS, ...) is stored
	 * in reg(?) 0x0006
	 * pcb connection point 11 = GND -> sets bit4 of response
	 * pcb connection point  7 = GND -> sets bit6 of response
	 */

	buf = kmalloc(1, GFP_KERNEL);
	if (!buf) {
		dbg("error kmalloc");
		return -ENOMEM;
	}

	/* read register */
	ARK3116_RCV_QUIET(serial, 0xFE, 0xC0, 0x0000, 0x0006, buf);
	temp = buf[0];
	kfree(buf);

	/* i do not really know if bit4=CTS and bit6=DSR... just a
	 * quick guess!
	 */
	return (temp & (1<<4) ? TIOCM_CTS : 0)
	       | (temp & (1<<6) ? TIOCM_DSR : 0);
}

static struct usb_driver ark3116_driver = {
	.name =		"ark3116",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table,
	.no_dynamic_id =	1,
};
static int ark3116_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	__u32 status;
	__u32 ctrl;
	unsigned long flags;

	mutex_lock(&priv->hw_lock);
	ctrl = priv->mcr;
	mutex_unlock(&priv->hw_lock);

	spin_lock_irqsave(&priv->status_lock, flags);
	status = priv->msr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	return  (status & UART_MSR_DSR  ? TIOCM_DSR  : 0) |
		(status & UART_MSR_CTS  ? TIOCM_CTS  : 0) |
		(status & UART_MSR_RI   ? TIOCM_RI   : 0) |
		(status & UART_MSR_DCD  ? TIOCM_CD   : 0) |
		(ctrl   & UART_MCR_DTR  ? TIOCM_DTR  : 0) |
		(ctrl   & UART_MCR_RTS  ? TIOCM_RTS  : 0) |
		(ctrl   & UART_MCR_OUT1 ? TIOCM_OUT1 : 0) |
		(ctrl   & UART_MCR_OUT2 ? TIOCM_OUT2 : 0);
}

static int ark3116_tiocmset(struct tty_struct *tty,
			unsigned set, unsigned clr)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* we need to take the mutex here, to make sure that the value
	 * in priv->mcr is actually the one that is in the hardware
	 */

	mutex_lock(&priv->hw_lock);

	if (set & TIOCM_RTS)
		priv->mcr |= UART_MCR_RTS;
	if (set & TIOCM_DTR)
		priv->mcr |= UART_MCR_DTR;
	if (set & TIOCM_OUT1)
		priv->mcr |= UART_MCR_OUT1;
	if (set & TIOCM_OUT2)
		priv->mcr |= UART_MCR_OUT2;
	if (clr & TIOCM_RTS)
		priv->mcr &= ~UART_MCR_RTS;
	if (clr & TIOCM_DTR)
		priv->mcr &= ~UART_MCR_DTR;
	if (clr & TIOCM_OUT1)
		priv->mcr &= ~UART_MCR_OUT1;
	if (clr & TIOCM_OUT2)
		priv->mcr &= ~UART_MCR_OUT2;

	ark3116_write_reg(port->serial, UART_MCR, priv->mcr);

	mutex_unlock(&priv->hw_lock);

	return 0;
}

static void ark3116_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct ark3116_private *priv = usb_get_serial_port_data(port);

	/* LCR is also used for other things: protect access */
	mutex_lock(&priv->hw_lock);

	if (break_state)
		priv->lcr |= UART_LCR_SBC;
	else
		priv->lcr &= ~UART_LCR_SBC;

	ark3116_write_reg(port->serial, UART_LCR, priv->lcr);

	mutex_unlock(&priv->hw_lock);
}

static void ark3116_update_msr(struct usb_serial_port *port, __u8 msr)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	priv->msr = msr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (msr & UART_MSR_ANY_DELTA) {
		/* update input line counters */
		if (msr & UART_MSR_DCTS)
			port->icount.cts++;
		if (msr & UART_MSR_DDSR)
			port->icount.dsr++;
		if (msr & UART_MSR_DDCD)
			port->icount.dcd++;
		if (msr & UART_MSR_TERI)
			port->icount.rng++;
		wake_up_interruptible(&port->port.delta_msr_wait);
	}
}

static void ark3116_update_lsr(struct usb_serial_port *port, __u8 lsr)
{
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	unsigned long flags;

	spin_lock_irqsave(&priv->status_lock, flags);
	/* combine bits */
	priv->lsr |= lsr;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (lsr&UART_LSR_BRK_ERROR_BITS) {
		if (lsr & UART_LSR_BI)
			port->icount.brk++;
		if (lsr & UART_LSR_FE)
			port->icount.frame++;
		if (lsr & UART_LSR_PE)
			port->icount.parity++;
		if (lsr & UART_LSR_OE)
			port->icount.overrun++;
	}
}

static void ark3116_read_int_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	int status = urb->status;
	const __u8 *data = urb->transfer_buffer;
	int result;

	switch (status) {
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&port->dev, "%s - urb shutting down with status: %d\n",
			__func__, status);
		return;
	default:
		dev_dbg(&port->dev, "%s - nonzero urb status received: %d\n",
			__func__, status);
		break;
	case 0: /* success */
		/* discovered this by trail and error... */
		if ((urb->actual_length == 4) && (data[0] == 0xe8)) {
			const __u8 id = data[1]&UART_IIR_ID;
			dev_dbg(&port->dev, "%s: iir=%02x\n", __func__, data[1]);
			if (id == UART_IIR_MSI) {
				dev_dbg(&port->dev, "%s: msr=%02x\n",
					__func__, data[3]);
				ark3116_update_msr(port, data[3]);
				break;
			} else if (id == UART_IIR_RLSI) {
				dev_dbg(&port->dev, "%s: lsr=%02x\n",
					__func__, data[2]);
				ark3116_update_lsr(port, data[2]);
				break;
			}
		}
		/*
		 * Not sure what this data meant...
		 */
		usb_serial_debug_data(&port->dev, __func__,
				      urb->actual_length,
				      urb->transfer_buffer);
		break;
	}

	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result)
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting interrupt urb\n",
			__func__, result);
}


/* Data comes in via the bulk (data) URB, errors/interrupts via the int URB.
 * This means that we cannot be sure which data byte has an associated error
 * condition, so we report an error for all data in the next bulk read.
 *
 * Actually, there might even be a window between the bulk data leaving the
 * ark and reading/resetting the lsr in the read_bulk_callback where an
 * interrupt for the next data block could come in.
 * Without somekind of ordering on the ark, we would have to report the
 * error for the next block of data as well...
 * For now, let's pretend this can't happen.
 */
static void ark3116_process_read_urb(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct ark3116_private *priv = usb_get_serial_port_data(port);
	unsigned char *data = urb->transfer_buffer;
	char tty_flag = TTY_NORMAL;
	unsigned long flags;
	__u32 lsr;

	/* update line status */
	spin_lock_irqsave(&priv->status_lock, flags);
	lsr = priv->lsr;
	priv->lsr &= ~UART_LSR_BRK_ERROR_BITS;
	spin_unlock_irqrestore(&priv->status_lock, flags);

	if (!urb->actual_length)
		return;

	if (lsr & UART_LSR_BRK_ERROR_BITS) {
		if (lsr & UART_LSR_BI)
			tty_flag = TTY_BREAK;
		else if (lsr & UART_LSR_PE)
			tty_flag = TTY_PARITY;
		else if (lsr & UART_LSR_FE)
			tty_flag = TTY_FRAME;

		/* overrun is special, not associated with a char */
		if (lsr & UART_LSR_OE)
			tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);
	}
	tty_insert_flip_string_fixed_flag(&port->port, data, tty_flag,
							urb->actual_length);
	tty_flip_buffer_push(&port->port);
}

static struct usb_serial_driver ark3116_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"ark3116",
	},
	.id_table =		id_table,
	.usb_driver =		&ark3116_driver,
	.num_ports =		1,
	.attach =		ark3116_attach,
	.set_termios =		ark3116_set_termios,
	.ioctl =		ark3116_ioctl,
	.tiocmget =		ark3116_tiocmget,
	.open =			ark3116_open,
};

static int __init ark3116_init(void)
{
	int retval;

	retval = usb_serial_register(&ark3116_device);
	if (retval)
		return retval;
	retval = usb_register(&ark3116_driver);
	if (retval)
		usb_serial_deregister(&ark3116_device);
	return retval;
}

static void __exit ark3116_exit(void)
{
	usb_deregister(&ark3116_driver);
	usb_serial_deregister(&ark3116_device);
}

module_init(ark3116_init);
module_exit(ark3116_exit);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");

	.num_ports =		1,
	.attach =		ark3116_attach,
	.port_probe =		ark3116_port_probe,
	.port_remove =		ark3116_port_remove,
	.set_termios =		ark3116_set_termios,
	.init_termios =		ark3116_init_termios,
	.ioctl =		ark3116_ioctl,
	.tiocmget =		ark3116_tiocmget,
	.tiocmset =		ark3116_tiocmset,
	.tiocmiwait =		usb_serial_generic_tiocmiwait,
	.get_icount =		usb_serial_generic_get_icount,
	.open =			ark3116_open,
	.close =		ark3116_close,
	.break_ctl = 		ark3116_break_ctl,
	.read_int_callback = 	ark3116_read_int_callback,
	.process_read_urb =	ark3116_process_read_urb,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&ark3116_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_LICENSE("GPL");

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

/*
 * The following describes what I learned from studying the old
 * ark3116.c driver, disassembling the windows driver, and some lucky
 * guesses. Since I do not have any datasheet or other
 * documentation, inaccuracies are almost guaranteed.
 *
 * Some specs for the ARK3116 can be found here:
 * http://web.archive.org/web/20060318000438/
 *   www.arkmicro.com/en/products/view.php?id=10
 * On that page, 2 GPIO pins are mentioned: I assume these are the
 * OUT1 and OUT2 pins of the UART, so I added support for those
 * through the MCR. Since the pins are not available on my hardware,
 * I could not verify this.
 * Also, it states there is "on-chip hardware flow control". I have
 * discovered how to enable that. Unfortunately, I do not know how to
 * enable XON/XOFF (software) flow control, which would need support
 * from the chip as well to work. Because of the wording on the web
 * page there is a real possibility the chip simply does not support
 * software flow control.
 *
 * I got my ark3116 as part of a mobile phone adapter cable. On the
 * PCB, the following numbered contacts are present:
 *
 *  1:- +5V
 *  2:o DTR
 *  3:i RX
 *  4:i DCD
 *  5:o RTS
 *  6:o TX
 *  7:i RI
 *  8:i DSR
 * 10:- 0V
 * 11:i CTS
 *
 * On my chip, all signals seem to be 3.3V, but 5V tolerant. But that
 * may be different for the one you have ;-).
 *
 * The windows driver limits the registers to 0-F, so I assume there
 * are actually 16 present on the device.
 *
 * On an UART interrupt, 4 bytes of data come in on the interrupt
 * endpoint. The bytes are 0xe8 IIR LSR MSR.
 *
 * The baudrate seems to be generated from the 12MHz crystal, using
 * 4-times subsampling. So quot=12e6/(4*baud). Also see description
 * of register E.
 *
 * Registers 0-7:
 * These seem to be the same as for a regular 16450. The FCR is set
 * to UART_FCR_DMA_SELECT (0x8), I guess to enable transfers between
 * the UART and the USB bridge/DMA engine.
 *
 * Register 8:
 * By trial and error, I found out that bit 0 enables hardware CTS,
 * stopping TX when CTS is +5V. Bit 1 does the same for RTS, making
 * RTS +5V when the 3116 cannot transfer the data to the USB bus
 * (verified by disabling the reading URB). Note that as far as I can
 * tell, the windows driver does NOT use this, so there might be some
 * hardware bug or something.
 *
 * According to a patch provided here
 * (http://lkml.org/lkml/2009/7/26/56), the ARK3116 can also be used
 * as an IrDA dongle. Since I do not have such a thing, I could not
 * investigate that aspect. However, I can speculate ;-).
 *
 * - IrDA encodes data differently than RS232. Most likely, one of
 *   the bits in registers 9..E enables the IR ENDEC (encoder/decoder).
 * - Depending on the IR transceiver, the input and output need to be
 *   inverted, so there are probably bits for that as well.
 * - IrDA is half-duplex, so there should be a bit for selecting that.
 *
 * This still leaves at least two registers unaccounted for. Perhaps
 * The chip can do XON/XOFF or CRC in HW?
 *
 * Register 9:
 * Set to 0x00 for IrDA, when the baudrate is initialised.
 *
 * Register A:
 * Set to 0x01 for IrDA, at init.
 *
 * Register B:
 * Set to 0x01 for IrDA, 0x00 for RS232, at init.
 *
 * Register C:
 * Set to 00 for IrDA, at init.
 *
 * Register D:
 * Set to 0x41 for IrDA, at init.
 *
 * Register E:
 * Somekind of baudrate override. The windows driver seems to set
 * this to 0x00 for normal baudrates, 0x01 for 460800, 0x02 for 921600.
 * Since 460800 and 921600 cannot be obtained by dividing 3MHz by an integer,
 * it could be somekind of subdivisor thingy.
 * However,it does not seem to do anything: selecting 921600 (divisor 3,
 * reg E=2), still gets 1 MHz. I also checked if registers 9, C or F would
 * work, but they don't.
 *
 * Register F: unknown
 */
