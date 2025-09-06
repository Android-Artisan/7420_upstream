/*
 * NVRAM definitions and access functions.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_NVRAM_H
#define _ASM_POWERPC_NVRAM_H

#include <linux/errno.h>

#define NVRW_CNT 0x20
#define NVRAM_HEADER_LEN 16 /* sizeof(struct nvram_header) */
#define NVRAM_BLOCK_LEN 16
#define NVRAM_MAX_REQ (2080/NVRAM_BLOCK_LEN)
#define NVRAM_MIN_REQ (1056/NVRAM_BLOCK_LEN)

#define NVRAM_AS0  0x74
#define NVRAM_AS1  0x75
#define NVRAM_DATA 0x77


/* RTC Offsets */

#define MOTO_RTC_SECONDS	0x1FF9
#define MOTO_RTC_MINUTES	0x1FFA
#define MOTO_RTC_HOURS		0x1FFB
#define MOTO_RTC_DAY_OF_WEEK	0x1FFC
#define MOTO_RTC_DAY_OF_MONTH	0x1FFD
#define MOTO_RTC_MONTH		0x1FFE
#define MOTO_RTC_YEAR		0x1FFF
#define MOTO_RTC_CONTROLA       0x1FF8
#define MOTO_RTC_CONTROLB       0x1FF9

#define NVRAM_SIG_SP	0x02	/* support processor */
#define NVRAM_SIG_OF	0x50	/* open firmware config */
#define NVRAM_SIG_FW	0x51	/* general firmware */
#define NVRAM_SIG_HW	0x52	/* hardware (VPD) */
#define NVRAM_SIG_FLIP	0x5a	/* Apple flip/flop header */
#define NVRAM_SIG_APPL	0x5f	/* Apple "system" (???) */
#define NVRAM_SIG_SYS	0x70	/* system env vars */
#define NVRAM_SIG_CFG	0x71	/* config data */
#define NVRAM_SIG_ELOG	0x72	/* error log */
#define NVRAM_SIG_VEND	0x7e	/* vendor defined */
#define NVRAM_SIG_FREE	0x7f	/* Free space */
#define NVRAM_SIG_OS	0xa0	/* OS defined */
#define NVRAM_SIG_PANIC	0xa1	/* Apple OSX "panic" */

/* If change this size, then change the size of NVNAME_LEN */
struct nvram_header {
	unsigned char signature;
	unsigned char checksum;
	unsigned short length;
	char name[12];
};

#ifdef __KERNEL__

#include <linux/list.h>

struct nvram_partition {
	struct list_head partition;
	struct nvram_header header;
	unsigned int index;
};

#ifndef _ASM_POWERPC_NVRAM_H
#define _ASM_POWERPC_NVRAM_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <uapi/asm/nvram.h>

/*
 * Set oops header version to distinguish between old and new format header.
 * lnx,oops-log partition max size is 4000, header version > 4000 will
 * help in identifying new header.
 */
#define OOPS_HDR_VERSION 5000

struct err_log_info {
	__be32 error_type;
	__be32 seq_num;
};

struct nvram_os_partition {
	const char *name;
	int req_size;	/* desired size, in bytes */
	int min_size;	/* minimum acceptable size (0 means req_size) */
	long size;	/* size of data portion (excluding err_log_info) */
	long index;	/* offset of data portion of partition */
	bool os_partition; /* partition initialized by OS, not FW */
};

struct oops_log_info {
	__be16 version;
	__be16 report_length;
	__be64 timestamp;
} __attribute__((packed));

extern struct nvram_os_partition oops_log_partition;

#ifdef CONFIG_PPC_PSERIES
extern struct nvram_os_partition rtas_log_partition;

extern int nvram_write_error_log(char * buff, int length,
					 unsigned int err_type, unsigned int err_seq);
extern int nvram_read_error_log(char * buff, int length,
					 unsigned int * err_type, unsigned int *err_seq);
extern int nvram_clear_error_log(void);
extern struct nvram_partition *nvram_find_partition(int sig, const char *name);

extern int pSeries_nvram_init(void);
extern int pSeries_nvram_init(void);
#endif /* CONFIG_PPC_PSERIES */

#ifdef CONFIG_MMIO_NVRAM
extern int mmio_nvram_init(void);
#else
static inline int mmio_nvram_init(void)
{
	return -ENODEV;
}
#endif

#endif /* __KERNEL__ */

/* PowerMac specific nvram stuffs */

enum {
	pmac_nvram_OF,		/* Open Firmware partition */
	pmac_nvram_XPRAM,	/* MacOS XPRAM partition */
	pmac_nvram_NR		/* MacOS Name Registry partition */
};

#ifdef __KERNEL__
extern int __init nvram_scan_partitions(void);
extern loff_t nvram_create_partition(const char *name, int sig,
				     int req_size, int min_size);
extern int nvram_remove_partition(const char *name, int sig,
					const char *exceptions[]);
extern int nvram_get_partition_size(loff_t data_index);
extern loff_t nvram_find_partition(const char *name, int sig, int *out_size);

/* Return partition offset in nvram */
extern int	pmac_get_partition(int partition);

/* Direct access to XPRAM on PowerMacs */
extern u8	pmac_xpram_read(int xpaddr);
extern void	pmac_xpram_write(int xpaddr, u8 data);

/* Synchronize NVRAM */
extern void	nvram_sync(void);

/* Normal access to NVRAM */
extern unsigned char nvram_read_byte(int i);
extern void nvram_write_byte(unsigned char c, int i);
#endif

/* Some offsets in XPRAM */
#define PMAC_XPRAM_MACHINE_LOC	0xe4
#define PMAC_XPRAM_SOUND_VOLUME	0x08

/* Machine location structure in PowerMac XPRAM */
struct pmac_machine_location {
	unsigned int	latitude;	/* 2+30 bit Fractional number */
	unsigned int	longitude;	/* 2+30 bit Fractional number */
	unsigned int	delta;		/* mix of GMT delta and DLS */
};

/*
 * /dev/nvram ioctls
 *
 * Note that PMAC_NVRAM_GET_OFFSET is still supported, but is
 * definitely obsolete. Do not use it if you can avoid it
 */

#define OBSOLETE_PMAC_NVRAM_GET_OFFSET \
				_IOWR('p', 0x40, int)

#define IOC_NVRAM_GET_OFFSET	_IOWR('p', 0x42, int)	/* Get NVRAM partition offset */
#define IOC_NVRAM_SYNC		_IO('p', 0x43)		/* Sync NVRAM image */

/* Initialize NVRAM OS partition */
extern int __init nvram_init_os_partition(struct nvram_os_partition *part);

/* Initialize NVRAM oops partition */
extern void __init nvram_init_oops_partition(int rtas_partition_exists);

/* Read a NVRAM partition */
extern int nvram_read_partition(struct nvram_os_partition *part, char *buff,
				int length, unsigned int *err_type,
				unsigned int *error_log_cnt);

/* Write to NVRAM OS partition */
extern int nvram_write_os_partition(struct nvram_os_partition *part,
				    char *buff, int length,
				    unsigned int err_type,
				    unsigned int error_log_cnt);

/* Determine NVRAM size */
extern ssize_t nvram_get_size(void);

/* Normal access to NVRAM */
extern unsigned char nvram_read_byte(int i);
extern void nvram_write_byte(unsigned char c, int i);
#endif /* _ASM_POWERPC_NVRAM_H */
