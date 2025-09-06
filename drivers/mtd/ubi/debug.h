/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __UBI_DEBUG_H__
#define __UBI_DEBUG_H__

#ifdef CONFIG_MTD_UBI_DEBUG
#include <linux/random.h>

#define dbg_err(fmt, ...) ubi_err(fmt, ##__VA_ARGS__)

#define ubi_assert(expr)  do {                                               \
        if (unlikely(!(expr))) {                                             \
                printk(KERN_CRIT "UBI assert failed in %s at %u (pid %d)\n", \
                       __func__, __LINE__, current->pid);                    \
                ubi_dbg_dump_stack();                                        \
        }                                                                    \
} while (0)

#define dbg_msg(fmt, ...)                                    \
	printk(KERN_DEBUG "UBI DBG (pid %d): %s: " fmt "\n", \
	       current->pid, __func__, ##__VA_ARGS__)

#define ubi_dbg_dump_stack() dump_stack()

struct ubi_ec_hdr;
struct ubi_vid_hdr;
struct ubi_volume;
struct ubi_vtbl_record;
struct ubi_scan_volume;
struct ubi_scan_leb;
struct ubi_mkvol_req;

void ubi_dbg_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr);
void ubi_dbg_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr);
void ubi_dbg_dump_vol_info(const struct ubi_volume *vol);
void ubi_dbg_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx);
void ubi_dbg_dump_sv(const struct ubi_scan_volume *sv);
void ubi_dbg_dump_seb(const struct ubi_scan_leb *seb, int type);
void ubi_dbg_dump_mkvol_req(const struct ubi_mkvol_req *req);

#ifdef CONFIG_MTD_UBI_DEBUG_MSG
/* General debugging messages */
#define dbg_gen(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_gen(fmt, ...) ({})
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_MSG_EBA
/* Messages from the eraseblock association sub-system */
#define dbg_eba(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_eba(fmt, ...) ({})
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_MSG_WL
/* Messages from the wear-leveling sub-system */
#define dbg_wl(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_wl(fmt, ...) ({})
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_MSG_IO
/* Messages from the input/output sub-system */
#define dbg_io(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_io(fmt, ...) ({})
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_MSG_BLD
/* Initialization and build messages */
#define dbg_bld(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#define UBI_IO_DEBUG 1
#else
#define dbg_bld(fmt, ...) ({})
#define UBI_IO_DEBUG 0
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_DISABLE_BGT
#define DBG_DISABLE_BGT 1
#else
#define DBG_DISABLE_BGT 0
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_EMULATE_BITFLIPS
/**
 * ubi_dbg_is_bitflip - if it is time to emulate a bit-flip.
 *
 * Returns non-zero if a bit-flip should be emulated, otherwise returns zero.
 */
static inline int ubi_dbg_is_bitflip(void)
{
	return !(random32() % 200);
}
#else
#define ubi_dbg_is_bitflip() 0
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_EMULATE_WRITE_FAILURES
/**
 * ubi_dbg_is_write_failure - if it is time to emulate a write failure.
void ubi_dump_flash(struct ubi_device *ubi, int pnum, int offset, int len);
void ubi_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr);
void ubi_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr);

#include <linux/random.h>

#define ubi_assert(expr)  do {                                               \
	if (unlikely(!(expr))) {                                             \
		pr_crit("UBI assert failed in %s at %u (pid %d)\n",          \
		       __func__, __LINE__, current->pid);                    \
		dump_stack();                                                \
	}                                                                    \
} while (0)

#define ubi_dbg_print_hex_dump(l, ps, pt, r, g, b, len, a)                   \
		print_hex_dump(l, ps, pt, r, g, b, len, a)

#define ubi_dbg_msg(type, fmt, ...) \
	pr_debug("UBI DBG " type " (pid %d): " fmt "\n", current->pid,       \
		 ##__VA_ARGS__)

/* General debugging messages */
#define dbg_gen(fmt, ...) ubi_dbg_msg("gen", fmt, ##__VA_ARGS__)
/* Messages from the eraseblock association sub-system */
#define dbg_eba(fmt, ...) ubi_dbg_msg("eba", fmt, ##__VA_ARGS__)
/* Messages from the wear-leveling sub-system */
#define dbg_wl(fmt, ...)  ubi_dbg_msg("wl", fmt, ##__VA_ARGS__)
/* Messages from the input/output sub-system */
#define dbg_io(fmt, ...)  ubi_dbg_msg("io", fmt, ##__VA_ARGS__)
/* Initialization and build messages */
#define dbg_bld(fmt, ...) ubi_dbg_msg("bld", fmt, ##__VA_ARGS__)

