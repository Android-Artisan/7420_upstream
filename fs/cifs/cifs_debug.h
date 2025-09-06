/*
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2002
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
*/
#define CIFS_DEBUG		/* BB temporary */

#ifndef _H_CIFS_DEBUG
#define _H_CIFS_DEBUG

void cifs_dump_mem(char *label, void *data, int length);
#ifdef CONFIG_CIFS_DEBUG2
#define DBG2 2
void cifs_dump_detail(struct smb_hdr *);
void cifs_dump_mids(struct TCP_Server_Info *);
#else
#define DBG2 0
#endif
extern int traceSMB;		/* flag which enables the function below */
void dump_smb(struct smb_hdr *, int);
#define CIFS_INFO	0x01
#define CIFS_RC  	0x02
#define CIFS_TIMER	0x04

void cifs_dump_detail(void *);
void cifs_dump_mids(struct TCP_Server_Info *);
extern bool traceSMB;		/* flag which enables the function below */
void dump_smb(void *, int);
#define CIFS_INFO	0x01
#define CIFS_RC		0x02
#define CIFS_TIMER	0x04

#define VFS 1
#define FYI 2
extern int cifsFYI;
#ifdef CONFIG_CIFS_DEBUG2
#define NOISY 4
#else
#define NOISY 0
#endif

/*
 *	debug ON
 *	--------
 */
#ifdef CIFS_DEBUG


/* information message: e.g., configuration, major event */
extern int cifsFYI;
#define cifsfyi(format,arg...) if (cifsFYI & CIFS_INFO) printk(KERN_DEBUG " " __FILE__ ": " format "\n" "" , ## arg)

#define cFYI(button,prspec) if (button) cifsfyi prspec

#define cifswarn(format, arg...) printk(KERN_WARNING ": " format "\n" , ## arg)

/* debug event message: */
extern int cifsERROR;

#define cEVENT(format,arg...) if (cifsERROR) printk(KERN_EVENT __FILE__ ": " format "\n" , ## arg)

/* error event message: e.g., i/o error */
#define cifserror(format,arg...) if (cifsERROR) printk(KERN_ERR " CIFS VFS: " format "\n" "" , ## arg)

#define cERROR(button, prspec) if (button) cifserror prspec
#ifdef CONFIG_CIFS_DEBUG

__printf(1, 2) void cifs_vfs_err(const char *fmt, ...);

/* information message: e.g., configuration, major event */
#define cifs_dbg(type, fmt, ...)					\
do {									\
	if (type == FYI && cifsFYI & CIFS_INFO) {			\
		pr_debug_ratelimited("%s: "				\
			    fmt, __FILE__, ##__VA_ARGS__);		\
	} else if (type == VFS) {					\
		cifs_vfs_err(fmt, ##__VA_ARGS__);			\
	} else if (type == NOISY && type != 0) {			\
		pr_debug_ratelimited(fmt, ##__VA_ARGS__);		\
	}								\
} while (0)

/*
 *	debug OFF
 *	---------
 */
#else		/* _CIFS_DEBUG */
#define cERROR(button, prspec)
#define cEVENT(format, arg...)
#define cFYI(button, prspec)
#define cifserror(format, arg...)
#endif		/* _CIFS_DEBUG */
#define cifs_dbg(type, fmt, ...)					\
do {									\
	if (0)								\
		pr_debug(fmt, ##__VA_ARGS__);				\
} while (0)
#endif

#endif				/* _H_CIFS_DEBUG */
