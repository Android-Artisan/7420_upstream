#ifndef __LINUX_MAPLE_H
#define __LINUX_MAPLE_H

#include <linux/device.h>
#include <mach/maple.h>

#include <mach/maple.h>

struct device;
extern struct bus_type maple_bus_type;

/* Maple Bus command and response codes */
enum maple_code {
	MAPLE_RESPONSE_FILEERR = -5,
	MAPLE_RESPONSE_AGAIN = -4,	/* request should be retransmitted */
	MAPLE_RESPONSE_BADCMD = -3,
	MAPLE_RESPONSE_BADFUNC = -2,
	MAPLE_RESPONSE_NONE = -1,	/* unit didn't respond at all */
	MAPLE_COMMAND_DEVINFO = 1,
	MAPLE_COMMAND_ALLINFO = 2,
	MAPLE_COMMAND_RESET = 3,
	MAPLE_COMMAND_KILL = 4,
	MAPLE_RESPONSE_DEVINFO = 5,
	MAPLE_RESPONSE_ALLINFO = 6,
	MAPLE_RESPONSE_OK = 7,
	MAPLE_RESPONSE_DATATRF = 8,
	MAPLE_COMMAND_GETCOND = 9,
	MAPLE_COMMAND_GETMINFO = 10,
	MAPLE_COMMAND_BREAD = 11,
	MAPLE_COMMAND_BWRITE = 12,
	MAPLE_COMMAND_SETCOND = 14
	MAPLE_RESPONSE_FILEERR =	-5,
	MAPLE_RESPONSE_AGAIN,	/* retransmit */
	MAPLE_RESPONSE_BADCMD,
	MAPLE_RESPONSE_BADFUNC,
	MAPLE_RESPONSE_NONE,	/* unit didn't respond*/
	MAPLE_COMMAND_DEVINFO =		1,
	MAPLE_COMMAND_ALLINFO,
	MAPLE_COMMAND_RESET,
	MAPLE_COMMAND_KILL,
	MAPLE_RESPONSE_DEVINFO,
	MAPLE_RESPONSE_ALLINFO,
	MAPLE_RESPONSE_OK,
	MAPLE_RESPONSE_DATATRF,
	MAPLE_COMMAND_GETCOND,
	MAPLE_COMMAND_GETMINFO,
	MAPLE_COMMAND_BREAD,
	MAPLE_COMMAND_BWRITE,
	MAPLE_COMMAND_BSYNC,
	MAPLE_COMMAND_SETCOND,
	MAPLE_COMMAND_MICCONTROL
};

enum maple_file_errors {
	MAPLE_FILEERR_INVALID_PARTITION =	0x01000000,
	MAPLE_FILEERR_PHASE_ERROR =		0x02000000,
	MAPLE_FILEERR_INVALID_BLOCK =		0x04000000,
	MAPLE_FILEERR_WRITE_ERROR =		0x08000000,
	MAPLE_FILEERR_INVALID_WRITE_LENGTH =	0x10000000,
	MAPLE_FILEERR_BAD_CRC = 		0x20000000
};

struct maple_buffer {
	char bufx[0x400];
	void *buf;
};

struct mapleq {
	struct list_head list;
	struct maple_device *dev;
	void *sendbuf, *recvbuf, *recvbufdcsp;
	unsigned char length;
	enum maple_code command;
	struct mutex mutex;
	struct maple_buffer *recvbuf;
	void *sendbuf, *recvbuf_p2;
	unsigned char length;
	enum maple_code command;
};

struct maple_devinfo {
	unsigned long function;
	unsigned long function_data[3];
	unsigned char area_code;
	unsigned char connector_direction;
	char product_name[31];
	char product_licence[61];
	unsigned short standby_power;
	unsigned short max_power;
};

struct maple_device {
	struct maple_driver *driver;
	struct mapleq *mq;
	void (*callback) (struct mapleq * mq);
	void (*fileerr_handler)(struct maple_device *mdev, void *recvbuf);
	int (*can_unload)(struct maple_device *mdev);
	unsigned long when, interval, function;
	struct maple_devinfo devinfo;
	unsigned char port, unit;
	char product_name[32];
	char product_licence[64];
	atomic_t busy;
	wait_queue_head_t maple_wait;
	struct device dev;
};

struct maple_driver {
	unsigned long function;
	struct device_driver drv;
};

void maple_getcond_callback(struct maple_device *dev,
			    void (*callback) (struct mapleq * mq),
			    unsigned long interval,
			    unsigned long function);
int maple_driver_register(struct maple_driver *);
void maple_driver_unregister(struct maple_driver *);

int maple_add_packet_sleeps(struct maple_device *mdev, u32 function,
int maple_add_packet(struct maple_device *mdev, u32 function,
	u32 command, u32 length, void *data);
void maple_clear_dev(struct maple_device *mdev);

#define to_maple_dev(n) container_of(n, struct maple_device, dev)
#define to_maple_driver(n) container_of(n, struct maple_driver, drv)

#define maple_get_drvdata(d)		dev_get_drvdata(&(d)->dev)
#define maple_set_drvdata(d,p)		dev_set_drvdata(&(d)->dev, (p))

#endif				/* __LINUX_MAPLE_H */