void ubi_dump_vol_info(const struct ubi_volume *vol);
void ubi_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx);
void ubi_dump_av(const struct ubi_ainf_volume *av);
void ubi_dump_aeb(const struct ubi_ainf_peb *aeb, int type);
void ubi_dump_mkvol_req(const struct ubi_mkvol_req *req);
int ubi_self_check_all_ff(struct ubi_device *ubi, int pnum, int offset,
			  int len);
int ubi_debugfs_init(void);
void ubi_debugfs_exit(void);
int ubi_debugfs_init_dev(struct ubi_device *ubi);
void ubi_debugfs_exit_dev(struct ubi_device *ubi);

/**
 * ubi_dbg_is_bgt_disabled - if the background thread is disabled.
 * @ubi: UBI device description object
 *
 * Returns non-zero if the UBI background thread is disabled for testing
 * purposes.
 */
static inline int ubi_dbg_is_bgt_disabled(const struct ubi_device *ubi)
{
	return ubi->dbg.disable_bgt;
}

/**
 * ubi_dbg_is_bitflip - if it is time to emulate a bit-flip.
 * @ubi: UBI device description object
 *
 * Returns non-zero if a bit-flip should be emulated, otherwise returns zero.
 */
static inline int ubi_dbg_is_bitflip(const struct ubi_device *ubi)
{
	if (ubi->dbg.emulate_bitflips)
		return !(prandom_u32() % 200);
	return 0;
}

/**
 * ubi_dbg_is_write_failure - if it is time to emulate a write failure.
 * @ubi: UBI device description object
 *
 * Returns non-zero if a write failure should be emulated, otherwise returns
 * zero.
 */
static inline int ubi_dbg_is_write_failure(void)
{
	return !(random32() % 500);
}
#else
#define ubi_dbg_is_write_failure() 0
#endif

#ifdef CONFIG_MTD_UBI_DEBUG_EMULATE_ERASE_FAILURES
/**
 * ubi_dbg_is_erase_failure - if its time to emulate an erase failure.
static inline int ubi_dbg_is_write_failure(const struct ubi_device *ubi)
{
	if (ubi->dbg.emulate_io_failures)
		return !(prandom_u32() % 500);
	return 0;
}

/**
 * ubi_dbg_is_erase_failure - if its time to emulate an erase failure.
 * @ubi: UBI device description object
 *
 * Returns non-zero if an erase failure should be emulated, otherwise returns
 * zero.
 */
static inline int ubi_dbg_is_erase_failure(void)
{
		return !(random32() % 400);
}
#else
#define ubi_dbg_is_erase_failure() 0
#endif

#else

#define ubi_assert(expr)                 ({})
#define dbg_err(fmt, ...)                ({})
#define dbg_msg(fmt, ...)                ({})
#define dbg_gen(fmt, ...)                ({})
#define dbg_eba(fmt, ...)                ({})
#define dbg_wl(fmt, ...)                 ({})
#define dbg_io(fmt, ...)                 ({})
#define dbg_bld(fmt, ...)                ({})
#define ubi_dbg_dump_stack()             ({})
#define ubi_dbg_dump_ec_hdr(ec_hdr)      ({})
#define ubi_dbg_dump_vid_hdr(vid_hdr)    ({})
#define ubi_dbg_dump_vol_info(vol)       ({})
#define ubi_dbg_dump_vtbl_record(r, idx) ({})
#define ubi_dbg_dump_sv(sv)              ({})
#define ubi_dbg_dump_seb(seb, type)      ({})
#define ubi_dbg_dump_mkvol_req(req)      ({})

#define UBI_IO_DEBUG               0
#define DBG_DISABLE_BGT            0
#define ubi_dbg_is_bitflip()       0
#define ubi_dbg_is_write_failure() 0
#define ubi_dbg_is_erase_failure() 0

#endif /* !CONFIG_MTD_UBI_DEBUG */
static inline int ubi_dbg_is_erase_failure(const struct ubi_device *ubi)
{
	if (ubi->dbg.emulate_io_failures)
		return !(prandom_u32() % 400);
	return 0;
}

static inline int ubi_dbg_chk_io(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_io;
}

static inline int ubi_dbg_chk_gen(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_gen;
}

static inline int ubi_dbg_chk_fastmap(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_fastmap;
}

static inline void ubi_enable_dbg_chk_fastmap(struct ubi_device *ubi)
{
	ubi->dbg.chk_fastmap = 1;
}

int ubi_dbg_power_cut(struct ubi_device *ubi, int caller);
#endif /* !__UBI_DEBUG_H__ */
