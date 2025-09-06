/*
 *   fs/cifs/connect.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Copyright (C) International Business Machines  Corp., 2002,2011
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/net.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/ipv6.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/ctype.h>
#include <linux/utsname.h>
#include <linux/mempool.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pagevec.h>
#include <linux/freezer.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <linux/inet.h>
#include <linux/module.h>
#include <keys/user-type.h>
#include <net/ipv6.h>
#include <linux/parser.h>

#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "dns_resolve.h"
#include "ntlmssp.h"
#include "nterr.h"
#include "rfc1002pdu.h"
#include "cn_cifs.h"
#include "fscache.h"
#ifdef CONFIG_CIFS_SMB2
#include "smb2proto.h"
#endif

#define CIFS_PORT 445
#define RFC1001_PORT 139

extern void SMBNTencrypt(unsigned char *passwd, unsigned char *c8,
			 unsigned char *p24);

extern mempool_t *cifs_req_poolp;

struct smb_vol {
	char *username;
	char *password;
	char *domainname;
	char *UNC;
	char *UNCip;
	char *in6_addr;   /* ipv6 address as human readable form of in6_addr */
	char *iocharset;  /* local code page for mapping to and from Unicode */
	char source_rfc1001_name[16]; /* netbios name of client */
	char target_rfc1001_name[16]; /* netbios name of server for Win9x/ME */
	uid_t linux_uid;
	gid_t linux_gid;
	mode_t file_mode;
	mode_t dir_mode;
	unsigned secFlg;
	bool rw:1;
	bool retry:1;
	bool intr:1;
	bool setuids:1;
	bool override_uid:1;
	bool override_gid:1;
	bool dynperm:1;
	bool noperm:1;
	bool no_psx_acl:1; /* set if posix acl support should be disabled */
	bool cifs_acl:1;
	bool no_xattr:1;   /* set if xattr (EA) support should be disabled*/
	bool server_ino:1; /* use inode numbers from server ie UniqueId */
	bool direct_io:1;
	bool remap:1;      /* set to remap seven reserved chars in filenames */
	bool posix_paths:1; /* unset to not ask for posix pathnames. */
	bool no_linux_ext:1;
	bool sfu_emul:1;
	bool nullauth:1;   /* attempt to authenticate with null user */
	bool nocase:1;     /* request case insensitive filenames */
	bool nobrl:1;      /* disable sending byte range locks to srv */
	bool seal:1;       /* request transport encryption on share */
	unsigned int rsize;
	unsigned int wsize;
	unsigned int sockopt;
	unsigned short int port;
	char *prepath;
};

static int ipv4_connect(struct sockaddr_in *psin_server,
			struct socket **csocket,
			char *netb_name,
			char *server_netb_name);
static int ipv6_connect(struct sockaddr_in6 *psin_server,
			struct socket **csocket);


	/*
	 * cifs tcp session reconnection
	 *
	 * mark tcp session as reconnecting so temporarily locked
	 * mark all smb sessions as reconnecting for tcp session
	 * reconnect tcp session
	 * wake up waiters on reconnection? - (not needed currently)
	 */

static int
cifs_reconnect(struct TCP_Server_Info *server)
{
	int rc = 0;
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct cifsTconInfo *tcon;
	struct mid_q_entry *mid_entry;

	spin_lock(&GlobalMid_Lock);
	if (kthread_should_stop()) {
extern mempool_t *cifs_req_poolp;

/* FIXME: should these be tunable? */
#define TLINK_ERROR_EXPIRE	(1 * HZ)
#define TLINK_IDLE_EXPIRE	(600 * HZ)

enum {

	/* Mount options that take no arguments */
	Opt_user_xattr, Opt_nouser_xattr,
	Opt_forceuid, Opt_noforceuid,
	Opt_forcegid, Opt_noforcegid,
	Opt_noblocksend, Opt_noautotune,
	Opt_hard, Opt_soft, Opt_perm, Opt_noperm,
	Opt_mapposix, Opt_nomapposix,
	Opt_mapchars, Opt_nomapchars, Opt_sfu,
	Opt_nosfu, Opt_nodfs, Opt_posixpaths,
	Opt_noposixpaths, Opt_nounix,
	Opt_nocase,
	Opt_brl, Opt_nobrl,
	Opt_forcemandatorylock, Opt_setuids,
	Opt_nosetuids, Opt_dynperm, Opt_nodynperm,
	Opt_nohard, Opt_nosoft,
	Opt_nointr, Opt_intr,
	Opt_nostrictsync, Opt_strictsync,
	Opt_serverino, Opt_noserverino,
	Opt_rwpidforward, Opt_cifsacl, Opt_nocifsacl,
	Opt_acl, Opt_noacl, Opt_locallease,
	Opt_sign, Opt_seal, Opt_noac,
	Opt_fsc, Opt_mfsymlinks,
	Opt_multiuser, Opt_sloppy, Opt_nosharesock,
	Opt_persistent, Opt_nopersistent,
	Opt_resilient, Opt_noresilient,

	/* Mount options which take numeric value */
	Opt_backupuid, Opt_backupgid, Opt_uid,
	Opt_cruid, Opt_gid, Opt_file_mode,
	Opt_dirmode, Opt_port,
	Opt_rsize, Opt_wsize, Opt_actimeo,

	/* Mount options which take string value */
	Opt_user, Opt_pass, Opt_ip,
	Opt_domain, Opt_srcaddr, Opt_iocharset,
	Opt_netbiosname, Opt_servern,
	Opt_ver, Opt_vers, Opt_sec, Opt_cache,

	/* Mount options to be ignored */
	Opt_ignore,

	/* Options which could be blank */
	Opt_blank_pass,
	Opt_blank_user,
	Opt_blank_ip,

	Opt_err
};

static const match_table_t cifs_mount_option_tokens = {

	{ Opt_user_xattr, "user_xattr" },
	{ Opt_nouser_xattr, "nouser_xattr" },
	{ Opt_forceuid, "forceuid" },
	{ Opt_noforceuid, "noforceuid" },
	{ Opt_forcegid, "forcegid" },
	{ Opt_noforcegid, "noforcegid" },
	{ Opt_noblocksend, "noblocksend" },
	{ Opt_noautotune, "noautotune" },
	{ Opt_hard, "hard" },
	{ Opt_soft, "soft" },
	{ Opt_perm, "perm" },
	{ Opt_noperm, "noperm" },
	{ Opt_mapchars, "mapchars" }, /* SFU style */
	{ Opt_nomapchars, "nomapchars" },
	{ Opt_mapposix, "mapposix" }, /* SFM style */
	{ Opt_nomapposix, "nomapposix" },
	{ Opt_sfu, "sfu" },
	{ Opt_nosfu, "nosfu" },
	{ Opt_nodfs, "nodfs" },
	{ Opt_posixpaths, "posixpaths" },
	{ Opt_noposixpaths, "noposixpaths" },
	{ Opt_nounix, "nounix" },
	{ Opt_nounix, "nolinux" },
	{ Opt_nocase, "nocase" },
	{ Opt_nocase, "ignorecase" },
	{ Opt_brl, "brl" },
	{ Opt_nobrl, "nobrl" },
	{ Opt_nobrl, "nolock" },
	{ Opt_forcemandatorylock, "forcemandatorylock" },
	{ Opt_forcemandatorylock, "forcemand" },
	{ Opt_setuids, "setuids" },
	{ Opt_nosetuids, "nosetuids" },
	{ Opt_dynperm, "dynperm" },
	{ Opt_nodynperm, "nodynperm" },
	{ Opt_nohard, "nohard" },
	{ Opt_nosoft, "nosoft" },
	{ Opt_nointr, "nointr" },
	{ Opt_intr, "intr" },
	{ Opt_nostrictsync, "nostrictsync" },
	{ Opt_strictsync, "strictsync" },
	{ Opt_serverino, "serverino" },
	{ Opt_noserverino, "noserverino" },
	{ Opt_rwpidforward, "rwpidforward" },
	{ Opt_cifsacl, "cifsacl" },
	{ Opt_nocifsacl, "nocifsacl" },
	{ Opt_acl, "acl" },
	{ Opt_noacl, "noacl" },
	{ Opt_locallease, "locallease" },
	{ Opt_sign, "sign" },
	{ Opt_seal, "seal" },
	{ Opt_noac, "noac" },
	{ Opt_fsc, "fsc" },
	{ Opt_mfsymlinks, "mfsymlinks" },
	{ Opt_multiuser, "multiuser" },
	{ Opt_sloppy, "sloppy" },
	{ Opt_nosharesock, "nosharesock" },
	{ Opt_persistent, "persistenthandles"},
	{ Opt_nopersistent, "nopersistenthandles"},
	{ Opt_resilient, "resilienthandles"},
	{ Opt_noresilient, "noresilienthandles"},

	{ Opt_backupuid, "backupuid=%s" },
	{ Opt_backupgid, "backupgid=%s" },
	{ Opt_uid, "uid=%s" },
	{ Opt_cruid, "cruid=%s" },
	{ Opt_gid, "gid=%s" },
	{ Opt_file_mode, "file_mode=%s" },
	{ Opt_dirmode, "dirmode=%s" },
	{ Opt_dirmode, "dir_mode=%s" },
	{ Opt_port, "port=%s" },
	{ Opt_rsize, "rsize=%s" },
	{ Opt_wsize, "wsize=%s" },
	{ Opt_actimeo, "actimeo=%s" },

	{ Opt_blank_user, "user=" },
	{ Opt_blank_user, "username=" },
	{ Opt_user, "user=%s" },
	{ Opt_user, "username=%s" },
	{ Opt_blank_pass, "pass=" },
	{ Opt_blank_pass, "password=" },
	{ Opt_pass, "pass=%s" },
	{ Opt_pass, "password=%s" },
	{ Opt_blank_ip, "ip=" },
	{ Opt_blank_ip, "addr=" },
	{ Opt_ip, "ip=%s" },
	{ Opt_ip, "addr=%s" },
	{ Opt_ignore, "unc=%s" },
	{ Opt_ignore, "target=%s" },
	{ Opt_ignore, "path=%s" },
	{ Opt_domain, "dom=%s" },
	{ Opt_domain, "domain=%s" },
	{ Opt_domain, "workgroup=%s" },
	{ Opt_srcaddr, "srcaddr=%s" },
	{ Opt_ignore, "prefixpath=%s" },
	{ Opt_iocharset, "iocharset=%s" },
	{ Opt_netbiosname, "netbiosname=%s" },
	{ Opt_servern, "servern=%s" },
	{ Opt_ver, "ver=%s" },
	{ Opt_vers, "vers=%s" },
	{ Opt_sec, "sec=%s" },
	{ Opt_cache, "cache=%s" },

	{ Opt_ignore, "cred" },
	{ Opt_ignore, "credentials" },
	{ Opt_ignore, "cred=%s" },
	{ Opt_ignore, "credentials=%s" },
	{ Opt_ignore, "guest" },
	{ Opt_ignore, "rw" },
	{ Opt_ignore, "ro" },
	{ Opt_ignore, "suid" },
	{ Opt_ignore, "nosuid" },
	{ Opt_ignore, "exec" },
	{ Opt_ignore, "noexec" },
	{ Opt_ignore, "nodev" },
	{ Opt_ignore, "noauto" },
	{ Opt_ignore, "dev" },
	{ Opt_ignore, "mand" },
	{ Opt_ignore, "nomand" },
	{ Opt_ignore, "_netdev" },

	{ Opt_err, NULL }
};

enum {
	Opt_sec_krb5, Opt_sec_krb5i, Opt_sec_krb5p,
	Opt_sec_ntlmsspi, Opt_sec_ntlmssp,
	Opt_ntlm, Opt_sec_ntlmi, Opt_sec_ntlmv2,
	Opt_sec_ntlmv2i, Opt_sec_lanman,
	Opt_sec_none,

	Opt_sec_err
};

static const match_table_t cifs_secflavor_tokens = {
	{ Opt_sec_krb5, "krb5" },
	{ Opt_sec_krb5i, "krb5i" },
	{ Opt_sec_krb5p, "krb5p" },
	{ Opt_sec_ntlmsspi, "ntlmsspi" },
	{ Opt_sec_ntlmssp, "ntlmssp" },
	{ Opt_ntlm, "ntlm" },
	{ Opt_sec_ntlmi, "ntlmi" },
	{ Opt_sec_ntlmv2, "nontlm" },
	{ Opt_sec_ntlmv2, "ntlmv2" },
	{ Opt_sec_ntlmv2i, "ntlmv2i" },
	{ Opt_sec_lanman, "lanman" },
	{ Opt_sec_none, "none" },

	{ Opt_sec_err, NULL }
};

/* cache flavors */
enum {
	Opt_cache_loose,
	Opt_cache_strict,
	Opt_cache_none,
	Opt_cache_err
};

static const match_table_t cifs_cacheflavor_tokens = {
	{ Opt_cache_loose, "loose" },
	{ Opt_cache_strict, "strict" },
	{ Opt_cache_none, "none" },
	{ Opt_cache_err, NULL }
};

static const match_table_t cifs_smb_version_tokens = {
	{ Smb_1, SMB1_VERSION_STRING },
	{ Smb_20, SMB20_VERSION_STRING},
	{ Smb_21, SMB21_VERSION_STRING },
	{ Smb_30, SMB30_VERSION_STRING },
	{ Smb_302, SMB302_VERSION_STRING },
#ifdef CONFIG_CIFS_SMB311
	{ Smb_311, SMB311_VERSION_STRING },
	{ Smb_311, ALT_SMB311_VERSION_STRING },
#endif /* SMB311 */
	{ Smb_version_err, NULL }
};

static int ip_connect(struct TCP_Server_Info *server);
static int generic_ip_connect(struct TCP_Server_Info *server);
static void tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink);
static void cifs_prune_tlinks(struct work_struct *work);
static int cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
					const char *devname);

/*
 * Resolve hostname and set ip addr in tcp ses. Useful for hostnames that may
 * get their ip addresses changed at some point.
 *
 * This should be called with server->srv_mutex held.
 */
#ifdef CONFIG_CIFS_DFS_UPCALL
static int reconn_set_ipaddr(struct TCP_Server_Info *server)
{
	int rc;
	int len;
	char *unc, *ipaddr = NULL;

	if (!server->hostname)
		return -EINVAL;

	len = strlen(server->hostname) + 3;

	unc = kmalloc(len, GFP_KERNEL);
	if (!unc) {
		cifs_dbg(FYI, "%s: failed to create UNC path\n", __func__);
		return -ENOMEM;
	}
	snprintf(unc, len, "\\\\%s", server->hostname);

	rc = dns_resolve_server_name_to_ip(unc, &ipaddr);
	kfree(unc);

	if (rc < 0) {
		cifs_dbg(FYI, "%s: failed to resolve server part of %s to IP: %d\n",
			 __func__, server->hostname, rc);
		return rc;
	}

	spin_lock(&cifs_tcp_ses_lock);
	rc = cifs_convert_address((struct sockaddr *)&server->dstaddr, ipaddr,
				  strlen(ipaddr));
	spin_unlock(&cifs_tcp_ses_lock);
	kfree(ipaddr);

	return !rc ? -1 : 0;
}
#else
static inline int reconn_set_ipaddr(struct TCP_Server_Info *server)
{
	return 0;
}
#endif

/*
 * cifs tcp session reconnection
 *
 * mark tcp session as reconnecting so temporarily locked
 * mark all smb sessions as reconnecting for tcp session
 * reconnect tcp session
 * wake up waiters on reconnection? - (not needed currently)
 */
int
cifs_reconnect(struct TCP_Server_Info *server)
{
	int rc = 0;
	struct list_head *tmp, *tmp2;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct mid_q_entry *mid_entry;
	struct list_head retry_list;

	spin_lock(&GlobalMid_Lock);
	if (server->tcpStatus == CifsExiting) {
		/* the demux thread will exit normally
		next time through the loop */
		spin_unlock(&GlobalMid_Lock);
		return rc;
	} else
		server->tcpStatus = CifsNeedReconnect;
	spin_unlock(&GlobalMid_Lock);
	server->maxBuf = 0;

	cFYI(1, ("Reconnecting tcp session"));

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		if (ses->server) {
			if (ses->server == server) {
				ses->status = CifsNeedReconnect;
				ses->ipc_tid = 0;
			}
		}
		/* else tcp and smb sessions need reconnection */
	}
	list_for_each(tmp, &GlobalTreeConnectionList) {
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		if ((tcon->ses) && (tcon->ses->server == server))
			tcon->tidStatus = CifsNeedReconnect;
	}
	read_unlock(&GlobalSMBSeslock);
	/* do not want to be sending data on a socket we are freeing */
	down(&server->tcpSem);
	if (server->ssocket) {
		cFYI(1, ("State: 0x%x Flags: 0x%lx", server->ssocket->state,
			server->ssocket->flags));
		kernel_sock_shutdown(server->ssocket, SHUT_WR);
		cFYI(1, ("Post shutdown state: 0x%x Flags: 0x%lx",
			server->ssocket->state,
			server->ssocket->flags));
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}

	spin_lock(&GlobalMid_Lock);
	list_for_each(tmp, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct
					mid_q_entry,
					qhead);
		if (mid_entry->midState == MID_REQUEST_SUBMITTED) {
				/* Mark other intransit requests as needing
				   retry so we do not immediately mark the
				   session bad again (ie after we reconnect
				   below) as they timeout too */
			mid_entry->midState = MID_RETRY_NEEDED;
		}
	}
	spin_unlock(&GlobalMid_Lock);
	up(&server->tcpSem);

	while ((!kthread_should_stop()) && (server->tcpStatus != CifsGood)) {
		try_to_freeze();
		if (server->protocolType == IPV6) {
			rc = ipv6_connect(&server->addr.sockAddr6,
					  &server->ssocket);
		} else {
			rc = ipv4_connect(&server->addr.sockAddr,
					&server->ssocket,
					server->workstation_RFC1001_name,
					server->server_RFC1001_name);
		}
		if (rc) {
			cFYI(1, ("reconnect error %d", rc));
#ifdef CONFIG_CIFS_SMB2
	server->max_read = 0;
#endif

	cifs_dbg(FYI, "Reconnecting tcp session\n");

	/* before reconnecting the tcp session, mark the smb session (uid)
		and the tid bad so they are not used until reconnected */
	cifs_dbg(FYI, "%s: marking sessions and tcons for reconnect\n",
		 __func__);
	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &server->smb_ses_list) {
		ses = list_entry(tmp, struct cifs_ses, smb_ses_list);
		ses->need_reconnect = true;
		ses->ipc_tid = 0;
		list_for_each(tmp2, &ses->tcon_list) {
			tcon = list_entry(tmp2, struct cifs_tcon, tcon_list);
			tcon->need_reconnect = true;
		}
	}
	spin_unlock(&cifs_tcp_ses_lock);

	/* do not want to be sending data on a socket we are freeing */
	cifs_dbg(FYI, "%s: tearing down socket\n", __func__);
	mutex_lock(&server->srv_mutex);
	if (server->ssocket) {
		cifs_dbg(FYI, "State: 0x%x Flags: 0x%lx\n",
			 server->ssocket->state, server->ssocket->flags);
		kernel_sock_shutdown(server->ssocket, SHUT_WR);
		cifs_dbg(FYI, "Post shutdown state: 0x%x Flags: 0x%lx\n",
			 server->ssocket->state, server->ssocket->flags);
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}
	server->sequence_number = 0;
	server->session_estab = false;
	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;
	server->lstrp = jiffies;

	/* mark submitted MIDs for retry and issue callback */
	INIT_LIST_HEAD(&retry_list);
	cifs_dbg(FYI, "%s: moving mids to private list\n", __func__);
	spin_lock(&GlobalMid_Lock);
	list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		if (mid_entry->mid_state == MID_REQUEST_SUBMITTED)
			mid_entry->mid_state = MID_RETRY_NEEDED;
		list_move(&mid_entry->qhead, &retry_list);
	}
	spin_unlock(&GlobalMid_Lock);
	mutex_unlock(&server->srv_mutex);

	cifs_dbg(FYI, "%s: issuing mid callbacks\n", __func__);
	list_for_each_safe(tmp, tmp2, &retry_list) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
		list_del_init(&mid_entry->qhead);
		mid_entry->callback(mid_entry);
	}

	do {
		try_to_freeze();

		/* we should try only the port we connected to before */
		mutex_lock(&server->srv_mutex);
		rc = generic_ip_connect(server);
		if (rc) {
			cifs_dbg(FYI, "reconnect error %d\n", rc);
			rc = reconn_set_ipaddr(server);
			if (rc) {
				cifs_dbg(FYI, "%s: failed to resolve hostname: %d\n",
					 __func__, rc);
			}
			mutex_unlock(&server->srv_mutex);
			msleep(3000);
		} else {
			atomic_inc(&tcpSesReconnectCount);
			spin_lock(&GlobalMid_Lock);
			if (!kthread_should_stop())
				server->tcpStatus = CifsGood;
			server->sequence_number = 0;
			spin_unlock(&GlobalMid_Lock);
	/*		atomic_set(&server->inFlight,0);*/
			wake_up(&server->response_q);
		}
	}
	return rc;
}

/*
	return codes:
		0 	not a transact2, or all data present
		>0 	transact2 with that much data missing
		-EINVAL = invalid transact2

 */
static int check2ndT2(struct smb_hdr *pSMB, unsigned int maxBufSize)
{
	struct smb_t2_rsp *pSMBt;
	int total_data_size;
	int data_in_this_rsp;
	int remaining;

	if (pSMB->Command != SMB_COM_TRANSACTION2)
		return 0;

	/* check for plausible wct, bcc and t2 data and parm sizes */
	/* check for parm and data offset going beyond end of smb */
	if (pSMB->WordCount != 10) { /* coalesce_t2 depends on this */
		cFYI(1, ("invalid transact2 word count"));
		return -EINVAL;
	}

	pSMBt = (struct smb_t2_rsp *)pSMB;

	total_data_size = le16_to_cpu(pSMBt->t2_rsp.TotalDataCount);
	data_in_this_rsp = le16_to_cpu(pSMBt->t2_rsp.DataCount);

	remaining = total_data_size - data_in_this_rsp;

	if (remaining == 0)
		return 0;
	else if (remaining < 0) {
		cFYI(1, ("total data %d smaller than data in frame %d",
			total_data_size, data_in_this_rsp));
		return -EINVAL;
	} else {
		cFYI(1, ("missing %d bytes from transact2, check next response",
			remaining));
		if (total_data_size > maxBufSize) {
			cERROR(1, ("TotalDataSize %d is over maximum buffer %d",
				total_data_size, maxBufSize));
			return -EINVAL;
		}
		return remaining;
	}
}

static int coalesce_t2(struct smb_hdr *psecond, struct smb_hdr *pTargetSMB)
{
	struct smb_t2_rsp *pSMB2 = (struct smb_t2_rsp *)psecond;
	struct smb_t2_rsp *pSMBt  = (struct smb_t2_rsp *)pTargetSMB;
	int total_data_size;
	int total_in_buf;
	int remaining;
	int total_in_buf2;
	char *data_area_of_target;
	char *data_area_of_buf2;
	__u16 byte_count;

	total_data_size = le16_to_cpu(pSMBt->t2_rsp.TotalDataCount);

	if (total_data_size != le16_to_cpu(pSMB2->t2_rsp.TotalDataCount)) {
		cFYI(1, ("total data size of primary and secondary t2 differ"));
	}

	total_in_buf = le16_to_cpu(pSMBt->t2_rsp.DataCount);

	remaining = total_data_size - total_in_buf;

	if (remaining < 0)
		return -EINVAL;

	if (remaining == 0) /* nothing to do, ignore */
		return 0;

	total_in_buf2 = le16_to_cpu(pSMB2->t2_rsp.DataCount);
	if (remaining < total_in_buf2) {
		cFYI(1, ("transact2 2nd response contains too much data"));
	}

	/* find end of first SMB data area */
	data_area_of_target = (char *)&pSMBt->hdr.Protocol +
				le16_to_cpu(pSMBt->t2_rsp.DataOffset);
	/* validate target area */

	data_area_of_buf2 = (char *) &pSMB2->hdr.Protocol +
					le16_to_cpu(pSMB2->t2_rsp.DataOffset);

	data_area_of_target += total_in_buf;

	/* copy second buffer into end of first buffer */
	memcpy(data_area_of_target, data_area_of_buf2, total_in_buf2);
	total_in_buf += total_in_buf2;
	pSMBt->t2_rsp.DataCount = cpu_to_le16(total_in_buf);
	byte_count = le16_to_cpu(BCC_LE(pTargetSMB));
	byte_count += total_in_buf2;
	BCC_LE(pTargetSMB) = cpu_to_le16(byte_count);

	byte_count = pTargetSMB->smb_buf_length;
	byte_count += total_in_buf2;

	/* BB also add check that we are not beyond maximum buffer size */

	pTargetSMB->smb_buf_length = byte_count;

	if (remaining == total_in_buf2) {
		cFYI(1, ("found the last secondary response"));
		return 0; /* we are done */
	} else /* more responses to go */
		return 1;

}

static int
cifs_demultiplex_thread(struct TCP_Server_Info *server)
{
	int length;
	unsigned int pdu_length, total_read;
	struct smb_hdr *smb_buffer = NULL;
	struct smb_hdr *bigbuf = NULL;
	struct smb_hdr *smallbuf = NULL;
	struct msghdr smb_msg;
	struct kvec iov;
	struct socket *csocket = server->ssocket;
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;
	char temp;
	bool isLargeBuf = false;
	bool isMultiRsp;
	int reconnect;

	current->flags |= PF_MEMALLOC;
	cFYI(1, ("Demultiplex PID: %d", task_pid_nr(current)));

	length = atomic_inc_return(&tcpSesAllocCount);
	if (length > 1)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv,
				GFP_KERNEL);

	set_freezable();
	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;
		if (bigbuf == NULL) {
			bigbuf = cifs_buf_get();
			if (!bigbuf) {
				cERROR(1, ("No memory for large SMB response"));
				msleep(3000);
				/* retry will check if exiting */
				continue;
			}
		} else if (isLargeBuf) {
			/* we are reusing a dirty large buf, clear its start */
			memset(bigbuf, 0, sizeof(struct smb_hdr));
		}

		if (smallbuf == NULL) {
			smallbuf = cifs_small_buf_get();
			if (!smallbuf) {
				cERROR(1, ("No memory for SMB response"));
				msleep(1000);
				/* retry will check if exiting */
				continue;
			}
			/* beginning of smb buffer is cleared in our buf_get */
		} else /* if existing small buf clear beginning */
			memset(smallbuf, 0, sizeof(struct smb_hdr));

		isLargeBuf = false;
		isMultiRsp = false;
		smb_buffer = smallbuf;
		iov.iov_base = smb_buffer;
		iov.iov_len = 4;
		smb_msg.msg_control = NULL;
		smb_msg.msg_controllen = 0;
		pdu_length = 4; /* enough to get RFC1001 header */
incomplete_rcv:
		length =
		    kernel_recvmsg(csocket, &smb_msg,
				&iov, 1, pdu_length, 0 /* BB other flags? */);

		if (kthread_should_stop()) {
			break;
		} else if (server->tcpStatus == CifsNeedReconnect) {
			cFYI(1, ("Reconnect after server stopped responding"));
			cifs_reconnect(server);
			cFYI(1, ("call to reconnect done"));
			csocket = server->ssocket;
			continue;
		} else if ((length == -ERESTARTSYS) || (length == -EAGAIN)) {
			msleep(1); /* minimum sleep to prevent looping
				allowing socket to clear and app threads to set
				tcpStatus CifsNeedReconnect if server hung */
			if (pdu_length < 4)
				goto incomplete_rcv;
			else
				continue;
		} else if (length <= 0) {
			if (server->tcpStatus == CifsNew) {
				cFYI(1, ("tcp session abend after SMBnegprot"));
				/* some servers kill the TCP session rather than
				   returning an SMB negprot error, in which
				   case reconnecting here is not going to help,
				   and so simply return error to mount */
				break;
			}
			if (!try_to_freeze() && (length == -EINTR)) {
				cFYI(1, ("cifsd thread killed"));
				break;
			}
			cFYI(1, ("Reconnect after unexpected peek error %d",
				length));
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		} else if (length < pdu_length) {
			cFYI(1, ("requested %d bytes but only got %d bytes",
				  pdu_length, length));
			pdu_length -= length;
			msleep(1);
			goto incomplete_rcv;
		}

		/* The right amount was read from socket - 4 bytes */
		/* so we can now interpret the length field */

		/* the first byte big endian of the length field,
		is actually not part of the length but the type
		with the most common, zero, as regular data */
		temp = *((char *) smb_buffer);

		/* Note that FC 1001 length is big endian on the wire,
		but we convert it here so it is always manipulated
		as host byte order */
		pdu_length = be32_to_cpu((__force __be32)smb_buffer->smb_buf_length);
		smb_buffer->smb_buf_length = pdu_length;

		cFYI(1, ("rfc1002 length 0x%x", pdu_length+4));

		if (temp == (char) RFC1002_SESSION_KEEP_ALIVE) {
			continue;
		} else if (temp == (char)RFC1002_POSITIVE_SESSION_RESPONSE) {
			cFYI(1, ("Good RFC 1002 session rsp"));
			continue;
		} else if (temp == (char)RFC1002_NEGATIVE_SESSION_RESPONSE) {
			/* we get this from Windows 98 instead of
			   an error on SMB negprot response */
			cFYI(1, ("Negative RFC1002 Session Response Error 0x%x)",
				pdu_length));
			if (server->tcpStatus == CifsNew) {
				/* if nack on negprot (rather than
				ret of smb negprot error) reconnecting
				not going to help, ret error to mount */
				break;
			} else {
				/* give server a second to
				clean up before reconnect attempt */
				msleep(1000);
				/* always try 445 first on reconnect
				since we get NACK on some if we ever
				connected to port 139 (the NACK is
				since we do not begin with RFC1001
				session initialize frame) */
				server->addr.sockAddr.sin_port =
					htons(CIFS_PORT);
				cifs_reconnect(server);
				csocket = server->ssocket;
				wake_up(&server->response_q);
				continue;
			}
		} else if (temp != (char) 0) {
			cERROR(1, ("Unknown RFC 1002 frame"));
			cifs_dump_mem(" Received Data: ", (char *)smb_buffer,
				      length);
			cifs_reconnect(server);
			csocket = server->ssocket;
			continue;
		}

		/* else we have an SMB response */
		if ((pdu_length > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4) ||
			    (pdu_length < sizeof(struct smb_hdr) - 1 - 4)) {
			cERROR(1, ("Invalid size SMB length %d pdu_length %d",
					length, pdu_length+4));
			cifs_reconnect(server);
			csocket = server->ssocket;
			wake_up(&server->response_q);
			continue;
		}

		/* else length ok */
		reconnect = 0;

		if (pdu_length > MAX_CIFS_SMALL_BUFFER_SIZE - 4) {
			isLargeBuf = true;
			memcpy(bigbuf, smallbuf, 4);
			smb_buffer = bigbuf;
		}
		length = 0;
		iov.iov_base = 4 + (char *)smb_buffer;
		iov.iov_len = pdu_length;
		for (total_read = 0; total_read < pdu_length;
		     total_read += length) {
			length = kernel_recvmsg(csocket, &smb_msg, &iov, 1,
						pdu_length - total_read, 0);
			if (kthread_should_stop() ||
			    (length == -EINTR)) {
				/* then will exit */
				reconnect = 2;
				break;
			} else if (server->tcpStatus == CifsNeedReconnect) {
				cifs_reconnect(server);
				csocket = server->ssocket;
				/* Reconnect wakes up rspns q */
				/* Now we will reread sock */
				reconnect = 1;
				break;
			} else if ((length == -ERESTARTSYS) ||
				   (length == -EAGAIN)) {
				msleep(1); /* minimum sleep to prevent looping,
					      allowing socket to clear and app
					      threads to set tcpStatus
					      CifsNeedReconnect if server hung*/
				length = 0;
				continue;
			} else if (length <= 0) {
				cERROR(1, ("Received no data, expecting %d",
					      pdu_length - total_read));
				cifs_reconnect(server);
				csocket = server->ssocket;
				reconnect = 1;
				break;
			}
		}
		if (reconnect == 2)
			break;
		else if (reconnect == 1)
			continue;

		length += 4; /* account for rfc1002 hdr */


		dump_smb(smb_buffer, length);
		if (checkSMB(smb_buffer, smb_buffer->Mid, total_read+4)) {
			cifs_dump_mem("Bad SMB: ", smb_buffer, 48);
			continue;
		}


		task_to_wake = NULL;
		spin_lock(&GlobalMid_Lock);
		list_for_each(tmp, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);

			if ((mid_entry->mid == smb_buffer->Mid) &&
			    (mid_entry->midState == MID_REQUEST_SUBMITTED) &&
			    (mid_entry->command == smb_buffer->Command)) {
				if (check2ndT2(smb_buffer,server->maxBuf) > 0) {
					/* We have a multipart transact2 resp */
					isMultiRsp = true;
					if (mid_entry->resp_buf) {
						/* merge response - fix up 1st*/
						if (coalesce_t2(smb_buffer,
							mid_entry->resp_buf)) {
							mid_entry->multiRsp =
								 true;
							break;
						} else {
							/* all parts received */
							mid_entry->multiEnd =
								 true;
							goto multi_t2_fnd;
						}
					} else {
						if (!isLargeBuf) {
							cERROR(1,("1st trans2 resp needs bigbuf"));
					/* BB maybe we can fix this up,  switch
					   to already allocated large buffer? */
						} else {
							/* Have first buffer */
							mid_entry->resp_buf =
								 smb_buffer;
							mid_entry->largeBuf =
								 true;
							bigbuf = NULL;
						}
					}
					break;
				}
				mid_entry->resp_buf = smb_buffer;
				mid_entry->largeBuf = isLargeBuf;
multi_t2_fnd:
				task_to_wake = mid_entry->tsk;
				mid_entry->midState = MID_RESPONSE_RECEIVED;
#ifdef CONFIG_CIFS_STATS2
				mid_entry->when_received = jiffies;
#endif
				/* so we do not time out requests to  server
				which is still responding (since server could
				be busy but not dead) */
				server->lstrp = jiffies;
				break;
			}
		}
		spin_unlock(&GlobalMid_Lock);
		if (task_to_wake) {
			/* Was previous buf put in mpx struct for multi-rsp? */
			if (!isMultiRsp) {
				/* smb buffer will be freed by user thread */
				if (isLargeBuf)
					bigbuf = NULL;
				else
					smallbuf = NULL;
			}
			wake_up_process(task_to_wake);
		} else if (!is_valid_oplock_break(smb_buffer, server) &&
			   !isMultiRsp) {
			cERROR(1, ("No task to wake, unknown frame received! "
				   "NumMids %d", midCount.counter));
			cifs_dump_mem("Received Data is: ", (char *)smb_buffer,
				      sizeof(struct smb_hdr));
#ifdef CONFIG_CIFS_DEBUG2
			cifs_dump_detail(smb_buffer);
			cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */

		}
	} /* end while !EXITING */
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsNeedNegotiate;
			spin_unlock(&GlobalMid_Lock);
			mutex_unlock(&server->srv_mutex);
		}
	} while (server->tcpStatus == CifsNeedReconnect);

	if (server->tcpStatus == CifsNeedNegotiate)
		mod_delayed_work(cifsiod_wq, &server->echo, 0);

	return rc;
}

static void
cifs_echo_request(struct work_struct *work)
{
	int rc;
	struct TCP_Server_Info *server = container_of(work,
					struct TCP_Server_Info, echo.work);
	unsigned long echo_interval;

	/*
	 * If we need to renegotiate, set echo interval to zero to
	 * immediately call echo service where we can renegotiate.
	 */
	if (server->tcpStatus == CifsNeedNegotiate)
		echo_interval = 0;
	else
		echo_interval = SMB_ECHO_INTERVAL;

	/*
	 * We cannot send an echo if it is disabled.
	 * Also, no need to ping if we got a response recently.
	 */

	if (server->tcpStatus == CifsNeedReconnect ||
	    server->tcpStatus == CifsExiting ||
	    server->tcpStatus == CifsNew ||
	    (server->ops->can_echo && !server->ops->can_echo(server)) ||
	    time_before(jiffies, server->lstrp + echo_interval - HZ))
		goto requeue_echo;

	rc = server->ops->echo ? server->ops->echo(server) : -ENOSYS;
	if (rc)
		cifs_dbg(FYI, "Unable to send echo request to server: %s\n",
			 server->hostname);

requeue_echo:
	queue_delayed_work(cifsiod_wq, &server->echo, SMB_ECHO_INTERVAL);
}

static bool
allocate_buffers(struct TCP_Server_Info *server)
{
	if (!server->bigbuf) {
		server->bigbuf = (char *)cifs_buf_get();
		if (!server->bigbuf) {
			cifs_dbg(VFS, "No memory for large SMB response\n");
			msleep(3000);
			/* retry will check if exiting */
			return false;
		}
	} else if (server->large_buf) {
		/* we are reusing a dirty large buf, clear its start */
		memset(server->bigbuf, 0, HEADER_SIZE(server));
	}

	if (!server->smallbuf) {
		server->smallbuf = (char *)cifs_small_buf_get();
		if (!server->smallbuf) {
			cifs_dbg(VFS, "No memory for SMB response\n");
			msleep(1000);
			/* retry will check if exiting */
			return false;
		}
		/* beginning of smb buffer is cleared in our buf_get */
	} else {
		/* if existing small buf clear beginning */
		memset(server->smallbuf, 0, HEADER_SIZE(server));
	}

	return true;
}

static bool
server_unresponsive(struct TCP_Server_Info *server)
{
	/*
	 * We need to wait 3 echo intervals to make sure we handle such
	 * situations right:
	 * 1s  client sends a normal SMB request
	 * 3s  client gets a response
	 * 30s echo workqueue job pops, and decides we got a response recently
	 *     and don't need to send another
	 * ...
	 * 65s kernel_recvmsg times out, and we see that we haven't gotten
	 *     a response in >60s.
	 */
	if ((server->tcpStatus == CifsGood ||
	    server->tcpStatus == CifsNeedNegotiate) &&
	    time_after(jiffies, server->lstrp + 3 * SMB_ECHO_INTERVAL)) {
		cifs_dbg(VFS, "Server %s has not responded in %d seconds. Reconnecting...\n",
			 server->hostname, (3 * SMB_ECHO_INTERVAL) / HZ);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return true;
	}

	return false;
}

/*
 * kvec_array_init - clone a kvec array, and advance into it
 * @new:	pointer to memory for cloned array
 * @iov:	pointer to original array
 * @nr_segs:	number of members in original array
 * @bytes:	number of bytes to advance into the cloned array
 *
 * This function will copy the array provided in iov to a section of memory
 * and advance the specified number of bytes into the new array. It returns
 * the number of segments in the new array. "new" must be at least as big as
 * the original iov array.
 */
static unsigned int
kvec_array_init(struct kvec *new, struct kvec *iov, unsigned int nr_segs,
		size_t bytes)
{
	size_t base = 0;

	while (bytes || !iov->iov_len) {
		int copy = min(bytes, iov->iov_len);

		bytes -= copy;
		base += copy;
		if (iov->iov_len == base) {
			iov++;
			nr_segs--;
			base = 0;
		}
	}
	memcpy(new, iov, sizeof(*iov) * nr_segs);
	new->iov_base += base;
	new->iov_len -= base;
	return nr_segs;
}

static struct kvec *
get_server_iovec(struct TCP_Server_Info *server, unsigned int nr_segs)
{
	struct kvec *new_iov;

	if (server->iov && nr_segs <= server->nr_iov)
		return server->iov;

	/* not big enough -- allocate a new one and release the old */
	new_iov = kmalloc(sizeof(*new_iov) * nr_segs, GFP_NOFS);
	if (new_iov) {
		kfree(server->iov);
		server->iov = new_iov;
		server->nr_iov = nr_segs;
	}
	return new_iov;
}

int
cifs_readv_from_socket(struct TCP_Server_Info *server, struct kvec *iov_orig,
		       unsigned int nr_segs, unsigned int to_read)
{
	int length = 0;
	int total_read;
	unsigned int segs;
	struct msghdr smb_msg;
	struct kvec *iov;

	iov = get_server_iovec(server, nr_segs);
	if (!iov)
		return -ENOMEM;

	smb_msg.msg_control = NULL;
	smb_msg.msg_controllen = 0;

	for (total_read = 0; to_read; total_read += length, to_read -= length) {
		try_to_freeze();

		if (server_unresponsive(server)) {
			total_read = -ECONNABORTED;
			break;
		}

		segs = kvec_array_init(iov, iov_orig, nr_segs, total_read);

		length = kernel_recvmsg(server->ssocket, &smb_msg,
					iov, segs, to_read, 0);

		if (server->tcpStatus == CifsExiting) {
			total_read = -ESHUTDOWN;
			break;
		} else if (server->tcpStatus == CifsNeedReconnect) {
			cifs_reconnect(server);
			total_read = -ECONNABORTED;
			break;
		} else if (length == -ERESTARTSYS ||
			   length == -EAGAIN ||
			   length == -EINTR) {
			/*
			 * Minimum sleep to prevent looping, allowing socket
			 * to clear and app threads to set tcpStatus
			 * CifsNeedReconnect if server hung.
			 */
			usleep_range(1000, 2000);
			length = 0;
			continue;
		} else if (length <= 0) {
			cifs_dbg(FYI, "Received no data or error: expecting %d\n"
				 "got %d", to_read, length);
			cifs_reconnect(server);
			total_read = -ECONNABORTED;
			break;
		}
	}
	return total_read;
}

int
cifs_read_from_socket(struct TCP_Server_Info *server, char *buf,
		      unsigned int to_read)
{
	struct kvec iov;

	iov.iov_base = buf;
	iov.iov_len = to_read;

	return cifs_readv_from_socket(server, &iov, 1, to_read);
}

static bool
is_smb_response(struct TCP_Server_Info *server, unsigned char type)
{
	/*
	 * The first byte big endian of the length field,
	 * is actually not part of the length but the type
	 * with the most common, zero, as regular data.
	 */
	switch (type) {
	case RFC1002_SESSION_MESSAGE:
		/* Regular SMB response */
		return true;
	case RFC1002_SESSION_KEEP_ALIVE:
		cifs_dbg(FYI, "RFC 1002 session keep alive\n");
		break;
	case RFC1002_POSITIVE_SESSION_RESPONSE:
		cifs_dbg(FYI, "RFC 1002 positive session response\n");
		break;
	case RFC1002_NEGATIVE_SESSION_RESPONSE:
		/*
		 * We get this from Windows 98 instead of an error on
		 * SMB negprot response.
		 */
		cifs_dbg(FYI, "RFC 1002 negative session response\n");
		/* give server a second to clean up */
		msleep(1000);
		/*
		 * Always try 445 first on reconnect since we get NACK
		 * on some if we ever connected to port 139 (the NACK
		 * is since we do not begin with RFC1001 session
		 * initialize frame).
		 */
		cifs_set_port((struct sockaddr *)&server->dstaddr, CIFS_PORT);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		break;
	default:
		cifs_dbg(VFS, "RFC 1002 unknown response type 0x%x\n", type);
		cifs_reconnect(server);
	}

	return false;
}

void
dequeue_mid(struct mid_q_entry *mid, bool malformed)
{
#ifdef CONFIG_CIFS_STATS2
	mid->when_received = jiffies;
#endif
	spin_lock(&GlobalMid_Lock);
	if (!malformed)
		mid->mid_state = MID_RESPONSE_RECEIVED;
	else
		mid->mid_state = MID_RESPONSE_MALFORMED;
	list_del_init(&mid->qhead);
	spin_unlock(&GlobalMid_Lock);
}

static void
handle_mid(struct mid_q_entry *mid, struct TCP_Server_Info *server,
	   char *buf, int malformed)
{
	if (server->ops->check_trans2 &&
	    server->ops->check_trans2(mid, server, buf, malformed))
		return;
	mid->resp_buf = buf;
	mid->large_buf = server->large_buf;
	/* Was previous buf put in mpx struct for multi-rsp? */
	if (!mid->multiRsp) {
		/* smb buffer will be freed by user thread */
		if (server->large_buf)
			server->bigbuf = NULL;
		else
			server->smallbuf = NULL;
	}
	dequeue_mid(mid, malformed);
}

static void clean_demultiplex_info(struct TCP_Server_Info *server)
{
	int length;

	/* take it off the list, if it's not already */
	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cancel_delayed_work_sync(&server->echo);

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);
	wake_up_all(&server->response_q);

	/* don't exit until kthread_stop is called */
	set_current_state(TASK_UNINTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_UNINTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);

	/* check if we have blocked requests that need to free */
	/* Note that cifs_max_pending is normally 50, but
	can be set at module install time to as little as two */
	spin_lock(&GlobalMid_Lock);
	if (atomic_read(&server->inFlight) >= cifs_max_pending)
		atomic_set(&server->inFlight, cifs_max_pending - 1);
	/* We do not want to set the max_pending too low or we
	could end up with the counter going negative */
	spin_unlock(&GlobalMid_Lock);
	/* Although there should not be any requests blocked on
	this queue it can not hurt to be paranoid and try to wake up requests
	that may haven been blocked when more than 50 at time were on the wire
	to the same server - they now will see the session is in exit state
	and get out of SendReceive.  */
	/* check if we have blocked requests that need to free */
	spin_lock(&server->req_lock);
	if (server->credits <= 0)
		server->credits = 1;
	spin_unlock(&server->req_lock);
	/*
	 * Although there should not be any requests blocked on this queue it
	 * can not hurt to be paranoid and try to wake up requests that may
	 * haven been blocked when more than 50 at time were on the wire to the
	 * same server - they now will see the session is in exit state and get
	 * out of SendReceive.
	 */
	wake_up_all(&server->request_q);
	/* give those requests time to exit */
	msleep(125);

	if (server->ssocket) {
		sock_release(csocket);
		server->ssocket = NULL;
	}
	/* buffer usuallly freed in free_mid - need to free it here on exit */
	cifs_buf_release(bigbuf);
	if (smallbuf) /* no sense logging a debug message if NULL */
		cifs_small_buf_release(smallbuf);

	read_lock(&GlobalSMBSeslock);
	if (list_empty(&server->pending_mid_q)) {
		/* loop through server session structures attached to this and
		    mark them dead */
		list_for_each(tmp, &GlobalSMBSessionList) {
			ses =
			    list_entry(tmp, struct cifsSesInfo,
				       cifsSessionList);
			if (ses->server == server) {
				ses->status = CifsExiting;
				ses->server = NULL;
			}
		}
		read_unlock(&GlobalSMBSeslock);
	} else {
		/* although we can not zero the server struct pointer yet,
		since there are active requests which may depnd on them,
		mark the corresponding SMB sessions as exiting too */
		list_for_each(tmp, &GlobalSMBSessionList) {
			ses = list_entry(tmp, struct cifsSesInfo,
					 cifsSessionList);
			if (ses->server == server)
				ses->status = CifsExiting;
		}

		spin_lock(&GlobalMid_Lock);
		list_for_each(tmp, &server->pending_mid_q) {
		mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			if (mid_entry->midState == MID_REQUEST_SUBMITTED) {
				cFYI(1, ("Clearing Mid 0x%x - waking up ",
					 mid_entry->mid));
				task_to_wake = mid_entry->tsk;
				if (task_to_wake)
					wake_up_process(task_to_wake);
			}
		}
		spin_unlock(&GlobalMid_Lock);
		read_unlock(&GlobalSMBSeslock);
		sock_release(server->ssocket);
		server->ssocket = NULL;
	}

	if (!list_empty(&server->pending_mid_q)) {
		struct list_head dispose_list;
		struct mid_q_entry *mid_entry;
		struct list_head *tmp, *tmp2;

		INIT_LIST_HEAD(&dispose_list);
		spin_lock(&GlobalMid_Lock);
		list_for_each_safe(tmp, tmp2, &server->pending_mid_q) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Clearing mid 0x%llx\n", mid_entry->mid);
			mid_entry->mid_state = MID_SHUTDOWN;
			list_move(&mid_entry->qhead, &dispose_list);
		}
		spin_unlock(&GlobalMid_Lock);

		/* now walk dispose list and issue callbacks */
		list_for_each_safe(tmp, tmp2, &dispose_list) {
			mid_entry = list_entry(tmp, struct mid_q_entry, qhead);
			cifs_dbg(FYI, "Callback mid 0x%llx\n", mid_entry->mid);
			list_del_init(&mid_entry->qhead);
			mid_entry->callback(mid_entry);
		}
		/* 1/8th of sec is more than enough time for them to exit */
		msleep(125);
	}

	if (!list_empty(&server->pending_mid_q)) {
		/* mpx threads have not exited yet give them
		at least the smb send timeout time for long ops */
		/* due to delays on oplock break requests, we need
		to wait at least 45 seconds before giving up
		on a request getting a response and going ahead
		and killing cifsd */
		cFYI(1, ("Wait for exit from demultiplex thread"));
		msleep(46000);
		/* if threads still have not exited they are probably never
		coming home not much else we can do but free the memory */
	}

	/* last chance to mark ses pointers invalid
	if there are any pointing to this (e.g
	if a crazy root user tried to kill cifsd
	kernel thread explicitly this might happen) */
	write_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		ses = list_entry(tmp, struct cifsSesInfo,
				cifsSessionList);
		if (ses->server == server)
			ses->server = NULL;
	}
	write_unlock(&GlobalSMBSeslock);

	kfree(server->hostname);
	kfree(server);

	length = atomic_dec_return(&tcpSesAllocCount);
	if (length  > 0)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv,
				GFP_KERNEL);

	return 0;
}

		/*
		 * mpx threads have not exited yet give them at least the smb
		 * send timeout time for long ops.
		 *
		 * Due to delays on oplock break requests, we need to wait at
		 * least 45 seconds before giving up on a request getting a
		 * response and going ahead and killing cifsd.
		 */
		cifs_dbg(FYI, "Wait for exit from demultiplex thread\n");
		msleep(46000);
		/*
		 * If threads still have not exited they are probably never
		 * coming home not much else we can do but free the memory.
		 */
	}

	kfree(server->hostname);
	kfree(server->iov);
	kfree(server);

	length = atomic_dec_return(&tcpSesAllocCount);
	if (length > 0)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);
}

static int
standard_receive3(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	int length;
	char *buf = server->smallbuf;
	unsigned int pdu_length = get_rfc1002_length(buf);

	/* make sure this will fit in a large buffer */
	if (pdu_length > CIFSMaxBufSize + MAX_HEADER_SIZE(server) - 4) {
		cifs_dbg(VFS, "SMB response too long (%u bytes)\n", pdu_length);
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -ECONNABORTED;
	}

	/* switch to large buffer if too big for a small one */
	if (pdu_length > MAX_CIFS_SMALL_BUFFER_SIZE - 4) {
		server->large_buf = true;
		memcpy(server->bigbuf, buf, server->total_read);
		buf = server->bigbuf;
	}

	/* now read the rest */
	length = cifs_read_from_socket(server, buf + HEADER_SIZE(server) - 1,
				pdu_length - HEADER_SIZE(server) + 1 + 4);
	if (length < 0)
		return length;
	server->total_read += length;

	dump_smb(buf, server->total_read);

	/*
	 * We know that we received enough to get to the MID as we
	 * checked the pdu_length earlier. Now check to see
	 * if the rest of the header is OK. We borrow the length
	 * var for the rest of the loop to avoid a new stack var.
	 *
	 * 48 bytes is enough to display the header and a little bit
	 * into the payload for debugging purposes.
	 */
	length = server->ops->check_message(buf, server->total_read);
	if (length != 0)
		cifs_dump_mem("Bad SMB: ", buf,
			min_t(unsigned int, server->total_read, 48));

	if (server->ops->is_session_expired &&
	    server->ops->is_session_expired(buf)) {
		cifs_reconnect(server);
		wake_up(&server->response_q);
		return -1;
	}

	if (server->ops->is_status_pending &&
	    server->ops->is_status_pending(buf, server, length))
		return -1;

	if (!mid)
		return length;

	handle_mid(mid, server, buf, length);
	return 0;
}

static int
cifs_demultiplex_thread(void *p)
{
	int length;
	struct TCP_Server_Info *server = p;
	unsigned int pdu_length;
	char *buf = NULL;
	struct task_struct *task_to_wake = NULL;
	struct mid_q_entry *mid_entry;

	current->flags |= PF_MEMALLOC;
	cifs_dbg(FYI, "Demultiplex PID: %d\n", task_pid_nr(current));

	length = atomic_inc_return(&tcpSesAllocCount);
	if (length > 1)
		mempool_resize(cifs_req_poolp, length + cifs_min_rcv);

	set_freezable();
	allow_kernel_signal(SIGKILL);
	while (server->tcpStatus != CifsExiting) {
		if (try_to_freeze())
			continue;

		if (!allocate_buffers(server))
			continue;

		server->large_buf = false;
		buf = server->smallbuf;
		pdu_length = 4; /* enough to get RFC1001 header */

		length = cifs_read_from_socket(server, buf, pdu_length);
		if (length < 0)
			continue;
		server->total_read = length;

		/*
		 * The right amount was read from socket - 4 bytes,
		 * so we can now interpret the length field.
		 */
		pdu_length = get_rfc1002_length(buf);

		cifs_dbg(FYI, "RFC1002 header 0x%x\n", pdu_length);
		if (!is_smb_response(server, buf[0]))
			continue;

		/* make sure we have enough to get to the MID */
		if (pdu_length < HEADER_SIZE(server) - 1 - 4) {
			cifs_dbg(VFS, "SMB response too short (%u bytes)\n",
				 pdu_length);
			cifs_reconnect(server);
			wake_up(&server->response_q);
			continue;
		}

		/* read down to the MID */
		length = cifs_read_from_socket(server, buf + 4,
					       HEADER_SIZE(server) - 1 - 4);
		if (length < 0)
			continue;
		server->total_read += length;

		mid_entry = server->ops->find_mid(server, buf);

		if (!mid_entry || !mid_entry->receive)
			length = standard_receive3(server, mid_entry);
		else
			length = mid_entry->receive(server, mid_entry);

		if (length < 0)
			continue;

		if (server->large_buf)
			buf = server->bigbuf;

		server->lstrp = jiffies;
		if (mid_entry != NULL) {
			if ((mid_entry->mid_flags & MID_WAIT_CANCELLED) &&
			     mid_entry->mid_state == MID_RESPONSE_RECEIVED &&
					server->ops->handle_cancelled_mid)
				server->ops->handle_cancelled_mid(
							mid_entry->resp_buf,
							server);

			if (!mid_entry->multiRsp || mid_entry->multiEnd)
				mid_entry->callback(mid_entry);
		} else if (server->ops->is_oplock_break &&
			   server->ops->is_oplock_break(buf, server)) {
			cifs_dbg(FYI, "Received oplock break\n");
		} else {
			cifs_dbg(VFS, "No task to wake, unknown frame received! NumMids %d\n",
				 atomic_read(&midCount));
			cifs_dump_mem("Received Data is: ", buf,
				      HEADER_SIZE(server));
#ifdef CONFIG_CIFS_DEBUG2
			if (server->ops->dump_detail)
				server->ops->dump_detail(buf);
			cifs_dump_mids(server);
#endif /* CIFS_DEBUG2 */

		}
	} /* end while !EXITING */

	/* buffer usually freed in free_mid - need to free it here on exit */
	cifs_buf_release(server->bigbuf);
	if (server->smallbuf) /* no sense logging a debug message if NULL */
		cifs_small_buf_release(server->smallbuf);

	task_to_wake = xchg(&server->tsk, NULL);
	clean_demultiplex_info(server);

	/* if server->tsk was NULL then wait for a signal before exiting */
	if (!task_to_wake) {
		set_current_state(TASK_INTERRUPTIBLE);
		while (!signal_pending(current)) {
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);
	}

	module_put_and_exit(0);
}

/* extract the host portion of the UNC string */
static char *
extract_hostname(const char *unc)
{
	const char *src;
	char *dst, *delim;
	unsigned int len;

	/* skip double chars at beginning of string */
	/* BB: check validity of these bytes? */
	src = unc + 2;

	/* delimiter between hostname and sharename is always '\\' now */
	delim = strchr(src, '\\');
	if (!delim)
		return ERR_PTR(-EINVAL);

	len = delim - src;
	dst = kmalloc((len + 1), GFP_KERNEL);
	if (dst == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(dst, src, len);
	dst[len] = '\0';

	return dst;
}

static int
cifs_parse_mount_options(char *options, const char *devname,
			 struct smb_vol *vol)
{
	char *value;
	char *data;
	unsigned int  temp_len, i, j;
	char separator[2];

	separator[0] = ',';
	separator[1] = 0;

	if (Local_System_Name[0] != 0)
		memcpy(vol->source_rfc1001_name, Local_System_Name, 15);
	else {
		char *nodename = utsname()->nodename;
		int n = strnlen(nodename, 15);
		memset(vol->source_rfc1001_name, 0x20, 15);
		for (i = 0; i < n; i++) {
			/* does not have to be perfect mapping since field is
			informational, only used for servers that do not support
			port 445 and it can be overridden at mount time */
			vol->source_rfc1001_name[i] = toupper(nodename[i]);
		}
	}
	vol->source_rfc1001_name[15] = 0;
	/* null target name indicates to use *SMBSERVR default called name
	   if we end up sending RFC1001 session initialize */
	vol->target_rfc1001_name[0] = 0;
	vol->linux_uid = current->uid;	/* current->euid instead? */
	vol->linux_gid = current->gid;
	vol->dir_mode = S_IRWXUGO;
	/* 2767 perms indicate mandatory locking support */
	vol->file_mode = (S_IRWXUGO | S_ISGID) & (~S_IXGRP);

	/* vol->retry default is 0 (i.e. "soft" limited retry not hard retry) */
	vol->rw = true;
	/* default is always to request posix paths. */
	vol->posix_paths = 1;

	if (!options)
		return 1;
static int get_option_ul(substring_t args[], unsigned long *option)
{
	int rc;
	char *string;

	string = match_strdup(args);
	if (string == NULL)
		return -ENOMEM;
	rc = kstrtoul(string, 0, option);
	kfree(string);

	return rc;
}

static int get_option_uid(substring_t args[], kuid_t *result)
{
	unsigned long value;
	kuid_t uid;
	int rc;

	rc = get_option_ul(args, &value);
	if (rc)
		return rc;

	uid = make_kuid(current_user_ns(), value);
	if (!uid_valid(uid))
		return -EINVAL;

	*result = uid;
	return 0;
}

static int get_option_gid(substring_t args[], kgid_t *result)
{
	unsigned long value;
	kgid_t gid;
	int rc;

	rc = get_option_ul(args, &value);
	if (rc)
		return rc;

	gid = make_kgid(current_user_ns(), value);
	if (!gid_valid(gid))
		return -EINVAL;

	*result = gid;
	return 0;
}

static int cifs_parse_security_flavors(char *value,
				       struct smb_vol *vol)
{

	substring_t args[MAX_OPT_ARGS];

	/*
	 * With mount options, the last one should win. Reset any existing
	 * settings back to default.
	 */
	vol->sectype = Unspecified;
	vol->sign = false;

	switch (match_token(value, cifs_secflavor_tokens, args)) {
	case Opt_sec_krb5p:
		cifs_dbg(VFS, "sec=krb5p is not supported!\n");
		return 1;
	case Opt_sec_krb5i:
		vol->sign = true;
		/* Fallthrough */
	case Opt_sec_krb5:
		vol->sectype = Kerberos;
		break;
	case Opt_sec_ntlmsspi:
		vol->sign = true;
		/* Fallthrough */
	case Opt_sec_ntlmssp:
		vol->sectype = RawNTLMSSP;
		break;
	case Opt_sec_ntlmi:
		vol->sign = true;
		/* Fallthrough */
	case Opt_ntlm:
		vol->sectype = NTLM;
		break;
	case Opt_sec_ntlmv2i:
		vol->sign = true;
		/* Fallthrough */
	case Opt_sec_ntlmv2:
		vol->sectype = NTLMv2;
		break;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	case Opt_sec_lanman:
		vol->sectype = LANMAN;
		break;
#endif
	case Opt_sec_none:
		vol->nullauth = 1;
		break;
	default:
		cifs_dbg(VFS, "bad security option: %s\n", value);
		return 1;
	}

	return 0;
}

static int
cifs_parse_cache_flavor(char *value, struct smb_vol *vol)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_cacheflavor_tokens, args)) {
	case Opt_cache_loose:
		vol->direct_io = false;
		vol->strict_io = false;
		break;
	case Opt_cache_strict:
		vol->direct_io = false;
		vol->strict_io = true;
		break;
	case Opt_cache_none:
		vol->direct_io = true;
		vol->strict_io = false;
		break;
	default:
		cifs_dbg(VFS, "bad cache= option: %s\n", value);
		return 1;
	}
	return 0;
}

static int
cifs_parse_smb_version(char *value, struct smb_vol *vol)
{
	substring_t args[MAX_OPT_ARGS];

	switch (match_token(value, cifs_smb_version_tokens, args)) {
	case Smb_1:
		vol->ops = &smb1_operations;
		vol->vals = &smb1_values;
		break;
#ifdef CONFIG_CIFS_SMB2
	case Smb_20:
		vol->ops = &smb20_operations;
		vol->vals = &smb20_values;
		break;
	case Smb_21:
		vol->ops = &smb21_operations;
		vol->vals = &smb21_values;
		break;
	case Smb_30:
		vol->ops = &smb30_operations;
		vol->vals = &smb30_values;
		break;
	case Smb_302:
		vol->ops = &smb30_operations; /* currently identical with 3.0 */
		vol->vals = &smb302_values;
		break;
#ifdef CONFIG_CIFS_SMB311
	case Smb_311:
		vol->ops = &smb311_operations;
		vol->vals = &smb311_values;
		break;
#endif /* SMB311 */
#endif
	default:
		cifs_dbg(VFS, "Unknown vers= option specified: %s\n", value);
		return 1;
	}
	return 0;
}

/*
 * Parse a devname into substrings and populate the vol->UNC and vol->prepath
 * fields with the result. Returns 0 on success and an error otherwise.
 */
static int
cifs_parse_devname(const char *devname, struct smb_vol *vol)
{
	char *pos;
	const char *delims = "/\\";
	size_t len;

	if (unlikely(!devname || !*devname)) {
		cifs_dbg(VFS, "Device name not specified.\n");
		return -EINVAL;
	}

	/* make sure we have a valid UNC double delimiter prefix */
	len = strspn(devname, delims);
	if (len != 2)
		return -EINVAL;

	/* find delimiter between host and sharename */
	pos = strpbrk(devname + 2, delims);
	if (!pos)
		return -EINVAL;

	/* skip past delimiter */
	++pos;

	/* now go until next delimiter or end of string */
	len = strcspn(pos, delims);

	/* move "pos" up to delimiter or NULL */
	pos += len;
	vol->UNC = kstrndup(devname, pos - devname, GFP_KERNEL);
	if (!vol->UNC)
		return -ENOMEM;

	convert_delimiter(vol->UNC, '\\');

	/* If pos is NULL, or is a bogus trailing delimiter then no prepath */
	if (!*pos++ || !*pos)
		return 0;

	vol->prepath = kstrdup(pos, GFP_KERNEL);
	if (!vol->prepath)
		return -ENOMEM;

	return 0;
}

static int
cifs_parse_mount_options(const char *mountdata, const char *devname,
			 struct smb_vol *vol)
{
	char *data, *end;
	char *mountdata_copy = NULL, *options;
	unsigned int  temp_len, i, j;
	char separator[2];
	short int override_uid = -1;
	short int override_gid = -1;
	bool uid_specified = false;
	bool gid_specified = false;
	bool sloppy = false;
	char *invalid = NULL;
	char *nodename = utsname()->nodename;
	char *string = NULL;
	char *tmp_end, *value;
	char delim;
	bool got_ip = false;
	unsigned short port = 0;
	struct sockaddr *dstaddr = (struct sockaddr *)&vol->dstaddr;

	separator[0] = ',';
	separator[1] = 0;
	delim = separator[0];

	/* ensure we always start with zeroed-out smb_vol */
	memset(vol, 0, sizeof(*vol));

	/*
	 * does not have to be perfect mapping since field is
	 * informational, only used for servers that do not support
	 * port 445 and it can be overridden at mount time
	 */
	memset(vol->source_rfc1001_name, 0x20, RFC1001_NAME_LEN);
	for (i = 0; i < strnlen(nodename, RFC1001_NAME_LEN); i++)
		vol->source_rfc1001_name[i] = toupper(nodename[i]);

	vol->source_rfc1001_name[RFC1001_NAME_LEN] = 0;
	/* null target name indicates to use *SMBSERVR default called name
	   if we end up sending RFC1001 session initialize */
	vol->target_rfc1001_name[0] = 0;
	vol->cred_uid = current_uid();
	vol->linux_uid = current_uid();
	vol->linux_gid = current_gid();

	/*
	 * default to SFM style remapping of seven reserved characters
	 * unless user overrides it or we negotiate CIFS POSIX where
	 * it is unnecessary.  Can not simultaneously use more than one mapping
	 * since then readdir could list files that open could not open
	 */
	vol->remap = true;

	/* default to only allowing write access to owner of the mount */
	vol->dir_mode = vol->file_mode = S_IRUGO | S_IXUGO | S_IWUSR;

	/* vol->retry default is 0 (i.e. "soft" limited retry not hard retry) */
	/* default is always to request posix paths. */
	vol->posix_paths = 1;
	/* default to using server inode numbers where available */
	vol->server_ino = 1;

	/* default is to use strict cifs caching semantics */
	vol->strict_io = true;

	vol->actimeo = CIFS_DEF_ACTIMEO;

	/* FIXME: add autonegotiation -- for now, SMB1 is default */
	vol->ops = &smb1_operations;
	vol->vals = &smb1_values;

	if (!mountdata)
		goto cifs_parse_mount_err;

	mountdata_copy = kstrndup(mountdata, PAGE_SIZE, GFP_KERNEL);
	if (!mountdata_copy)
		goto cifs_parse_mount_err;

	options = mountdata_copy;
	end = options + strlen(options);

	if (strncmp(options, "sep=", 4) == 0) {
		if (options[4] != 0) {
			separator[0] = options[4];
			options += 5;
		} else {
			cFYI(1, ("Null separator not allowed"));
		}
	}

	while ((data = strsep(&options, separator)) != NULL) {
		if (!*data)
			continue;
		if ((value = strchr(data, '=')) != NULL)
			*value++ = '\0';

		/* Have to parse this before we parse for "user" */
		if (strnicmp(data, "user_xattr", 10) == 0) {
			vol->no_xattr = 0;
		} else if (strnicmp(data, "nouser_xattr", 12) == 0) {
			vol->no_xattr = 1;
		} else if (strnicmp(data, "user", 4) == 0) {
			if (!value) {
				printk(KERN_WARNING
				       "CIFS: invalid or missing username\n");
				return 1;	/* needs_arg; */
			} else if (!*value) {
				/* null user, ie anonymous, authentication */
				vol->nullauth = 1;
			}
			if (strnlen(value, 200) < 200) {
				vol->username = value;
			} else {
				printk(KERN_WARNING "CIFS: username too long\n");
				return 1;
			}
		} else if (strnicmp(data, "pass", 4) == 0) {
			if (!value) {
				vol->password = NULL;
				continue;
			} else if (value[0] == 0) {
				/* check if string begins with double comma
				   since that would mean the password really
				   does start with a comma, and would not
				   indicate an empty string */
				if (value[1] != separator[0]) {
					vol->password = NULL;
					continue;
				}
			}
			temp_len = strlen(value);
			/* removed password length check, NTLM passwords
				can be arbitrarily long */

			/* if comma in password, the string will be
			prematurely null terminated.  Commas in password are
			specified across the cifs mount interface by a double
			comma ie ,, and a comma used as in other cases ie ','
			as a parameter delimiter/separator is single and due
			to the strsep above is temporarily zeroed. */

			/* NB: password legally can have multiple commas and
			the only illegal character in a password is null */

			if ((value[temp_len] == 0) &&
			    (value[temp_len+1] == separator[0])) {
				/* reinsert comma */
				value[temp_len] = separator[0];
				temp_len += 2;  /* move after second comma */
				while (value[temp_len] != 0)  {
					if (value[temp_len] == separator[0]) {
						if (value[temp_len+1] ==
						     separator[0]) {
						/* skip second comma */
							temp_len++;
						} else {
						/* single comma indicating start
							 of next parm */
							break;
						}
					}
					temp_len++;
				}
				if (value[temp_len] == 0) {
					options = NULL;
				} else {
					value[temp_len] = 0;
					/* point option to start of next parm */
					options = value + temp_len + 1;
				}
				/* go from value to value + temp_len condensing
				double commas to singles. Note that this ends up
				allocating a few bytes too many, which is ok */
				vol->password = kzalloc(temp_len, GFP_KERNEL);
				if (vol->password == NULL) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for password\n");
					return 1;
				}
				for (i = 0, j = 0; i < temp_len; i++, j++) {
					vol->password[j] = value[i];
					if (value[i] == separator[0]
						&& value[i+1] == separator[0]) {
						/* skip second comma */
						i++;
					}
				}
				vol->password[j] = 0;
			} else {
				vol->password = kzalloc(temp_len+1, GFP_KERNEL);
				if (vol->password == NULL) {
					printk(KERN_WARNING "CIFS: no memory "
							    "for password\n");
					return 1;
				}
				strcpy(vol->password, value);
			}
		} else if (strnicmp(data, "ip", 2) == 0) {
			if (!value || !*value) {
				vol->UNCip = NULL;
			} else if (strnlen(value, 35) < 35) {
				vol->UNCip = value;
			} else {
				printk(KERN_WARNING "CIFS: ip address "
						    "too long\n");
				return 1;
			}
		} else if (strnicmp(data, "sec", 3) == 0) {
			if (!value || !*value) {
				cERROR(1, ("no security value specified"));
				continue;
			} else if (strnicmp(value, "krb5i", 5) == 0) {
				vol->secFlg |= CIFSSEC_MAY_KRB5 |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "krb5p", 5) == 0) {
				/* vol->secFlg |= CIFSSEC_MUST_SEAL |
					CIFSSEC_MAY_KRB5; */
				cERROR(1, ("Krb5 cifs privacy not supported"));
				return 1;
			} else if (strnicmp(value, "krb5", 4) == 0) {
				vol->secFlg |= CIFSSEC_MAY_KRB5;
			} else if (strnicmp(value, "ntlmv2i", 7) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLMV2 |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "ntlmv2", 6) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLMV2;
			} else if (strnicmp(value, "ntlmi", 5) == 0) {
				vol->secFlg |= CIFSSEC_MAY_NTLM |
					CIFSSEC_MUST_SIGN;
			} else if (strnicmp(value, "ntlm", 4) == 0) {
				/* ntlm is default so can be turned off too */
				vol->secFlg |= CIFSSEC_MAY_NTLM;
			} else if (strnicmp(value, "nontlm", 6) == 0) {
				/* BB is there a better way to do this? */
				vol->secFlg |= CIFSSEC_MAY_NTLMV2;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
			} else if (strnicmp(value, "lanman", 6) == 0) {
				vol->secFlg |= CIFSSEC_MAY_LANMAN;
#endif
			} else if (strnicmp(value, "none", 4) == 0) {
				vol->nullauth = 1;
			} else {
				cERROR(1, ("bad security option: %s", value));
				return 1;
			}
		} else if ((strnicmp(data, "unc", 3) == 0)
			   || (strnicmp(data, "target", 6) == 0)
			   || (strnicmp(data, "path", 4) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid path to "
						    "network resource\n");
				return 1;	/* needs_arg; */
			}
			if ((temp_len = strnlen(value, 300)) < 300) {
				vol->UNC = kmalloc(temp_len+1, GFP_KERNEL);
				if (vol->UNC == NULL)
					return 1;
				strcpy(vol->UNC, value);
				if (strncmp(vol->UNC, "//", 2) == 0) {
					vol->UNC[0] = '\\';
					vol->UNC[1] = '\\';
				} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
					printk(KERN_WARNING
					       "CIFS: UNC Path does not begin "
					       "with // or \\\\ \n");
					return 1;
				}
			} else {
				printk(KERN_WARNING "CIFS: UNC name too long\n");
				return 1;
			}
		} else if ((strnicmp(data, "domain", 3) == 0)
			   || (strnicmp(data, "workgroup", 5) == 0)) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid domain name\n");
				return 1;	/* needs_arg; */
			}
			/* BB are there cases in which a comma can be valid in
			a domain name and need special handling? */
			if (strnlen(value, 256) < 256) {
				vol->domainname = value;
				cFYI(1, ("Domain name set"));
			} else {
				printk(KERN_WARNING "CIFS: domain name too "
						    "long\n");
				return 1;
			}
		} else if (strnicmp(data, "prefixpath", 10) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING
					"CIFS: invalid path prefix\n");
				return 1;       /* needs_argument */
			}
			if ((temp_len = strnlen(value, 1024)) < 1024) {
				if (value[0] != '/')
					temp_len++;  /* missing leading slash */
				vol->prepath = kmalloc(temp_len+1, GFP_KERNEL);
				if (vol->prepath == NULL)
					return 1;
				if (value[0] != '/') {
					vol->prepath[0] = '/';
					strcpy(vol->prepath+1, value);
				} else
					strcpy(vol->prepath, value);
				cFYI(1, ("prefix path %s", vol->prepath));
			} else {
				printk(KERN_WARNING "CIFS: prefix too long\n");
				return 1;
			}
		} else if (strnicmp(data, "iocharset", 9) == 0) {
			if (!value || !*value) {
				printk(KERN_WARNING "CIFS: invalid iocharset "
						    "specified\n");
				return 1;	/* needs_arg; */
			}
			if (strnlen(value, 65) < 65) {
				if (strnicmp(value, "default", 7))
					vol->iocharset = value;
				/* if iocharset not set then load_nls_default
				   is used by caller */
				cFYI(1, ("iocharset set to %s", value));
			} else {
				printk(KERN_WARNING "CIFS: iocharset name "
						    "too long.\n");
				return 1;
			}
		} else if (strnicmp(data, "uid", 3) == 0) {
			if (value && *value) {
				vol->linux_uid =
					simple_strtoul(value, &value, 0);
				vol->override_uid = 1;
			}
		} else if (strnicmp(data, "gid", 3) == 0) {
			if (value && *value) {
				vol->linux_gid =
					simple_strtoul(value, &value, 0);
				vol->override_gid = 1;
			}
		} else if (strnicmp(data, "file_mode", 4) == 0) {
			if (value && *value) {
				vol->file_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "dir_mode", 4) == 0) {
			if (value && *value) {
				vol->dir_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "dirmode", 4) == 0) {
			if (value && *value) {
				vol->dir_mode =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "port", 4) == 0) {
			if (value && *value) {
				vol->port =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "rsize", 5) == 0) {
			if (value && *value) {
				vol->rsize =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "wsize", 5) == 0) {
			if (value && *value) {
				vol->wsize =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "sockopt", 5) == 0) {
			if (value && *value) {
				vol->sockopt =
					simple_strtoul(value, &value, 0);
			}
		} else if (strnicmp(data, "netbiosname", 4) == 0) {
			if (!value || !*value || (*value == ' ')) {
				cFYI(1, ("invalid (empty) netbiosname"));
			} else {
				memset(vol->source_rfc1001_name, 0x20, 15);
				for (i = 0; i < 15; i++) {
				/* BB are there cases in which a comma can be
				valid in this workstation netbios name (and need
				special handling)? */

				/* We do not uppercase netbiosname for user */
					if (value[i] == 0)
						break;
					else
						vol->source_rfc1001_name[i] =
								value[i];
				}
				/* The string has 16th byte zero still from
				set at top of the function  */
				if ((i == 15) && (value[i] != 0))
					printk(KERN_WARNING "CIFS: netbiosname"
						" longer than 15 truncated.\n");
			}
		} else if (strnicmp(data, "servern", 7) == 0) {
			/* servernetbiosname specified override *SMBSERVER */
			if (!value || !*value || (*value == ' ')) {
				cFYI(1, ("empty server netbiosname specified"));
			} else {
				/* last byte, type, is 0x20 for servr type */
				memset(vol->target_rfc1001_name, 0x20, 16);

				for (i = 0; i < 15; i++) {
				/* BB are there cases in which a comma can be
				   valid in this workstation netbios name
				   (and need special handling)? */

				/* user or mount helper must uppercase
				   the netbiosname */
					if (value[i] == 0)
						break;
					else
						vol->target_rfc1001_name[i] =
								value[i];
				}
				/* The string has 16th byte zero still from
				   set at top of the function  */
				if ((i == 15) && (value[i] != 0))
					printk(KERN_WARNING "CIFS: server net"
					"biosname longer than 15 truncated.\n");
			}
		} else if (strnicmp(data, "credentials", 4) == 0) {
			/* ignore */
		} else if (strnicmp(data, "version", 3) == 0) {
			/* ignore */
		} else if (strnicmp(data, "guest", 5) == 0) {
			/* ignore */
		} else if (strnicmp(data, "rw", 2) == 0) {
			vol->rw = true;
		} else if ((strnicmp(data, "suid", 4) == 0) ||
				   (strnicmp(data, "nosuid", 6) == 0) ||
				   (strnicmp(data, "exec", 4) == 0) ||
				   (strnicmp(data, "noexec", 6) == 0) ||
				   (strnicmp(data, "nodev", 5) == 0) ||
				   (strnicmp(data, "noauto", 6) == 0) ||
				   (strnicmp(data, "dev", 3) == 0)) {
			/*  The mount tool or mount.cifs helper (if present)
			    uses these opts to set flags, and the flags are read
			    by the kernel vfs layer before we get here (ie
			    before read super) so there is no point trying to
			    parse these options again and set anything and it
			    is ok to just ignore them */
			continue;
		} else if (strnicmp(data, "ro", 2) == 0) {
			vol->rw = false;
		} else if (strnicmp(data, "hard", 4) == 0) {
			vol->retry = 1;
		} else if (strnicmp(data, "soft", 4) == 0) {
			vol->retry = 0;
		} else if (strnicmp(data, "perm", 4) == 0) {
			vol->noperm = 0;
		} else if (strnicmp(data, "noperm", 6) == 0) {
			vol->noperm = 1;
		} else if (strnicmp(data, "mapchars", 8) == 0) {
			vol->remap = 1;
		} else if (strnicmp(data, "nomapchars", 10) == 0) {
			vol->remap = 0;
		} else if (strnicmp(data, "sfu", 3) == 0) {
			vol->sfu_emul = 1;
		} else if (strnicmp(data, "nosfu", 5) == 0) {
			vol->sfu_emul = 0;
		} else if (strnicmp(data, "posixpaths", 10) == 0) {
			vol->posix_paths = 1;
		} else if (strnicmp(data, "noposixpaths", 12) == 0) {
			vol->posix_paths = 0;
		} else if (strnicmp(data, "nounix", 6) == 0) {
			vol->no_linux_ext = 1;
		} else if (strnicmp(data, "nolinux", 7) == 0) {
			vol->no_linux_ext = 1;
		} else if ((strnicmp(data, "nocase", 6) == 0) ||
			   (strnicmp(data, "ignorecase", 10)  == 0)) {
			vol->nocase = 1;
		} else if (strnicmp(data, "brl", 3) == 0) {
			vol->nobrl =  0;
		} else if ((strnicmp(data, "nobrl", 5) == 0) ||
			   (strnicmp(data, "nolock", 6) == 0)) {
			vol->nobrl =  1;
			/* turn off mandatory locking in mode
			if remote locking is turned off since the
			local vfs will do advisory */
			if (vol->file_mode ==
				(S_IALLUGO & ~(S_ISUID | S_IXGRP)))
				vol->file_mode = S_IALLUGO;
		} else if (strnicmp(data, "setuids", 7) == 0) {
			vol->setuids = 1;
		} else if (strnicmp(data, "nosetuids", 9) == 0) {
			vol->setuids = 0;
		} else if (strnicmp(data, "dynperm", 7) == 0) {
			vol->dynperm = true;
		} else if (strnicmp(data, "nodynperm", 9) == 0) {
			vol->dynperm = false;
		} else if (strnicmp(data, "nohard", 6) == 0) {
			vol->retry = 0;
		} else if (strnicmp(data, "nosoft", 6) == 0) {
			vol->retry = 1;
		} else if (strnicmp(data, "nointr", 6) == 0) {
			vol->intr = 0;
		} else if (strnicmp(data, "intr", 4) == 0) {
			vol->intr = 1;
		} else if (strnicmp(data, "serverino", 7) == 0) {
			vol->server_ino = 1;
		} else if (strnicmp(data, "noserverino", 9) == 0) {
			vol->server_ino = 0;
		} else if (strnicmp(data, "cifsacl", 7) == 0) {
			vol->cifs_acl = 1;
		} else if (strnicmp(data, "nocifsacl", 9) == 0) {
			vol->cifs_acl = 0;
		} else if (strnicmp(data, "acl", 3) == 0) {
			vol->no_psx_acl = 0;
		} else if (strnicmp(data, "noacl", 5) == 0) {
			vol->no_psx_acl = 1;
		} else if (strnicmp(data, "sign", 4) == 0) {
			vol->secFlg |= CIFSSEC_MUST_SIGN;
		} else if (strnicmp(data, "seal", 4) == 0) {
			/* we do not do the following in secFlags because seal
			   is a per tree connection (mount) not a per socket
			   or per-smb connection option in the protocol */
			/* vol->secFlg |= CIFSSEC_MUST_SEAL; */
			vol->seal = 1;
		} else if (strnicmp(data, "direct", 6) == 0) {
			vol->direct_io = 1;
		} else if (strnicmp(data, "forcedirectio", 13) == 0) {
			vol->direct_io = 1;
		} else if (strnicmp(data, "in6_addr", 8) == 0) {
			if (!value || !*value) {
				vol->in6_addr = NULL;
			} else if (strnlen(value, 49) == 48) {
				vol->in6_addr = value;
			} else {
				printk(KERN_WARNING "CIFS: ip v6 address not "
						    "48 characters long\n");
				return 1;
			}
		} else if (strnicmp(data, "noac", 4) == 0) {
			printk(KERN_WARNING "CIFS: Mount option noac not "
				"supported. Instead set "
				"/proc/fs/cifs/LookupCacheEnabled to 0\n");
		} else
			printk(KERN_WARNING "CIFS: Unknown mount option %s\n",
						data);
	}
	if (vol->UNC == NULL) {
		if (devname == NULL) {
			printk(KERN_WARNING "CIFS: Missing UNC name for mount "
						"target\n");
			return 1;
		}
		if ((temp_len = strnlen(devname, 300)) < 300) {
			vol->UNC = kmalloc(temp_len+1, GFP_KERNEL);
			if (vol->UNC == NULL)
				return 1;
			strcpy(vol->UNC, devname);
			if (strncmp(vol->UNC, "//", 2) == 0) {
				vol->UNC[0] = '\\';
				vol->UNC[1] = '\\';
			} else if (strncmp(vol->UNC, "\\\\", 2) != 0) {
				printk(KERN_WARNING "CIFS: UNC Path does not "
						    "begin with // or \\\\ \n");
				return 1;
			}
			value = strpbrk(vol->UNC+2, "/\\");
			if (value)
				*value = '\\';
		} else {
			printk(KERN_WARNING "CIFS: UNC name too long\n");
			return 1;
		}
	}
	if (vol->UNCip == NULL)
		vol->UNCip = &vol->UNC[2];

	return 0;
}

static struct cifsSesInfo *
cifs_find_tcp_session(struct in_addr *target_ip_addr,
		      struct in6_addr *target_ip6_addr,
		      char *userName, struct TCP_Server_Info **psrvTcp)
{
	struct list_head *tmp;
	struct cifsSesInfo *ses;

	*psrvTcp = NULL;

	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		if (!ses->server)
			continue;

		if (target_ip_addr &&
		    ses->server->addr.sockAddr.sin_addr.s_addr != target_ip_addr->s_addr)
				continue;
		else if (target_ip6_addr &&
			 memcmp(&ses->server->addr.sockAddr6.sin6_addr,
				target_ip6_addr, sizeof(*target_ip6_addr)))
				continue;
		/* BB lock server and tcp session; increment use count here?? */

		/* found a match on the TCP session */
		*psrvTcp = ses->server;

		/* BB check if reconnection needed */
		if (strncmp(ses->userName, userName, MAX_USERNAME_SIZE) == 0) {
			read_unlock(&GlobalSMBSeslock);
			/* Found exact match on both TCP and
			   SMB sessions */
			return ses;
		}
		/* else tcp and smb sessions need reconnection */
	}
	read_unlock(&GlobalSMBSeslock);

	return NULL;
}

static struct cifsTconInfo *
find_unc(__be32 new_target_ip_addr, char *uncName, char *userName)
{
	struct list_head *tmp;
	struct cifsTconInfo *tcon;
	__be32 old_ip;

	read_lock(&GlobalSMBSeslock);

	list_for_each(tmp, &GlobalTreeConnectionList) {
		cFYI(1, ("Next tcon"));
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		if (!tcon->ses || !tcon->ses->server)
			continue;

		old_ip = tcon->ses->server->addr.sockAddr.sin_addr.s_addr;
		cFYI(1, ("old ip addr: %x == new ip %x ?",
			old_ip, new_target_ip_addr));

		if (old_ip != new_target_ip_addr)
			continue;

		/* BB lock tcon, server, tcp session and increment use count? */
		/* found a match on the TCP session */
		/* BB check if reconnection needed */
		cFYI(1, ("IP match, old UNC: %s new: %s",
			tcon->treeName, uncName));

		if (strncmp(tcon->treeName, uncName, MAX_TREE_SIZE))
			continue;

		cFYI(1, ("and old usr: %s new: %s",
			tcon->treeName, uncName));

		if (strncmp(tcon->ses->userName, userName, MAX_USERNAME_SIZE))
			continue;

		/* matched smb session (user name) */
		read_unlock(&GlobalSMBSeslock);
		return tcon;
	}

	read_unlock(&GlobalSMBSeslock);
	return NULL;
}

int
get_dfs_path(int xid, struct cifsSesInfo *pSesInfo, const char *old_path,
	     const struct nls_table *nls_codepage, unsigned int *pnum_referrals,
	     struct dfs_info3_param **preferrals, int remap)
			cifs_dbg(FYI, "Null separator not allowed\n");
		}
	}
	vol->backupuid_specified = false; /* no backup intent for a user */
	vol->backupgid_specified = false; /* no backup intent for a group */

	switch (cifs_parse_devname(devname, vol)) {
	case 0:
		break;
	case -ENOMEM:
		cifs_dbg(VFS, "Unable to allocate memory for devname.\n");
		goto cifs_parse_mount_err;
	case -EINVAL:
		cifs_dbg(VFS, "Malformed UNC in devname.\n");
		goto cifs_parse_mount_err;
	default:
		cifs_dbg(VFS, "Unknown error parsing devname.\n");
		goto cifs_parse_mount_err;
	}

	while ((data = strsep(&options, separator)) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		unsigned long option;
		int token;

		if (!*data)
			continue;

		token = match_token(data, cifs_mount_option_tokens, args);

		switch (token) {

		/* Ingnore the following */
		case Opt_ignore:
			break;

		/* Boolean values */
		case Opt_user_xattr:
			vol->no_xattr = 0;
			break;
		case Opt_nouser_xattr:
			vol->no_xattr = 1;
			break;
		case Opt_forceuid:
			override_uid = 1;
			break;
		case Opt_noforceuid:
			override_uid = 0;
			break;
		case Opt_forcegid:
			override_gid = 1;
			break;
		case Opt_noforcegid:
			override_gid = 0;
			break;
		case Opt_noblocksend:
			vol->noblocksnd = 1;
			break;
		case Opt_noautotune:
			vol->noautotune = 1;
			break;
		case Opt_hard:
			vol->retry = 1;
			break;
		case Opt_soft:
			vol->retry = 0;
			break;
		case Opt_perm:
			vol->noperm = 0;
			break;
		case Opt_noperm:
			vol->noperm = 1;
			break;
		case Opt_mapchars:
			vol->sfu_remap = true;
			vol->remap = false; /* disable SFM mapping */
			break;
		case Opt_nomapchars:
			vol->sfu_remap = false;
			break;
		case Opt_mapposix:
			vol->remap = true;
			vol->sfu_remap = false; /* disable SFU mapping */
			break;
		case Opt_nomapposix:
			vol->remap = false;
			break;
		case Opt_sfu:
			vol->sfu_emul = 1;
			break;
		case Opt_nosfu:
			vol->sfu_emul = 0;
			break;
		case Opt_nodfs:
			vol->nodfs = 1;
			break;
		case Opt_posixpaths:
			vol->posix_paths = 1;
			break;
		case Opt_noposixpaths:
			vol->posix_paths = 0;
			break;
		case Opt_nounix:
			vol->no_linux_ext = 1;
			break;
		case Opt_nocase:
			vol->nocase = 1;
			break;
		case Opt_brl:
			vol->nobrl =  0;
			break;
		case Opt_nobrl:
			vol->nobrl =  1;
			/*
			 * turn off mandatory locking in mode
			 * if remote locking is turned off since the
			 * local vfs will do advisory
			 */
			if (vol->file_mode ==
				(S_IALLUGO & ~(S_ISUID | S_IXGRP)))
				vol->file_mode = S_IALLUGO;
			break;
		case Opt_forcemandatorylock:
			vol->mand_lock = 1;
			break;
		case Opt_setuids:
			vol->setuids = 1;
			break;
		case Opt_nosetuids:
			vol->setuids = 0;
			break;
		case Opt_dynperm:
			vol->dynperm = true;
			break;
		case Opt_nodynperm:
			vol->dynperm = false;
			break;
		case Opt_nohard:
			vol->retry = 0;
			break;
		case Opt_nosoft:
			vol->retry = 1;
			break;
		case Opt_nointr:
			vol->intr = 0;
			break;
		case Opt_intr:
			vol->intr = 1;
			break;
		case Opt_nostrictsync:
			vol->nostrictsync = 1;
			break;
		case Opt_strictsync:
			vol->nostrictsync = 0;
			break;
		case Opt_serverino:
			vol->server_ino = 1;
			break;
		case Opt_noserverino:
			vol->server_ino = 0;
			break;
		case Opt_rwpidforward:
			vol->rwpidforward = 1;
			break;
		case Opt_cifsacl:
			vol->cifs_acl = 1;
			break;
		case Opt_nocifsacl:
			vol->cifs_acl = 0;
			break;
		case Opt_acl:
			vol->no_psx_acl = 0;
			break;
		case Opt_noacl:
			vol->no_psx_acl = 1;
			break;
		case Opt_locallease:
			vol->local_lease = 1;
			break;
		case Opt_sign:
			vol->sign = true;
			break;
		case Opt_seal:
			/* we do not do the following in secFlags because seal
			 * is a per tree connection (mount) not a per socket
			 * or per-smb connection option in the protocol
			 * vol->secFlg |= CIFSSEC_MUST_SEAL;
			 */
			vol->seal = 1;
			break;
		case Opt_noac:
			pr_warn("CIFS: Mount option noac not supported. Instead set /proc/fs/cifs/LookupCacheEnabled to 0\n");
			break;
		case Opt_fsc:
#ifndef CONFIG_CIFS_FSCACHE
			cifs_dbg(VFS, "FS-Cache support needs CONFIG_CIFS_FSCACHE kernel config option set\n");
			goto cifs_parse_mount_err;
#endif
			vol->fsc = true;
			break;
		case Opt_mfsymlinks:
			vol->mfsymlinks = true;
			break;
		case Opt_multiuser:
			vol->multiuser = true;
			break;
		case Opt_sloppy:
			sloppy = true;
			break;
		case Opt_nosharesock:
			vol->nosharesock = true;
			break;
		case Opt_nopersistent:
			vol->nopersistent = true;
			if (vol->persistent) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_persistent:
			vol->persistent = true;
			if ((vol->nopersistent) || (vol->resilient)) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_resilient:
			vol->resilient = true;
			if (vol->persistent) {
				cifs_dbg(VFS,
				  "persistenthandles mount options conflict\n");
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_noresilient:
			vol->resilient = false; /* already the default */
			break;

		/* Numeric Values */
		case Opt_backupuid:
			if (get_option_uid(args, &vol->backupuid)) {
				cifs_dbg(VFS, "%s: Invalid backupuid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->backupuid_specified = true;
			break;
		case Opt_backupgid:
			if (get_option_gid(args, &vol->backupgid)) {
				cifs_dbg(VFS, "%s: Invalid backupgid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->backupgid_specified = true;
			break;
		case Opt_uid:
			if (get_option_uid(args, &vol->linux_uid)) {
				cifs_dbg(VFS, "%s: Invalid uid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			uid_specified = true;
			break;
		case Opt_cruid:
			if (get_option_uid(args, &vol->cred_uid)) {
				cifs_dbg(VFS, "%s: Invalid cruid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_gid:
			if (get_option_gid(args, &vol->linux_gid)) {
				cifs_dbg(VFS, "%s: Invalid gid value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			gid_specified = true;
			break;
		case Opt_file_mode:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid file_mode value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->file_mode = option;
			break;
		case Opt_dirmode:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid dir_mode value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->dir_mode = option;
			break;
		case Opt_port:
			if (get_option_ul(args, &option) ||
			    option > USHRT_MAX) {
				cifs_dbg(VFS, "%s: Invalid port value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			port = (unsigned short)option;
			break;
		case Opt_rsize:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid rsize value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->rsize = option;
			break;
		case Opt_wsize:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid wsize value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->wsize = option;
			break;
		case Opt_actimeo:
			if (get_option_ul(args, &option)) {
				cifs_dbg(VFS, "%s: Invalid actimeo value\n",
					 __func__);
				goto cifs_parse_mount_err;
			}
			vol->actimeo = HZ * option;
			if (vol->actimeo > CIFS_MAX_ACTIMEO) {
				cifs_dbg(VFS, "attribute cache timeout too large\n");
				goto cifs_parse_mount_err;
			}
			break;

		/* String Arguments */

		case Opt_blank_user:
			/* null user, ie. anonymous authentication */
			vol->nullauth = 1;
			vol->username = NULL;
			break;
		case Opt_user:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strnlen(string, CIFS_MAX_USERNAME_LEN) >
							CIFS_MAX_USERNAME_LEN) {
				pr_warn("CIFS: username too long\n");
				goto cifs_parse_mount_err;
			}

			kfree(vol->username);
			vol->username = kstrdup(string, GFP_KERNEL);
			if (!vol->username)
				goto cifs_parse_mount_err;
			break;
		case Opt_blank_pass:
			/* passwords have to be handled differently
			 * to allow the character used for deliminator
			 * to be passed within them
			 */

			/*
			 * Check if this is a case where the  password
			 * starts with a delimiter
			 */
			tmp_end = strchr(data, '=');
			tmp_end++;
			if (!(tmp_end < end && tmp_end[1] == delim)) {
				/* No it is not. Set the password to NULL */
				kzfree(vol->password);
				vol->password = NULL;
				break;
			}
			/* Yes it is. Drop down to Opt_pass below.*/
		case Opt_pass:
			/* Obtain the value string */
			value = strchr(data, '=');
			value++;

			/* Set tmp_end to end of the string */
			tmp_end = (char *) value + strlen(value);

			/* Check if following character is the deliminator
			 * If yes, we have encountered a double deliminator
			 * reset the NULL character to the deliminator
			 */
			if (tmp_end < end && tmp_end[1] == delim) {
				tmp_end[0] = delim;

				/* Keep iterating until we get to a single
				 * deliminator OR the end
				 */
				while ((tmp_end = strchr(tmp_end, delim))
					!= NULL && (tmp_end[1] == delim)) {
						tmp_end = (char *) &tmp_end[2];
				}

				/* Reset var options to point to next element */
				if (tmp_end) {
					tmp_end[0] = '\0';
					options = (char *) &tmp_end[1];
				} else
					/* Reached the end of the mount option
					 * string */
					options = end;
			}

			kzfree(vol->password);
			/* Now build new password string */
			temp_len = strlen(value);
			vol->password = kzalloc(temp_len+1, GFP_KERNEL);
			if (vol->password == NULL) {
				pr_warn("CIFS: no memory for password\n");
				goto cifs_parse_mount_err;
			}

			for (i = 0, j = 0; i < temp_len; i++, j++) {
				vol->password[j] = value[i];
				if ((value[i] == delim) &&
				     value[i+1] == delim)
					/* skip the second deliminator */
					i++;
			}
			vol->password[j] = '\0';
			break;
		case Opt_blank_ip:
			/* FIXME: should this be an error instead? */
			got_ip = false;
			break;
		case Opt_ip:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (!cifs_convert_address(dstaddr, string,
					strlen(string))) {
				pr_err("CIFS: bad ip= option (%s).\n", string);
				goto cifs_parse_mount_err;
			}
			got_ip = true;
			break;
		case Opt_domain:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strnlen(string, CIFS_MAX_DOMAINNAME_LEN)
					== CIFS_MAX_DOMAINNAME_LEN) {
				pr_warn("CIFS: domain name too long\n");
				goto cifs_parse_mount_err;
			}

			kfree(vol->domainname);
			vol->domainname = kstrdup(string, GFP_KERNEL);
			if (!vol->domainname) {
				pr_warn("CIFS: no memory for domainname\n");
				goto cifs_parse_mount_err;
			}
			cifs_dbg(FYI, "Domain name set\n");
			break;
		case Opt_srcaddr:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (!cifs_convert_address(
					(struct sockaddr *)&vol->srcaddr,
					string, strlen(string))) {
				pr_warn("CIFS: Could not parse srcaddr: %s\n",
					string);
				goto cifs_parse_mount_err;
			}
			break;
		case Opt_iocharset:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strnlen(string, 1024) >= 65) {
				pr_warn("CIFS: iocharset name too long.\n");
				goto cifs_parse_mount_err;
			}

			 if (strncasecmp(string, "default", 7) != 0) {
				kfree(vol->iocharset);
				vol->iocharset = kstrdup(string,
							 GFP_KERNEL);
				if (!vol->iocharset) {
					pr_warn("CIFS: no memory for charset\n");
					goto cifs_parse_mount_err;
				}
			}
			/* if iocharset not set then load_nls_default
			 * is used by caller
			 */
			 cifs_dbg(FYI, "iocharset set to %s\n", string);
			break;
		case Opt_netbiosname:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			memset(vol->source_rfc1001_name, 0x20,
				RFC1001_NAME_LEN);
			/*
			 * FIXME: are there cases in which a comma can
			 * be valid in workstation netbios name (and
			 * need special handling)?
			 */
			for (i = 0; i < RFC1001_NAME_LEN; i++) {
				/* don't ucase netbiosname for user */
				if (string[i] == 0)
					break;
				vol->source_rfc1001_name[i] = string[i];
			}
			/* The string has 16th byte zero still from
			 * set at top of the function
			 */
			if (i == RFC1001_NAME_LEN && string[i] != 0)
				pr_warn("CIFS: netbiosname longer than 15 truncated.\n");
			break;
		case Opt_servern:
			/* servernetbiosname specified override *SMBSERVER */
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			/* last byte, type, is 0x20 for servr type */
			memset(vol->target_rfc1001_name, 0x20,
				RFC1001_NAME_LEN_WITH_NULL);

			/* BB are there cases in which a comma can be
			   valid in this workstation netbios name
			   (and need special handling)? */

			/* user or mount helper must uppercase the
			   netbios name */
			for (i = 0; i < 15; i++) {
				if (string[i] == 0)
					break;
				vol->target_rfc1001_name[i] = string[i];
			}
			/* The string has 16th byte zero still from
			   set at top of the function  */
			if (i == RFC1001_NAME_LEN && string[i] != 0)
				pr_warn("CIFS: server netbiosname longer than 15 truncated.\n");
			break;
		case Opt_ver:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (strncasecmp(string, "1", 1) == 0) {
				/* This is the default */
				break;
			}
			/* For all other value, error */
			pr_warn("CIFS: Invalid version specified\n");
			goto cifs_parse_mount_err;
		case Opt_vers:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_smb_version(string, vol) != 0)
				goto cifs_parse_mount_err;
			break;
		case Opt_sec:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_security_flavors(string, vol) != 0)
				goto cifs_parse_mount_err;
			break;
		case Opt_cache:
			string = match_strdup(args);
			if (string == NULL)
				goto out_nomem;

			if (cifs_parse_cache_flavor(string, vol) != 0)
				goto cifs_parse_mount_err;
			break;
		default:
			/*
			 * An option we don't recognize. Save it off for later
			 * if we haven't already found one
			 */
			if (!invalid)
				invalid = data;
			break;
		}
		/* Free up any allocated string */
		kfree(string);
		string = NULL;
	}

	if (!sloppy && invalid) {
		pr_err("CIFS: Unknown mount option \"%s\"\n", invalid);
		goto cifs_parse_mount_err;
	}

#ifndef CONFIG_KEYS
	/* Muliuser mounts require CONFIG_KEYS support */
	if (vol->multiuser) {
		cifs_dbg(VFS, "Multiuser mounts require kernels with CONFIG_KEYS enabled\n");
		goto cifs_parse_mount_err;
	}
#endif
	if (!vol->UNC) {
		cifs_dbg(VFS, "CIFS mount error: No usable UNC path provided in device string!\n");
		goto cifs_parse_mount_err;
	}

	/* make sure UNC has a share name */
	if (!strchr(vol->UNC + 3, '\\')) {
		cifs_dbg(VFS, "Malformed UNC. Unable to find share name.\n");
		goto cifs_parse_mount_err;
	}

	if (!got_ip) {
		/* No ip= option specified? Try to get it from UNC */
		if (!cifs_convert_address(dstaddr, &vol->UNC[2],
						strlen(&vol->UNC[2]))) {
			pr_err("Unable to determine destination address.\n");
			goto cifs_parse_mount_err;
		}
	}

	/* set the port that we got earlier */
	cifs_set_port(dstaddr, port);

	if (uid_specified)
		vol->override_uid = override_uid;
	else if (override_uid == 1)
		pr_notice("CIFS: ignoring forceuid mount option specified with no uid= option.\n");

	if (gid_specified)
		vol->override_gid = override_gid;
	else if (override_gid == 1)
		pr_notice("CIFS: ignoring forcegid mount option specified with no gid= option.\n");

	kfree(mountdata_copy);
	return 0;

out_nomem:
	pr_warn("Could not allocate temporary buffer\n");
cifs_parse_mount_err:
	kfree(string);
	kfree(mountdata_copy);
	return 1;
}

/** Returns true if srcaddr isn't specified and rhs isn't
 * specified, or if srcaddr is specified and
 * matches the IP address of the rhs argument.
 */
static bool
srcip_matches(struct sockaddr *srcaddr, struct sockaddr *rhs)
{
	switch (srcaddr->sa_family) {
	case AF_UNSPEC:
		return (rhs->sa_family == AF_UNSPEC);
	case AF_INET: {
		struct sockaddr_in *saddr4 = (struct sockaddr_in *)srcaddr;
		struct sockaddr_in *vaddr4 = (struct sockaddr_in *)rhs;
		return (saddr4->sin_addr.s_addr == vaddr4->sin_addr.s_addr);
	}
	case AF_INET6: {
		struct sockaddr_in6 *saddr6 = (struct sockaddr_in6 *)srcaddr;
		struct sockaddr_in6 *vaddr6 = (struct sockaddr_in6 *)rhs;
		return ipv6_addr_equal(&saddr6->sin6_addr, &vaddr6->sin6_addr);
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}
}

/*
 * If no port is specified in addr structure, we try to match with 445 port
 * and if it fails - with 139 ports. It should be called only if address
 * families of server and addr are equal.
 */
static bool
match_port(struct TCP_Server_Info *server, struct sockaddr *addr)
{
	__be16 port, *sport;

	switch (addr->sa_family) {
	case AF_INET:
		sport = &((struct sockaddr_in *) &server->dstaddr)->sin_port;
		port = ((struct sockaddr_in *) addr)->sin_port;
		break;
	case AF_INET6:
		sport = &((struct sockaddr_in6 *) &server->dstaddr)->sin6_port;
		port = ((struct sockaddr_in6 *) addr)->sin6_port;
		break;
	default:
		WARN_ON(1);
		return false;
	}

	if (!port) {
		port = htons(CIFS_PORT);
		if (port == *sport)
			return true;

		port = htons(RFC1001_PORT);
	}

	return port == *sport;
}

static bool
match_address(struct TCP_Server_Info *server, struct sockaddr *addr,
	      struct sockaddr *srcaddr)
{
	switch (addr->sa_family) {
	case AF_INET: {
		struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
		struct sockaddr_in *srv_addr4 =
					(struct sockaddr_in *)&server->dstaddr;

		if (addr4->sin_addr.s_addr != srv_addr4->sin_addr.s_addr)
			return false;
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
		struct sockaddr_in6 *srv_addr6 =
					(struct sockaddr_in6 *)&server->dstaddr;

		if (!ipv6_addr_equal(&addr6->sin6_addr,
				     &srv_addr6->sin6_addr))
			return false;
		if (addr6->sin6_scope_id != srv_addr6->sin6_scope_id)
			return false;
		break;
	}
	default:
		WARN_ON(1);
		return false; /* don't expect to be here */
	}

	if (!srcip_matches(srcaddr, (struct sockaddr *)&server->srcaddr))
		return false;

	return true;
}

static bool
match_security(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	/*
	 * The select_sectype function should either return the vol->sectype
	 * that was specified, or "Unspecified" if that sectype was not
	 * compatible with the given NEGOTIATE request.
	 */
	if (select_sectype(server, vol->sectype) == Unspecified)
		return false;

	/*
	 * Now check if signing mode is acceptable. No need to check
	 * global_secflags at this point since if MUST_SIGN is set then
	 * the server->sign had better be too.
	 */
	if (vol->sign && !server->sign)
		return false;

	return true;
}

static int match_server(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	struct sockaddr *addr = (struct sockaddr *)&vol->dstaddr;

	if (vol->nosharesock)
		return 0;

	if ((server->vals != vol->vals) || (server->ops != vol->ops))
		return 0;

	if (!net_eq(cifs_net_ns(server), current->nsproxy->net_ns))
		return 0;

	if (!match_address(server, addr,
			   (struct sockaddr *)&vol->srcaddr))
		return 0;

	if (!match_port(server, addr))
		return 0;

	if (!match_security(server, vol))
		return 0;

	return 1;
}

static struct TCP_Server_Info *
cifs_find_tcp_session(struct smb_vol *vol)
{
	struct TCP_Server_Info *server;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(server, &cifs_tcp_ses_list, tcp_ses_list) {
		if (!match_server(server, vol))
			continue;

		++server->srv_count;
		spin_unlock(&cifs_tcp_ses_lock);
		cifs_dbg(FYI, "Existing tcp session with server found\n");
		return server;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

void
cifs_put_tcp_session(struct TCP_Server_Info *server, int from_reconnect)
{
	struct task_struct *task;

	spin_lock(&cifs_tcp_ses_lock);
	if (--server->srv_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	put_net(cifs_net_ns(server));

	list_del_init(&server->tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cancel_delayed_work_sync(&server->echo);

#ifdef CONFIG_CIFS_SMB2
	if (from_reconnect)
		/*
		 * Avoid deadlock here: reconnect work calls
		 * cifs_put_tcp_session() at its end. Need to be sure
		 * that reconnect work does nothing with server pointer after
		 * that step.
		 */
		cancel_delayed_work(&server->reconnect);
	else
		cancel_delayed_work_sync(&server->reconnect);
#endif

	spin_lock(&GlobalMid_Lock);
	server->tcpStatus = CifsExiting;
	spin_unlock(&GlobalMid_Lock);

	cifs_crypto_shash_release(server);
	cifs_fscache_release_client_cookie(server);

	kfree(server->session_key.response);
	server->session_key.response = NULL;
	server->session_key.len = 0;

	task = xchg(&server->tsk, NULL);
	if (task)
		send_sig(SIGKILL, task, 1);
}

static struct TCP_Server_Info *
cifs_get_tcp_session(struct smb_vol *volume_info)
{
	struct TCP_Server_Info *tcp_ses = NULL;
	int rc;

	cifs_dbg(FYI, "UNC: %s\n", volume_info->UNC);

	/* see if we already have a matching tcp_ses */
	tcp_ses = cifs_find_tcp_session(volume_info);
	if (tcp_ses)
		return tcp_ses;

	tcp_ses = kzalloc(sizeof(struct TCP_Server_Info), GFP_KERNEL);
	if (!tcp_ses) {
		rc = -ENOMEM;
		goto out_err;
	}

	tcp_ses->ops = volume_info->ops;
	tcp_ses->vals = volume_info->vals;
	cifs_set_net_ns(tcp_ses, get_net(current->nsproxy->net_ns));
	tcp_ses->hostname = extract_hostname(volume_info->UNC);
	if (IS_ERR(tcp_ses->hostname)) {
		rc = PTR_ERR(tcp_ses->hostname);
		goto out_err_crypto_release;
	}

	tcp_ses->noblocksnd = volume_info->noblocksnd;
	tcp_ses->noautotune = volume_info->noautotune;
	tcp_ses->tcp_nodelay = volume_info->sockopt_tcp_nodelay;
	tcp_ses->in_flight = 0;
	tcp_ses->credits = 1;
	init_waitqueue_head(&tcp_ses->response_q);
	init_waitqueue_head(&tcp_ses->request_q);
	INIT_LIST_HEAD(&tcp_ses->pending_mid_q);
	mutex_init(&tcp_ses->srv_mutex);
	memcpy(tcp_ses->workstation_RFC1001_name,
		volume_info->source_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	memcpy(tcp_ses->server_RFC1001_name,
		volume_info->target_rfc1001_name, RFC1001_NAME_LEN_WITH_NULL);
	tcp_ses->session_estab = false;
	tcp_ses->sequence_number = 0;
	tcp_ses->lstrp = jiffies;
	spin_lock_init(&tcp_ses->req_lock);
	INIT_LIST_HEAD(&tcp_ses->tcp_ses_list);
	INIT_LIST_HEAD(&tcp_ses->smb_ses_list);
	INIT_DELAYED_WORK(&tcp_ses->echo, cifs_echo_request);
#ifdef CONFIG_CIFS_SMB2
	INIT_DELAYED_WORK(&tcp_ses->reconnect, smb2_reconnect_server);
	mutex_init(&tcp_ses->reconnect_mutex);
#endif
	memcpy(&tcp_ses->srcaddr, &volume_info->srcaddr,
	       sizeof(tcp_ses->srcaddr));
	memcpy(&tcp_ses->dstaddr, &volume_info->dstaddr,
		sizeof(tcp_ses->dstaddr));
#ifdef CONFIG_CIFS_SMB2
	generate_random_uuid(tcp_ses->client_guid);
#endif
	/*
	 * at this point we are the only ones with the pointer
	 * to the struct since the kernel thread not created yet
	 * no need to spinlock this init of tcpStatus or srv_count
	 */
	tcp_ses->tcpStatus = CifsNew;
	++tcp_ses->srv_count;

	rc = ip_connect(tcp_ses);
	if (rc < 0) {
		cifs_dbg(VFS, "Error connecting to socket. Aborting operation.\n");
		goto out_err_crypto_release;
	}

	/*
	 * since we're in a cifs function already, we know that
	 * this will succeed. No need for try_module_get().
	 */
	__module_get(THIS_MODULE);
	tcp_ses->tsk = kthread_run(cifs_demultiplex_thread,
				  tcp_ses, "cifsd");
	if (IS_ERR(tcp_ses->tsk)) {
		rc = PTR_ERR(tcp_ses->tsk);
		cifs_dbg(VFS, "error %d create cifsd thread\n", rc);
		module_put(THIS_MODULE);
		goto out_err_crypto_release;
	}
	tcp_ses->tcpStatus = CifsNeedNegotiate;

	/* thread spawned, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcp_ses->tcp_ses_list, &cifs_tcp_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_client_cookie(tcp_ses);

	/* queue echo request delayed work */
	queue_delayed_work(cifsiod_wq, &tcp_ses->echo, SMB_ECHO_INTERVAL);

	return tcp_ses;

out_err_crypto_release:
	cifs_crypto_shash_release(tcp_ses);

	put_net(cifs_net_ns(tcp_ses));

out_err:
	if (tcp_ses) {
		if (!IS_ERR(tcp_ses->hostname))
			kfree(tcp_ses->hostname);
		if (tcp_ses->ssocket)
			sock_release(tcp_ses->ssocket);
		kfree(tcp_ses);
	}
	return ERR_PTR(rc);
}

static int match_session(struct cifs_ses *ses, struct smb_vol *vol)
{
	if (vol->sectype != Unspecified &&
	    vol->sectype != ses->sectype)
		return 0;

	switch (ses->sectype) {
	case Kerberos:
		if (!uid_eq(vol->cred_uid, ses->cred_uid))
			return 0;
		break;
	default:
		/* NULL username means anonymous session */
		if (ses->user_name == NULL) {
			if (!vol->nullauth)
				return 0;
			break;
		}

		/* anything else takes username/password */
		if (strncmp(ses->user_name,
			    vol->username ? vol->username : "",
			    CIFS_MAX_USERNAME_LEN))
			return 0;
		if ((vol->username && strlen(vol->username) != 0) &&
		    ses->password != NULL &&
		    strncmp(ses->password,
			    vol->password ? vol->password : "",
			    CIFS_MAX_PASSWORD_LEN))
			return 0;
	}
	return 1;
}

static struct cifs_ses *
cifs_find_smb_ses(struct TCP_Server_Info *server, struct smb_vol *vol)
{
	struct cifs_ses *ses;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &server->smb_ses_list, smb_ses_list) {
		if (ses->status == CifsExiting)
			continue;
		if (!match_session(ses, vol))
			continue;
		++ses->ses_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return ses;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

static void
cifs_put_smb_ses(struct cifs_ses *ses)
{
	unsigned int rc, xid;
	struct TCP_Server_Info *server = ses->server;

	cifs_dbg(FYI, "%s: ses_count=%d\n", __func__, ses->ses_count);

	spin_lock(&cifs_tcp_ses_lock);
	if (ses->status == CifsExiting) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}
	if (--ses->ses_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}
	if (ses->status == CifsGood)
		ses->status = CifsExiting;
	spin_unlock(&cifs_tcp_ses_lock);

	if (ses->status == CifsExiting && server->ops->logoff) {
		xid = get_xid();
		rc = server->ops->logoff(xid, ses);
		if (rc)
			cifs_dbg(VFS, "%s: Session Logoff failure rc=%d\n",
				__func__, rc);
		_free_xid(xid);
	}

	spin_lock(&cifs_tcp_ses_lock);
	list_del_init(&ses->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	sesInfoFree(ses);
	cifs_put_tcp_session(server, 0);
}

#ifdef CONFIG_KEYS

/* strlen("cifs:a:") + CIFS_MAX_DOMAINNAME_LEN + 1 */
#define CIFSCREDS_DESC_SIZE (7 + CIFS_MAX_DOMAINNAME_LEN + 1)

/* Populate username and pw fields from keyring if possible */
static int
cifs_set_cifscreds(struct smb_vol *vol, struct cifs_ses *ses)
{
	int rc = 0;
	int is_domain = 0;
	const char *delim, *payload;
	char *desc;
	ssize_t len;
	struct key *key;
	struct TCP_Server_Info *server = ses->server;
	struct sockaddr_in *sa;
	struct sockaddr_in6 *sa6;
	const struct user_key_payload *upayload;

	desc = kmalloc(CIFSCREDS_DESC_SIZE, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	/* try to find an address key first */
	switch (server->dstaddr.ss_family) {
	case AF_INET:
		sa = (struct sockaddr_in *)&server->dstaddr;
		sprintf(desc, "cifs:a:%pI4", &sa->sin_addr.s_addr);
		break;
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)&server->dstaddr;
		sprintf(desc, "cifs:a:%pI6c", &sa6->sin6_addr.s6_addr);
		break;
	default:
		cifs_dbg(FYI, "Bad ss_family (%hu)\n",
			 server->dstaddr.ss_family);
		rc = -EINVAL;
		goto out_err;
	}

	cifs_dbg(FYI, "%s: desc=%s\n", __func__, desc);
	key = request_key(&key_type_logon, desc, "");
	if (IS_ERR(key)) {
		if (!ses->domainName) {
			cifs_dbg(FYI, "domainName is NULL\n");
			rc = PTR_ERR(key);
			goto out_err;
		}

		/* didn't work, try to find a domain key */
		sprintf(desc, "cifs:d:%s", ses->domainName);
		cifs_dbg(FYI, "%s: desc=%s\n", __func__, desc);
		key = request_key(&key_type_logon, desc, "");
		if (IS_ERR(key)) {
			rc = PTR_ERR(key);
			goto out_err;
		}
		is_domain = 1;
	}

	down_read(&key->sem);
	upayload = user_key_payload(key);
	if (IS_ERR_OR_NULL(upayload)) {
		rc = upayload ? PTR_ERR(upayload) : -EINVAL;
		goto out_key_put;
	}

	/* find first : in payload */
	payload = upayload->data;
	delim = strnchr(payload, upayload->datalen, ':');
	cifs_dbg(FYI, "payload=%s\n", payload);
	if (!delim) {
		cifs_dbg(FYI, "Unable to find ':' in payload (datalen=%d)\n",
			 upayload->datalen);
		rc = -EINVAL;
		goto out_key_put;
	}

	len = delim - payload;
	if (len > CIFS_MAX_USERNAME_LEN || len <= 0) {
		cifs_dbg(FYI, "Bad value from username search (len=%zd)\n",
			 len);
		rc = -EINVAL;
		goto out_key_put;
	}

	vol->username = kstrndup(payload, len, GFP_KERNEL);
	if (!vol->username) {
		cifs_dbg(FYI, "Unable to allocate %zd bytes for username\n",
			 len);
		rc = -ENOMEM;
		goto out_key_put;
	}
	cifs_dbg(FYI, "%s: username=%s\n", __func__, vol->username);

	len = key->datalen - (len + 1);
	if (len > CIFS_MAX_PASSWORD_LEN || len <= 0) {
		cifs_dbg(FYI, "Bad len for password search (len=%zd)\n", len);
		rc = -EINVAL;
		kfree(vol->username);
		vol->username = NULL;
		goto out_key_put;
	}

	++delim;
	vol->password = kstrndup(delim, len, GFP_KERNEL);
	if (!vol->password) {
		cifs_dbg(FYI, "Unable to allocate %zd bytes for password\n",
			 len);
		rc = -ENOMEM;
		kfree(vol->username);
		vol->username = NULL;
		goto out_key_put;
	}

	/*
	 * If we have a domain key then we must set the domainName in the
	 * for the request.
	 */
	if (is_domain && ses->domainName) {
		vol->domainname = kstrndup(ses->domainName,
					   strlen(ses->domainName),
					   GFP_KERNEL);
		if (!vol->domainname) {
			cifs_dbg(FYI, "Unable to allocate %zd bytes for "
				 "domain\n", len);
			rc = -ENOMEM;
			kfree(vol->username);
			vol->username = NULL;
			kzfree(vol->password);
			vol->password = NULL;
			goto out_key_put;
		}
	}

out_key_put:
	up_read(&key->sem);
	key_put(key);
out_err:
	kfree(desc);
	cifs_dbg(FYI, "%s: returning %d\n", __func__, rc);
	return rc;
}
#else /* ! CONFIG_KEYS */
static inline int
cifs_set_cifscreds(struct smb_vol *vol __attribute__((unused)),
		   struct cifs_ses *ses __attribute__((unused)))
{
	return -ENOSYS;
}
#endif /* CONFIG_KEYS */

static struct cifs_ses *
cifs_get_smb_ses(struct TCP_Server_Info *server, struct smb_vol *volume_info)
{
	int rc = 0;
	unsigned int xid;
	struct cifs_ses *ses;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;

	xid = get_xid();

	ses = cifs_find_smb_ses(server, volume_info);
	if (ses) {
		cifs_dbg(FYI, "Existing smb sess found (status=%d)\n",
			 ses->status);

		mutex_lock(&ses->session_mutex);
		rc = cifs_negotiate_protocol(xid, ses);
		if (rc) {
			mutex_unlock(&ses->session_mutex);
			/* problem -- put our ses reference */
			cifs_put_smb_ses(ses);
			free_xid(xid);
			return ERR_PTR(rc);
		}
		if (ses->need_reconnect) {
			cifs_dbg(FYI, "Session needs reconnect\n");
			rc = cifs_setup_session(xid, ses,
						volume_info->local_nls);
			if (rc) {
				mutex_unlock(&ses->session_mutex);
				/* problem -- put our reference */
				cifs_put_smb_ses(ses);
				free_xid(xid);
				return ERR_PTR(rc);
			}
		}
		mutex_unlock(&ses->session_mutex);

		/* existing SMB ses has a server reference already */
		cifs_put_tcp_session(server, 0);
		free_xid(xid);
		return ses;
	}

	rc = -ENOMEM;

	cifs_dbg(FYI, "Existing smb sess not found\n");
	ses = sesInfoAlloc();
	if (ses == NULL)
		goto get_ses_fail;

	/* new SMB session uses our server ref */
	ses->server = server;
	if (server->dstaddr.ss_family == AF_INET6)
		sprintf(ses->serverName, "%pI6", &addr6->sin6_addr);
	else
		sprintf(ses->serverName, "%pI4", &addr->sin_addr);

	if (volume_info->username) {
		ses->user_name = kstrdup(volume_info->username, GFP_KERNEL);
		if (!ses->user_name)
			goto get_ses_fail;
	}

	/* volume_info->password freed at unmount */
	if (volume_info->password) {
		ses->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!ses->password)
			goto get_ses_fail;
	}
	if (volume_info->domainname) {
		ses->domainName = kstrdup(volume_info->domainname, GFP_KERNEL);
		if (!ses->domainName)
			goto get_ses_fail;
	}
	ses->cred_uid = volume_info->cred_uid;
	ses->linux_uid = volume_info->linux_uid;

	ses->sectype = volume_info->sectype;
	ses->sign = volume_info->sign;

	mutex_lock(&ses->session_mutex);
	rc = cifs_negotiate_protocol(xid, ses);
	if (!rc)
		rc = cifs_setup_session(xid, ses, volume_info->local_nls);
	mutex_unlock(&ses->session_mutex);
	if (rc)
		goto get_ses_fail;

	/* success, put it on the list */
	spin_lock(&cifs_tcp_ses_lock);
	list_add(&ses->smb_ses_list, &server->smb_ses_list);
	spin_unlock(&cifs_tcp_ses_lock);

	free_xid(xid);
	return ses;

get_ses_fail:
	sesInfoFree(ses);
	free_xid(xid);
	return ERR_PTR(rc);
}

static int match_tcon(struct cifs_tcon *tcon, const char *unc)
{
	if (tcon->tidStatus == CifsExiting)
		return 0;
	if (strncmp(tcon->treeName, unc, MAX_TREE_SIZE))
		return 0;
	return 1;
}

static struct cifs_tcon *
cifs_find_tcon(struct cifs_ses *ses, const char *unc)
{
	struct list_head *tmp;
	struct cifs_tcon *tcon;

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each(tmp, &ses->tcon_list) {
		tcon = list_entry(tmp, struct cifs_tcon, tcon_list);
		if (!match_tcon(tcon, unc))
			continue;
		++tcon->tc_count;
		spin_unlock(&cifs_tcp_ses_lock);
		return tcon;
	}
	spin_unlock(&cifs_tcp_ses_lock);
	return NULL;
}

void
cifs_put_tcon(struct cifs_tcon *tcon)
{
	unsigned int xid;
	struct cifs_ses *ses = tcon->ses;

	cifs_dbg(FYI, "%s: tc_count=%d\n", __func__, tcon->tc_count);
	spin_lock(&cifs_tcp_ses_lock);
	if (--tcon->tc_count > 0) {
		spin_unlock(&cifs_tcp_ses_lock);
		return;
	}

	list_del_init(&tcon->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	xid = get_xid();
	if (ses->server->ops->tree_disconnect)
		ses->server->ops->tree_disconnect(xid, tcon);
	_free_xid(xid);

	cifs_fscache_release_super_cookie(tcon);
	tconInfoFree(tcon);
	cifs_put_smb_ses(ses);
}

static struct cifs_tcon *
cifs_get_tcon(struct cifs_ses *ses, struct smb_vol *volume_info)
{
	int rc, xid;
	struct cifs_tcon *tcon;

	tcon = cifs_find_tcon(ses, volume_info->UNC);
	if (tcon) {
		cifs_dbg(FYI, "Found match on UNC path\n");
		/* existing tcon already has a reference */
		cifs_put_smb_ses(ses);
		if (tcon->seal != volume_info->seal)
			cifs_dbg(VFS, "transport encryption setting conflicts with existing tid\n");
		return tcon;
	}

	if (!ses->server->ops->tree_connect) {
		rc = -ENOSYS;
		goto out_fail;
	}

	tcon = tconInfoAlloc();
	if (tcon == NULL) {
		rc = -ENOMEM;
		goto out_fail;
	}

	tcon->ses = ses;
	if (volume_info->password) {
		tcon->password = kstrdup(volume_info->password, GFP_KERNEL);
		if (!tcon->password) {
			rc = -ENOMEM;
			goto out_fail;
		}
	}

	/*
	 * BB Do we need to wrap session_mutex around this TCon call and Unix
	 * SetFS as we do on SessSetup and reconnect?
	 */
	xid = get_xid();
	rc = ses->server->ops->tree_connect(xid, ses, volume_info->UNC, tcon,
					    volume_info->local_nls);
	free_xid(xid);
	cifs_dbg(FYI, "Tcon rc = %d\n", rc);
	if (rc)
		goto out_fail;

	if (volume_info->nodfs) {
		tcon->Flags &= ~SMB_SHARE_IS_IN_DFS;
		cifs_dbg(FYI, "DFS disabled (%d)\n", tcon->Flags);
	}
	tcon->seal = volume_info->seal;
	tcon->use_persistent = false;
	/* check if SMB2 or later, CIFS does not support persistent handles */
	if (volume_info->persistent) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB3 or later required for persistent handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
#ifdef CONFIG_CIFS_SMB2
		} else if (ses->server->capabilities &
			   SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
			tcon->use_persistent = true;
		else /* persistent handles requested but not supported */ {
			cifs_dbg(VFS,
				"Persistent handles not supported on share\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
#endif /* CONFIG_CIFS_SMB2 */
		}
#ifdef CONFIG_CIFS_SMB2
	} else if ((tcon->capabilities & SMB2_SHARE_CAP_CONTINUOUS_AVAILABILITY)
	     && (ses->server->capabilities & SMB2_GLOBAL_CAP_PERSISTENT_HANDLES)
	     && (volume_info->nopersistent == false)) {
		cifs_dbg(FYI, "enabling persistent handles\n");
		tcon->use_persistent = true;
#endif /* CONFIG_CIFS_SMB2 */
	} else if (volume_info->resilient) {
		if (ses->server->vals->protocol_id == 0) {
			cifs_dbg(VFS,
			     "SMB2.1 or later required for resilient handles\n");
			rc = -EOPNOTSUPP;
			goto out_fail;
		}
		tcon->use_resilient = true;
	}

	/*
	 * We can have only one retry value for a connection to a share so for
	 * resources mounted more than once to the same server share the last
	 * value passed in for the retry flag is used.
	 */
	tcon->retry = volume_info->retry;
	tcon->nocase = volume_info->nocase;
	tcon->local_lease = volume_info->local_lease;
	INIT_LIST_HEAD(&tcon->pending_opens);

	spin_lock(&cifs_tcp_ses_lock);
	list_add(&tcon->tcon_list, &ses->tcon_list);
	spin_unlock(&cifs_tcp_ses_lock);

	cifs_fscache_get_super_cookie(tcon);

	return tcon;

out_fail:
	tconInfoFree(tcon);
	return ERR_PTR(rc);
}

void
cifs_put_tlink(struct tcon_link *tlink)
{
	if (!tlink || IS_ERR(tlink))
		return;

	if (!atomic_dec_and_test(&tlink->tl_count) ||
	    test_bit(TCON_LINK_IN_TREE, &tlink->tl_flags)) {
		tlink->tl_time = jiffies;
		return;
	}

	if (!IS_ERR(tlink_tcon(tlink)))
		cifs_put_tcon(tlink_tcon(tlink));
	kfree(tlink);
	return;
}

static inline struct tcon_link *
cifs_sb_master_tlink(struct cifs_sb_info *cifs_sb)
{
	return cifs_sb->master_tlink;
}

static int
compare_mount_options(struct super_block *sb, struct cifs_mnt_data *mnt_data)
{
	struct cifs_sb_info *old = CIFS_SB(sb);
	struct cifs_sb_info *new = mnt_data->cifs_sb;

	if ((sb->s_flags & CIFS_MS_MASK) != (mnt_data->flags & CIFS_MS_MASK))
		return 0;

	if ((old->mnt_cifs_flags & CIFS_MOUNT_MASK) !=
	    (new->mnt_cifs_flags & CIFS_MOUNT_MASK))
		return 0;

	/*
	 * We want to share sb only if we don't specify an r/wsize or
	 * specified r/wsize is greater than or equal to existing one.
	 */
	if (new->wsize && new->wsize < old->wsize)
		return 0;

	if (new->rsize && new->rsize < old->rsize)
		return 0;

	if (!uid_eq(old->mnt_uid, new->mnt_uid) || !gid_eq(old->mnt_gid, new->mnt_gid))
		return 0;

	if (old->mnt_file_mode != new->mnt_file_mode ||
	    old->mnt_dir_mode != new->mnt_dir_mode)
		return 0;

	if (strcmp(old->local_nls->charset, new->local_nls->charset))
		return 0;

	if (old->actimeo != new->actimeo)
		return 0;

	return 1;
}

int
cifs_match_super(struct super_block *sb, void *data)
{
	struct cifs_mnt_data *mnt_data = (struct cifs_mnt_data *)data;
	struct smb_vol *volume_info;
	struct cifs_sb_info *cifs_sb;
	struct TCP_Server_Info *tcp_srv;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	int rc = 0;

	spin_lock(&cifs_tcp_ses_lock);
	cifs_sb = CIFS_SB(sb);
	tlink = cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));
	if (tlink == NULL) {
		/* can not match superblock if tlink were ever null */
		spin_unlock(&cifs_tcp_ses_lock);
		return 0;
	}
	tcon = tlink_tcon(tlink);
	ses = tcon->ses;
	tcp_srv = ses->server;

	volume_info = mnt_data->vol;

	if (!match_server(tcp_srv, volume_info) ||
	    !match_session(ses, volume_info) ||
	    !match_tcon(tcon, volume_info->UNC)) {
		rc = 0;
		goto out;
	}

	rc = compare_mount_options(sb, mnt_data);
out:
	spin_unlock(&cifs_tcp_ses_lock);
	cifs_put_tlink(tlink);
	return rc;
}

int
get_dfs_path(const unsigned int xid, struct cifs_ses *ses, const char *old_path,
	     const struct nls_table *nls_codepage, unsigned int *num_referrals,
	     struct dfs_info3_param **referrals, int remap)
{
	char *temp_unc;
	int rc = 0;

	*pnum_referrals = 0;
	*preferrals = NULL;

	if (pSesInfo->ipc_tid == 0) {
		temp_unc = kmalloc(2 /* for slashes */ +
			strnlen(pSesInfo->serverName,
				SERVER_NAME_LEN_WITH_NULL * 2)
				 + 1 + 4 /* slash IPC$ */  + 2,
				GFP_KERNEL);
	if (!ses->server->ops->tree_connect || !ses->server->ops->get_dfs_refer)
		return -ENOSYS;

	*num_referrals = 0;
	*referrals = NULL;

	if (ses->ipc_tid == 0) {
		temp_unc = kmalloc(2 /* for slashes */ +
			strnlen(ses->serverName, SERVER_NAME_LEN_WITH_NULL * 2)
				+ 1 + 4 /* slash IPC$ */ + 2, GFP_KERNEL);
		if (temp_unc == NULL)
			return -ENOMEM;
		temp_unc[0] = '\\';
		temp_unc[1] = '\\';
		strcpy(temp_unc + 2, pSesInfo->serverName);
		strcpy(temp_unc + 2 + strlen(pSesInfo->serverName), "\\IPC$");
		rc = CIFSTCon(xid, pSesInfo, temp_unc, NULL, nls_codepage);
		cFYI(1,
		     ("CIFS Tcon rc = %d ipc_tid = %d", rc, pSesInfo->ipc_tid));
		kfree(temp_unc);
	}
	if (rc == 0)
		rc = CIFSGetDFSRefer(xid, pSesInfo, old_path, preferrals,
				     pnum_referrals, nls_codepage, remap);
	/* BB map targetUNCs to dfs_info3 structures, here or
		in CIFSGetDFSRefer BB */
		strcpy(temp_unc + 2, ses->serverName);
		strcpy(temp_unc + 2 + strlen(ses->serverName), "\\IPC$");
		rc = ses->server->ops->tree_connect(xid, ses, temp_unc, NULL,
						    nls_codepage);
		cifs_dbg(FYI, "Tcon rc = %d ipc_tid = %d\n", rc, ses->ipc_tid);
		kfree(temp_unc);
	}
	if (rc == 0)
		rc = ses->server->ops->get_dfs_refer(xid, ses, old_path,
						     referrals, num_referrals,
						     nls_codepage, remap);
	/*
	 * BB - map targetUNCs to dfs_info3 structures, here or in
	 * ses->server->ops->get_dfs_refer.
	 */

	return rc;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key cifs_key[2];
static struct lock_class_key cifs_slock_key[2];

static inline void
cifs_reclassify_socket4(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET-CIFS",
		&cifs_slock_key[0], "sk_lock-AF_INET-CIFS", &cifs_key[0]);
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
	struct sock *sk = sock->sk;
	BUG_ON(sock_owned_by_user(sk));
	sock_lock_init_class_and_name(sk, "slock-AF_INET6-CIFS",
		&cifs_slock_key[1], "sk_lock-AF_INET6-CIFS", &cifs_key[1]);
}
#else
static inline void
cifs_reclassify_socket4(struct socket *sock)
{
}

static inline void
cifs_reclassify_socket6(struct socket *sock)
{
}
#endif

/* See RFC1001 section 14 on representation of Netbios names */
static void rfc1002mangle(char *target, char *source, unsigned int length)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < (length); i++) {
		/* mask a nibble at a time and encode */
		target[j] = 'A' + (0x0F & (source[i] >> 4));
		target[j+1] = 'A' + (0x0F & source[i]);
		j += 2;
	}

}


static int
ipv4_connect(struct sockaddr_in *psin_server, struct socket **csocket,
	     char *netbios_name, char *target_name)
{
	int rc = 0;
	int connected = 0;
	__be16 orig_port = 0;

	if (*csocket == NULL) {
		rc = sock_create_kern(PF_INET, SOCK_STREAM,
				      IPPROTO_TCP, csocket);
		if (rc < 0) {
			cERROR(1, ("Error %d creating socket", rc));
			*csocket = NULL;
			return rc;
		} else {
		/* BB other socket options to set KEEPALIVE, NODELAY? */
			cFYI(1, ("Socket created"));
			(*csocket)->sk->sk_allocation = GFP_NOFS;
			cifs_reclassify_socket4(*csocket);
		}
	}

	psin_server->sin_family = AF_INET;
	if (psin_server->sin_port) { /* user overrode default port */
		rc = (*csocket)->ops->connect(*csocket,
				(struct sockaddr *) psin_server,
				sizeof(struct sockaddr_in), 0);
		if (rc >= 0)
			connected = 1;
	}

	if (!connected) {
		/* save original port so we can retry user specified port
			later if fall back ports fail this time  */
		orig_port = psin_server->sin_port;

		/* do not retry on the same port we just failed on */
		if (psin_server->sin_port != htons(CIFS_PORT)) {
			psin_server->sin_port = htons(CIFS_PORT);

			rc = (*csocket)->ops->connect(*csocket,
					(struct sockaddr *) psin_server,
					sizeof(struct sockaddr_in), 0);
			if (rc >= 0)
				connected = 1;
		}
	}
	if (!connected) {
		psin_server->sin_port = htons(RFC1001_PORT);
		rc = (*csocket)->ops->connect(*csocket, (struct sockaddr *)
					      psin_server,
					      sizeof(struct sockaddr_in), 0);
		if (rc >= 0)
			connected = 1;
	}

	/* give up here - unless we want to retry on different
		protocol families some day */
	if (!connected) {
		if (orig_port)
			psin_server->sin_port = orig_port;
		cFYI(1, ("Error %d connecting to server via ipv4", rc));
		sock_release(*csocket);
		*csocket = NULL;
		return rc;
	}
	/* Eventually check for other socket options to change from
		the default. sock_setsockopt not used because it expects
		user space buffer */
	 cFYI(1, ("sndbuf %d rcvbuf %d rcvtimeo 0x%lx",
		 (*csocket)->sk->sk_sndbuf,
		 (*csocket)->sk->sk_rcvbuf, (*csocket)->sk->sk_rcvtimeo));
	(*csocket)->sk->sk_rcvtimeo = 7 * HZ;
	/* make the bufsizes depend on wsize/rsize and max requests */
	if ((*csocket)->sk->sk_sndbuf < (200 * 1024))
		(*csocket)->sk->sk_sndbuf = 200 * 1024;
	if ((*csocket)->sk->sk_rcvbuf < (140 * 1024))
		(*csocket)->sk->sk_rcvbuf = 140 * 1024;

	/* send RFC1001 sessinit */
	if (psin_server->sin_port == htons(RFC1001_PORT)) {
		/* some servers require RFC1001 sessinit before sending
		negprot - BB check reconnection in case where second
		sessinit is sent but no second negprot */
		struct rfc1002_session_packet *ses_init_buf;
		struct smb_hdr *smb_buf;
		ses_init_buf = kzalloc(sizeof(struct rfc1002_session_packet),
				       GFP_KERNEL);
		if (ses_init_buf) {
			ses_init_buf->trailer.session_req.called_len = 32;
			if (target_name && (target_name[0] != 0)) {
				rfc1002mangle(ses_init_buf->trailer.session_req.called_name,
					target_name, 16);
			} else {
				rfc1002mangle(ses_init_buf->trailer.session_req.called_name,
					DEFAULT_CIFS_CALLED_NAME, 16);
			}

			ses_init_buf->trailer.session_req.calling_len = 32;
			/* calling name ends in null (byte 16) from old smb
			convention. */
			if (netbios_name && (netbios_name[0] != 0)) {
				rfc1002mangle(ses_init_buf->trailer.session_req.calling_name,
					netbios_name, 16);
			} else {
				rfc1002mangle(ses_init_buf->trailer.session_req.calling_name,
					"LINUX_CIFS_CLNT", 16);
			}
			ses_init_buf->trailer.session_req.scope1 = 0;
			ses_init_buf->trailer.session_req.scope2 = 0;
			smb_buf = (struct smb_hdr *)ses_init_buf;
			/* sizeof RFC1002_SESSION_REQUEST with no scope */
			smb_buf->smb_buf_length = 0x81000044;
			rc = smb_send(*csocket, smb_buf, 0x44,
				(struct sockaddr *)psin_server);
			kfree(ses_init_buf);
			msleep(1); /* RFC1001 layer in at least one server
				      requires very short break before negprot
				      presumably because not expecting negprot
				      to follow so fast.  This is a simple
				      solution that works without
				      complicating the code and causes no
				      significant slowing down on mount
				      for everyone else */
		}
		/* else the negprot may still work without this
		even though malloc failed */

	}

static int
bind_socket(struct TCP_Server_Info *server)
{
	int rc = 0;
	if (server->srcaddr.ss_family != AF_UNSPEC) {
		/* Bind to the specified local IP address */
		struct socket *socket = server->ssocket;
		rc = socket->ops->bind(socket,
				       (struct sockaddr *) &server->srcaddr,
				       sizeof(server->srcaddr));
		if (rc < 0) {
			struct sockaddr_in *saddr4;
			struct sockaddr_in6 *saddr6;
			saddr4 = (struct sockaddr_in *)&server->srcaddr;
			saddr6 = (struct sockaddr_in6 *)&server->srcaddr;
			if (saddr6->sin6_family == AF_INET6)
				cifs_dbg(VFS, "Failed to bind to: %pI6c, error: %d\n",
					 &saddr6->sin6_addr, rc);
			else
				cifs_dbg(VFS, "Failed to bind to: %pI4, error: %d\n",
					 &saddr4->sin_addr.s_addr, rc);
		}
	}
	return rc;
}

static int
ip_rfc1001_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	/*
	 * some servers require RFC1001 sessinit before sending
	 * negprot - BB check reconnection in case where second
	 * sessinit is sent but no second negprot
	 */
	struct rfc1002_session_packet *ses_init_buf;
	struct smb_hdr *smb_buf;
	ses_init_buf = kzalloc(sizeof(struct rfc1002_session_packet),
			       GFP_KERNEL);
	if (ses_init_buf) {
		ses_init_buf->trailer.session_req.called_len = 32;

		if (server->server_RFC1001_name &&
		    server->server_RFC1001_name[0] != 0)
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.called_name,
				      server->server_RFC1001_name,
				      RFC1001_NAME_LEN_WITH_NULL);
		else
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.called_name,
				      DEFAULT_CIFS_CALLED_NAME,
				      RFC1001_NAME_LEN_WITH_NULL);

		ses_init_buf->trailer.session_req.calling_len = 32;

		/*
		 * calling name ends in null (byte 16) from old smb
		 * convention.
		 */
		if (server->workstation_RFC1001_name[0] != 0)
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.calling_name,
				      server->workstation_RFC1001_name,
				      RFC1001_NAME_LEN_WITH_NULL);
		else
			rfc1002mangle(ses_init_buf->trailer.
				      session_req.calling_name,
				      "LINUX_CIFS_CLNT",
				      RFC1001_NAME_LEN_WITH_NULL);

		ses_init_buf->trailer.session_req.scope1 = 0;
		ses_init_buf->trailer.session_req.scope2 = 0;
		smb_buf = (struct smb_hdr *)ses_init_buf;

		/* sizeof RFC1002_SESSION_REQUEST with no scope */
		smb_buf->smb_buf_length = cpu_to_be32(0x81000044);
		rc = smb_send(server, smb_buf, 0x44);
		kfree(ses_init_buf);
		/*
		 * RFC1001 layer in at least one server
		 * requires very short break before negprot
		 * presumably because not expecting negprot
		 * to follow so fast.  This is a simple
		 * solution that works without
		 * complicating the code and causes no
		 * significant slowing down on mount
		 * for everyone else
		 */
		usleep_range(1000, 2000);
	}
	/*
	 * else the negprot may still work without this
	 * even though malloc failed
	 */

	return rc;
}

static int
generic_ip_connect(struct TCP_Server_Info *server)
{
	int rc = 0;
	__be16 sport;
	int slen, sfamily;
	struct socket *socket = server->ssocket;
	struct sockaddr *saddr;

	saddr = (struct sockaddr *) &server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6) {
		sport = ((struct sockaddr_in6 *) saddr)->sin6_port;
		slen = sizeof(struct sockaddr_in6);
		sfamily = AF_INET6;
	} else {
		sport = ((struct sockaddr_in *) saddr)->sin_port;
		slen = sizeof(struct sockaddr_in);
		sfamily = AF_INET;
	}

	if (socket == NULL) {
		rc = __sock_create(cifs_net_ns(server), sfamily, SOCK_STREAM,
				   IPPROTO_TCP, &socket, 1);
		if (rc < 0) {
			cifs_dbg(VFS, "Error %d creating socket\n", rc);
			server->ssocket = NULL;
			return rc;
		}

		/* BB other socket options to set KEEPALIVE, NODELAY? */
		cifs_dbg(FYI, "Socket created\n");
		server->ssocket = socket;
		socket->sk->sk_allocation = GFP_NOFS;
		if (sfamily == AF_INET6)
			cifs_reclassify_socket6(socket);
		else
			cifs_reclassify_socket4(socket);
	}

	rc = bind_socket(server);
	if (rc < 0)
		return rc;

	/*
	 * Eventually check for other socket options to change from
	 * the default. sock_setsockopt not used because it expects
	 * user space buffer
	 */
	socket->sk->sk_rcvtimeo = 7 * HZ;
	socket->sk->sk_sndtimeo = 5 * HZ;

	/* make the bufsizes depend on wsize/rsize and max requests */
	if (server->noautotune) {
		if (socket->sk->sk_sndbuf < (200 * 1024))
			socket->sk->sk_sndbuf = 200 * 1024;
		if (socket->sk->sk_rcvbuf < (140 * 1024))
			socket->sk->sk_rcvbuf = 140 * 1024;
	}

	if (server->tcp_nodelay) {
		int val = 1;
		rc = kernel_setsockopt(socket, SOL_TCP, TCP_NODELAY,
				(char *)&val, sizeof(val));
		if (rc)
			cifs_dbg(FYI, "set TCP_NODELAY socket option error %d\n",
				 rc);
	}

	cifs_dbg(FYI, "sndbuf %d rcvbuf %d rcvtimeo 0x%lx\n",
		 socket->sk->sk_sndbuf,
		 socket->sk->sk_rcvbuf, socket->sk->sk_rcvtimeo);

	rc = socket->ops->connect(socket, saddr, slen, 0);
	if (rc < 0) {
		cifs_dbg(FYI, "Error %d connecting to server\n", rc);
		sock_release(socket);
		server->ssocket = NULL;
		return rc;
	}

	if (sport == htons(RFC1001_PORT))
		rc = ip_rfc1001_connect(server);

	return rc;
}

static int
ipv6_connect(struct sockaddr_in6 *psin_server, struct socket **csocket)
{
	int rc = 0;
	int connected = 0;
	__be16 orig_port = 0;

	if (*csocket == NULL) {
		rc = sock_create_kern(PF_INET6, SOCK_STREAM,
				      IPPROTO_TCP, csocket);
		if (rc < 0) {
			cERROR(1, ("Error %d creating ipv6 socket", rc));
			*csocket = NULL;
			return rc;
		} else {
		/* BB other socket options to set KEEPALIVE, NODELAY? */
			 cFYI(1, ("ipv6 Socket created"));
			(*csocket)->sk->sk_allocation = GFP_NOFS;
			cifs_reclassify_socket6(*csocket);
		}
	}

	psin_server->sin6_family = AF_INET6;

	if (psin_server->sin6_port) { /* user overrode default port */
		rc = (*csocket)->ops->connect(*csocket,
				(struct sockaddr *) psin_server,
				sizeof(struct sockaddr_in6), 0);
		if (rc >= 0)
			connected = 1;
	}

	if (!connected) {
		/* save original port so we can retry user specified port
			later if fall back ports fail this time  */

		orig_port = psin_server->sin6_port;
		/* do not retry on the same port we just failed on */
		if (psin_server->sin6_port != htons(CIFS_PORT)) {
			psin_server->sin6_port = htons(CIFS_PORT);

			rc = (*csocket)->ops->connect(*csocket,
					(struct sockaddr *) psin_server,
					sizeof(struct sockaddr_in6), 0);
			if (rc >= 0)
				connected = 1;
		}
	}
	if (!connected) {
		psin_server->sin6_port = htons(RFC1001_PORT);
		rc = (*csocket)->ops->connect(*csocket, (struct sockaddr *)
				 psin_server, sizeof(struct sockaddr_in6), 0);
		if (rc >= 0)
			connected = 1;
	}

	/* give up here - unless we want to retry on different
		protocol families some day */
	if (!connected) {
		if (orig_port)
			psin_server->sin6_port = orig_port;
		cFYI(1, ("Error %d connecting to server via ipv6", rc));
		sock_release(*csocket);
		*csocket = NULL;
		return rc;
	}
	/* Eventually check for other socket options to change from
		the default. sock_setsockopt not used because it expects
		user space buffer */
	(*csocket)->sk->sk_rcvtimeo = 7 * HZ;

	return rc;
}

void reset_cifs_unix_caps(int xid, struct cifsTconInfo *tcon,
			  struct super_block *sb, struct smb_vol *vol_info)
ip_connect(struct TCP_Server_Info *server)
{
	__be16 *sport;
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&server->dstaddr;
	struct sockaddr_in *addr = (struct sockaddr_in *)&server->dstaddr;

	if (server->dstaddr.ss_family == AF_INET6)
		sport = &addr6->sin6_port;
	else
		sport = &addr->sin_port;

	if (*sport == 0) {
		int rc;

		/* try with 445 port at first */
		*sport = htons(CIFS_PORT);

		rc = generic_ip_connect(server);
		if (rc >= 0)
			return rc;

		/* if it failed, try with 139 port */
		*sport = htons(RFC1001_PORT);
	}

	return generic_ip_connect(server);
}

void reset_cifs_unix_caps(unsigned int xid, struct cifs_tcon *tcon,
			  struct cifs_sb_info *cifs_sb, struct smb_vol *vol_info)
{
	/* if we are reconnecting then should we check to see if
	 * any requested capabilities changed locally e.g. via
	 * remount but we can not do much about it here
	 * if they have (even if we could detect it by the following)
	 * Perhaps we could add a backpointer to array of sb from tcon
	 * or if we change to make all sb to same share the same
	 * sb as NFS - then we only have one backpointer to sb.
	 * What if we wanted to mount the server share twice once with
	 * and once without posixacls or posix paths? */
	__u64 saved_cap = le64_to_cpu(tcon->fsUnixInfo.Capability);

	if (vol_info && vol_info->no_linux_ext) {
		tcon->fsUnixInfo.Capability = 0;
		tcon->unix_ext = 0; /* Unix Extensions disabled */
		cFYI(1, ("Linux protocol extensions disabled"));
		cifs_dbg(FYI, "Linux protocol extensions disabled\n");
		return;
	} else if (vol_info)
		tcon->unix_ext = 1; /* Unix Extensions supported */

	if (tcon->unix_ext == 0) {
		cFYI(1, ("Unix extensions disabled so not set on reconnect"));
		cifs_dbg(FYI, "Unix extensions disabled so not set on reconnect\n");
		return;
	}

	if (!CIFSSMBQFSUnixInfo(xid, tcon)) {
		__u64 cap = le64_to_cpu(tcon->fsUnixInfo.Capability);

		cifs_dbg(FYI, "unix caps which server supports %lld\n", cap);
		/* check for reconnect case in which we do not
		   want to change the mount behavior if we can avoid it */
		if (vol_info == NULL) {
			/* turn off POSIX ACL and PATHNAMES if not set
			   originally at mount time */
			if ((saved_cap & CIFS_UNIX_POSIX_ACL_CAP) == 0)
				cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
			if ((saved_cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
					cERROR(1, ("POSIXPATH support change"));
				cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
			} else if ((cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				cERROR(1, ("possible reconnect error"));
				cERROR(1,
					("server disabled POSIX path support"));
			}
		}

					cifs_dbg(VFS, "POSIXPATH support change\n");
				cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
			} else if ((cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) == 0) {
				cifs_dbg(VFS, "possible reconnect error\n");
				cifs_dbg(VFS, "server disabled POSIX path support\n");
			}
		}

		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cifs_dbg(VFS, "per-share encryption not supported yet\n");

		cap &= CIFS_UNIX_CAP_MASK;
		if (vol_info && vol_info->no_psx_acl)
			cap &= ~CIFS_UNIX_POSIX_ACL_CAP;
		else if (CIFS_UNIX_POSIX_ACL_CAP & cap) {
			cFYI(1, ("negotiated posix acl support"));
			if (sb)
				sb->s_flags |= MS_POSIXACL;
			cifs_dbg(FYI, "negotiated posix acl support\n");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIXACL;
		}

		if (vol_info && vol_info->posix_paths == 0)
			cap &= ~CIFS_UNIX_POSIX_PATHNAMES_CAP;
		else if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP) {
			cFYI(1, ("negotiate posix pathnames"));
			if (sb)
				CIFS_SB(sb)->mnt_cifs_flags |=
					CIFS_MOUNT_POSIX_PATHS;
		}

		/* We might be setting the path sep back to a different
		form if we are reconnecting and the server switched its
		posix path capability for this share */
		if (sb && (CIFS_SB(sb)->prepathlen > 0))
			CIFS_SB(sb)->prepath[0] = CIFS_DIR_SEP(CIFS_SB(sb));

		if (sb && (CIFS_SB(sb)->rsize > 127 * 1024)) {
			if ((cap & CIFS_UNIX_LARGE_READ_CAP) == 0) {
				CIFS_SB(sb)->rsize = 127 * 1024;
				cFYI(DBG2,
					("larger reads not supported by srv"));
			}
		}


		cFYI(1, ("Negotiate caps 0x%x", (int)cap));
#ifdef CONFIG_CIFS_DEBUG2
		if (cap & CIFS_UNIX_FCNTL_CAP)
			cFYI(1, ("FCNTL cap"));
		if (cap & CIFS_UNIX_EXTATTR_CAP)
			cFYI(1, ("EXTATTR cap"));
		if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
			cFYI(1, ("POSIX path cap"));
		if (cap & CIFS_UNIX_XATTR_CAP)
			cFYI(1, ("XATTR cap"));
		if (cap & CIFS_UNIX_POSIX_ACL_CAP)
			cFYI(1, ("POSIX ACL cap"));
		if (cap & CIFS_UNIX_LARGE_READ_CAP)
			cFYI(1, ("very large read cap"));
		if (cap & CIFS_UNIX_LARGE_WRITE_CAP)
			cFYI(1, ("very large write cap"));
#endif /* CIFS_DEBUG2 */
		if (CIFSSMBSetFSUnixInfo(xid, tcon, cap)) {
			if (vol_info == NULL) {
				cFYI(1, ("resetting capabilities failed"));
			} else
				cERROR(1, ("Negotiating Unix capabilities "
					   "with the server failed.  Consider "
					   "mounting with the Unix Extensions\n"
					   "disabled, if problems are found, "
					   "by specifying the nounix mount "
					   "option."));
			cifs_dbg(FYI, "negotiate posix pathnames\n");
			if (cifs_sb)
				cifs_sb->mnt_cifs_flags |=
					CIFS_MOUNT_POSIX_PATHS;
		}

		cifs_dbg(FYI, "Negotiate caps 0x%x\n", (int)cap);
#ifdef CONFIG_CIFS_DEBUG2
		if (cap & CIFS_UNIX_FCNTL_CAP)
			cifs_dbg(FYI, "FCNTL cap\n");
		if (cap & CIFS_UNIX_EXTATTR_CAP)
			cifs_dbg(FYI, "EXTATTR cap\n");
		if (cap & CIFS_UNIX_POSIX_PATHNAMES_CAP)
			cifs_dbg(FYI, "POSIX path cap\n");
		if (cap & CIFS_UNIX_XATTR_CAP)
			cifs_dbg(FYI, "XATTR cap\n");
		if (cap & CIFS_UNIX_POSIX_ACL_CAP)
			cifs_dbg(FYI, "POSIX ACL cap\n");
		if (cap & CIFS_UNIX_LARGE_READ_CAP)
			cifs_dbg(FYI, "very large read cap\n");
		if (cap & CIFS_UNIX_LARGE_WRITE_CAP)
			cifs_dbg(FYI, "very large write cap\n");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_CAP)
			cifs_dbg(FYI, "transport encryption cap\n");
		if (cap & CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)
			cifs_dbg(FYI, "mandatory transport encryption cap\n");
#endif /* CIFS_DEBUG2 */
		if (CIFSSMBSetFSUnixInfo(xid, tcon, cap)) {
			if (vol_info == NULL) {
				cifs_dbg(FYI, "resetting capabilities failed\n");
			} else
				cifs_dbg(VFS, "Negotiating Unix capabilities with the server failed. Consider mounting with the Unix Extensions disabled if problems are found by specifying the nounix mount option.\n");

		}
	}
}

static void
convert_delimiter(char *path, char delim)
{
	int i;
	char old_delim;

	if (path == NULL)
		return;

	if (delim == '/')
		old_delim = '\\';
	else
		old_delim = '/';

	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] == old_delim)
			path[i] = delim;
	}
}

int
cifs_mount(struct super_block *sb, struct cifs_sb_info *cifs_sb,
	   char *mount_data, const char *devname)
{
	int rc = 0;
	int xid;
	int address_type = AF_INET;
	struct socket *csocket = NULL;
	struct sockaddr_in sin_server;
	struct sockaddr_in6 sin_server6;
	struct smb_vol volume_info;
	struct cifsSesInfo *pSesInfo = NULL;
	struct cifsSesInfo *existingCifsSes = NULL;
	struct cifsTconInfo *tcon = NULL;
	struct TCP_Server_Info *srvTcp = NULL;

	xid = GetXid();

/* cFYI(1, ("Entering cifs_mount. Xid: %d with: %s", xid, mount_data)); */

	memset(&volume_info, 0, sizeof(struct smb_vol));
	if (cifs_parse_mount_options(mount_data, devname, &volume_info)) {
		rc = -EINVAL;
		goto out;
	}

	if (volume_info.nullauth) {
		cFYI(1, ("null user"));
		volume_info.username = "";
	} else if (volume_info.username) {
		/* BB fixme parse for domain name here */
		cFYI(1, ("Username: %s", volume_info.username));
	} else {
		cifserror("No username specified");
	/* In userspace mount helper we can get user name from alternate
	   locations such as env variables and files on disk */
		rc = -EINVAL;
		goto out;
	}

	if (volume_info.UNCip && volume_info.UNC) {
		rc = cifs_inet_pton(AF_INET, volume_info.UNCip,
				    &sin_server.sin_addr.s_addr);

		if (rc <= 0) {
			/* not ipv4 address, try ipv6 */
			rc = cifs_inet_pton(AF_INET6, volume_info.UNCip,
					    &sin_server6.sin6_addr.in6_u);
			if (rc > 0)
				address_type = AF_INET6;
		} else {
			address_type = AF_INET;
		}

		if (rc <= 0) {
			/* we failed translating address */
			rc = -EINVAL;
			goto out;
		}

		cFYI(1, ("UNC: %s ip: %s", volume_info.UNC, volume_info.UNCip));
		/* success */
		rc = 0;
	} else if (volume_info.UNCip) {
		/* BB using ip addr as server name to connect to the
		   DFS root below */
		cERROR(1, ("Connecting to DFS root not implemented yet"));
		rc = -EINVAL;
		goto out;
	} else /* which servers DFS root would we conect to */ {
		cERROR(1,
		       ("CIFS mount error: No UNC path (e.g. -o "
			"unc=//192.168.1.100/public) specified"));
		rc = -EINVAL;
		goto out;
	}

	/* this is needed for ASCII cp to Unicode converts */
	if (volume_info.iocharset == NULL) {
		cifs_sb->local_nls = load_nls_default();
	/* load_nls_default can not return null */
	} else {
		cifs_sb->local_nls = load_nls(volume_info.iocharset);
		if (cifs_sb->local_nls == NULL) {
			cERROR(1, ("CIFS mount error: iocharset %s not found",
				 volume_info.iocharset));
			rc = -ELIBACC;
			goto out;
		}
	}

	if (address_type == AF_INET)
		existingCifsSes = cifs_find_tcp_session(&sin_server.sin_addr,
			NULL /* no ipv6 addr */,
			volume_info.username, &srvTcp);
	else if (address_type == AF_INET6) {
		cFYI(1, ("looking for ipv6 address"));
		existingCifsSes = cifs_find_tcp_session(NULL /* no ipv4 addr */,
			&sin_server6.sin6_addr,
			volume_info.username, &srvTcp);
	} else {
		rc = -EINVAL;
		goto out;
	}

	if (srvTcp) {
		cFYI(1, ("Existing tcp session with server found"));
	} else {	/* create socket */
		if (volume_info.port)
			sin_server.sin_port = htons(volume_info.port);
		else
			sin_server.sin_port = 0;
		if (address_type == AF_INET6) {
			cFYI(1, ("attempting ipv6 connect"));
			/* BB should we allow ipv6 on port 139? */
			/* other OS never observed in Wild doing 139 with v6 */
			rc = ipv6_connect(&sin_server6, &csocket);
		} else
			rc = ipv4_connect(&sin_server, &csocket,
				  volume_info.source_rfc1001_name,
				  volume_info.target_rfc1001_name);
		if (rc < 0) {
			cERROR(1, ("Error connecting to IPv4 socket. "
				   "Aborting operation"));
			if (csocket != NULL)
				sock_release(csocket);
			goto out;
		}

		srvTcp = kzalloc(sizeof(struct TCP_Server_Info), GFP_KERNEL);
		if (!srvTcp) {
			rc = -ENOMEM;
			sock_release(csocket);
			goto out;
		} else {
			memcpy(&srvTcp->addr.sockAddr, &sin_server,
				sizeof(struct sockaddr_in));
			atomic_set(&srvTcp->inFlight, 0);
			/* BB Add code for ipv6 case too */
			srvTcp->ssocket = csocket;
			srvTcp->protocolType = IPV4;
			srvTcp->hostname = extract_hostname(volume_info.UNC);
			if (IS_ERR(srvTcp->hostname)) {
				rc = PTR_ERR(srvTcp->hostname);
				sock_release(csocket);
				goto out;
			}
			init_waitqueue_head(&srvTcp->response_q);
			init_waitqueue_head(&srvTcp->request_q);
			INIT_LIST_HEAD(&srvTcp->pending_mid_q);
			/* at this point we are the only ones with the pointer
			to the struct since the kernel thread not created yet
			so no need to spinlock this init of tcpStatus */
			srvTcp->tcpStatus = CifsNew;
			init_MUTEX(&srvTcp->tcpSem);
			srvTcp->tsk = kthread_run((void *)(void *)cifs_demultiplex_thread, srvTcp, "cifsd");
			if (IS_ERR(srvTcp->tsk)) {
				rc = PTR_ERR(srvTcp->tsk);
				cERROR(1, ("error %d create cifsd thread", rc));
				srvTcp->tsk = NULL;
				sock_release(csocket);
				kfree(srvTcp->hostname);
				goto out;
			}
			rc = 0;
			memcpy(srvTcp->workstation_RFC1001_name,
				volume_info.source_rfc1001_name, 16);
			memcpy(srvTcp->server_RFC1001_name,
				volume_info.target_rfc1001_name, 16);
			srvTcp->sequence_number = 0;
		}
	}

	if (existingCifsSes) {
		pSesInfo = existingCifsSes;
		cFYI(1, ("Existing smb sess found (status=%d)",
			pSesInfo->status));
		down(&pSesInfo->sesSem);
		if (pSesInfo->status == CifsNeedReconnect) {
			cFYI(1, ("Session needs reconnect"));
			rc = cifs_setup_session(xid, pSesInfo,
						cifs_sb->local_nls);
		}
		up(&pSesInfo->sesSem);
	} else if (!rc) {
		cFYI(1, ("Existing smb sess not found"));
		pSesInfo = sesInfoAlloc();
		if (pSesInfo == NULL)
			rc = -ENOMEM;
		else {
			pSesInfo->server = srvTcp;
			sprintf(pSesInfo->serverName, "%u.%u.%u.%u",
				NIPQUAD(sin_server.sin_addr.s_addr));
		}

		if (!rc) {
			/* volume_info.password freed at unmount */
			if (volume_info.password) {
				pSesInfo->password = volume_info.password;
				/* set to NULL to prevent freeing on exit */
				volume_info.password = NULL;
			}
			if (volume_info.username)
				strncpy(pSesInfo->userName,
					volume_info.username,
					MAX_USERNAME_SIZE);
			if (volume_info.domainname) {
				int len = strlen(volume_info.domainname);
				pSesInfo->domainName =
					kmalloc(len + 1, GFP_KERNEL);
				if (pSesInfo->domainName)
					strcpy(pSesInfo->domainName,
						volume_info.domainname);
			}
			pSesInfo->linux_uid = volume_info.linux_uid;
			pSesInfo->overrideSecFlg = volume_info.secFlg;
			down(&pSesInfo->sesSem);
			/* BB FIXME need to pass vol->secFlgs BB */
			rc = cifs_setup_session(xid, pSesInfo,
						cifs_sb->local_nls);
			up(&pSesInfo->sesSem);
			if (!rc)
				atomic_inc(&srvTcp->socketUseCount);
		}
	}

	/* search for existing tcon to this server share */
	if (!rc) {
		if (volume_info.rsize > CIFSMaxBufSize) {
			cERROR(1, ("rsize %d too large, using MaxBufSize",
				volume_info.rsize));
			cifs_sb->rsize = CIFSMaxBufSize;
		} else if ((volume_info.rsize) &&
				(volume_info.rsize <= CIFSMaxBufSize))
			cifs_sb->rsize = volume_info.rsize;
		else /* default */
			cifs_sb->rsize = CIFSMaxBufSize;

		if (volume_info.wsize > PAGEVEC_SIZE * PAGE_CACHE_SIZE) {
			cERROR(1, ("wsize %d too large, using 4096 instead",
				  volume_info.wsize));
			cifs_sb->wsize = 4096;
		} else if (volume_info.wsize)
			cifs_sb->wsize = volume_info.wsize;
		else
			cifs_sb->wsize =
				min_t(const int, PAGEVEC_SIZE * PAGE_CACHE_SIZE,
					127*1024);
			/* old default of CIFSMaxBufSize was too small now
			   that SMB Write2 can send multiple pages in kvec.
			   RFC1001 does not describe what happens when frame
			   bigger than 128K is sent so use that as max in
			   conjunction with 52K kvec constraint on arch with 4K
			   page size  */

		if (cifs_sb->rsize < 2048) {
			cifs_sb->rsize = 2048;
			/* Windows ME may prefer this */
			cFYI(1, ("readsize set to minimum: 2048"));
		}
		/* calculate prepath */
		cifs_sb->prepath = volume_info.prepath;
		if (cifs_sb->prepath) {
			cifs_sb->prepathlen = strlen(cifs_sb->prepath);
			/* we can not convert the / to \ in the path
			separators in the prefixpath yet because we do not
			know (until reset_cifs_unix_caps is called later)
			whether POSIX PATH CAP is available. We normalize
			the / to \ after reset_cifs_unix_caps is called */
			volume_info.prepath = NULL;
		} else
			cifs_sb->prepathlen = 0;
		cifs_sb->mnt_uid = volume_info.linux_uid;
		cifs_sb->mnt_gid = volume_info.linux_gid;
		cifs_sb->mnt_file_mode = volume_info.file_mode;
		cifs_sb->mnt_dir_mode = volume_info.dir_mode;
		cFYI(1, ("file mode: 0x%x  dir mode: 0x%x",
			cifs_sb->mnt_file_mode, cifs_sb->mnt_dir_mode));

		if (volume_info.noperm)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_PERM;
		if (volume_info.setuids)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SET_UID;
		if (volume_info.server_ino)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SERVER_INUM;
		if (volume_info.remap)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SPECIAL_CHR;
		if (volume_info.no_xattr)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_XATTR;
		if (volume_info.sfu_emul)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UNX_EMUL;
		if (volume_info.nobrl)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_BRL;
		if (volume_info.cifs_acl)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_ACL;
		if (volume_info.override_uid)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_UID;
		if (volume_info.override_gid)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_GID;
		if (volume_info.dynperm)
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DYNPERM;
		if (volume_info.direct_io) {
			cFYI(1, ("mounting share using direct i/o"));
			cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DIRECT_IO;
		}

		if ((volume_info.cifs_acl) && (volume_info.dynperm))
			cERROR(1, ("mount option dynperm ignored if cifsacl "
				   "mount option supported"));

		tcon =
		    find_unc(sin_server.sin_addr.s_addr, volume_info.UNC,
			     volume_info.username);
		if (tcon) {
			cFYI(1, ("Found match on UNC path"));
			/* we can have only one retry value for a connection
			   to a share so for resources mounted more than once
			   to the same server share the last value passed in
			   for the retry flag is used */
			tcon->retry = volume_info.retry;
			tcon->nocase = volume_info.nocase;
			if (tcon->seal != volume_info.seal)
				cERROR(1, ("transport encryption setting "
					   "conflicts with existing tid"));
		} else {
			tcon = tconInfoAlloc();
			if (tcon == NULL)
				rc = -ENOMEM;
			else {
				/* check for null share name ie connecting to
				 * dfs root */

				/* BB check if this works for exactly length
				 * three strings */
				if ((strchr(volume_info.UNC + 3, '\\') == NULL)
				    && (strchr(volume_info.UNC + 3, '/') ==
					NULL)) {
/*					rc = connect_to_dfs_path(xid, pSesInfo,
						"", cifs_sb->local_nls,
						cifs_sb->mnt_cifs_flags &
						  CIFS_MOUNT_MAP_SPECIAL_CHR);*/
					cFYI(1, ("DFS root not supported"));
					rc = -ENODEV;
					goto out;
				} else {
					/* BB Do we need to wrap sesSem around
					 * this TCon call and Unix SetFS as
					 * we do on SessSetup and reconnect? */
					rc = CIFSTCon(xid, pSesInfo,
						volume_info.UNC,
						tcon, cifs_sb->local_nls);
					cFYI(1, ("CIFS Tcon rc = %d", rc));
				}
				if (!rc) {
					atomic_inc(&pSesInfo->inUse);
					tcon->retry = volume_info.retry;
					tcon->nocase = volume_info.nocase;
					tcon->seal = volume_info.seal;
				}
			}
		}
	}
	if (pSesInfo) {
		if (pSesInfo->capabilities & CAP_LARGE_FILES) {
			sb->s_maxbytes = (u64) 1 << 63;
		} else
			sb->s_maxbytes = (u64) 1 << 31;	/* 2 GB */
	}

	/* BB FIXME fix time_gran to be larger for LANMAN sessions */
	sb->s_time_gran = 100;

/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		/* if session setup failed, use count is zero but
		we still need to free cifsd thread */
		if (atomic_read(&srvTcp->socketUseCount) == 0) {
			spin_lock(&GlobalMid_Lock);
			srvTcp->tcpStatus = CifsExiting;
			spin_unlock(&GlobalMid_Lock);
			if (srvTcp->tsk) {
				/* If we could verify that kthread_stop would
				   always wake up processes blocked in
				   tcp in recv_mesg then we could remove the
				   send_sig call */
				force_sig(SIGKILL, srvTcp->tsk);
				kthread_stop(srvTcp->tsk);
			}
		}
		 /* If find_unc succeeded then rc == 0 so we can not end */
		if (tcon)  /* up accidently freeing someone elses tcon struct */
			tconInfoFree(tcon);
		if (existingCifsSes == NULL) {
			if (pSesInfo) {
				if ((pSesInfo->server) &&
				    (pSesInfo->status == CifsGood)) {
					int temp_rc;
					temp_rc = CIFSSMBLogoff(xid, pSesInfo);
					/* if the socketUseCount is now zero */
					if ((temp_rc == -ESHUTDOWN) &&
					    (pSesInfo->server) &&
					    (pSesInfo->server->tsk)) {
						force_sig(SIGKILL,
							pSesInfo->server->tsk);
						kthread_stop(pSesInfo->server->tsk);
					}
				} else {
					cFYI(1, ("No session or bad tcon"));
					if ((pSesInfo->server) &&
					    (pSesInfo->server->tsk)) {
						force_sig(SIGKILL,
							pSesInfo->server->tsk);
						kthread_stop(pSesInfo->server->tsk);
					}
				}
				sesInfoFree(pSesInfo);
				/* pSesInfo = NULL; */
			}
		}
	} else {
		atomic_inc(&tcon->useCount);
		cifs_sb->tcon = tcon;
		tcon->ses = pSesInfo;

		/* do not care if following two calls succeed - informational */
		if (!tcon->ipc) {
			CIFSSMBQFSDeviceInfo(xid, tcon);
			CIFSSMBQFSAttributeInfo(xid, tcon);
		}

		/* tell server which Unix caps we support */
		if (tcon->ses->capabilities & CAP_UNIX)
			/* reset of caps checks mount to see if unix extensions
			   disabled for just this mount */
			reset_cifs_unix_caps(xid, tcon, sb, &volume_info);
		else
			tcon->unix_ext = 0; /* server does not support them */

		/* convert forward to back slashes in prepath here if needed */
		if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) == 0)
			convert_delimiter(cifs_sb->prepath,
					  CIFS_DIR_SEP(cifs_sb));

		if ((tcon->unix_ext == 0) && (cifs_sb->rsize > (1024 * 127))) {
			cifs_sb->rsize = 1024 * 127;
			cFYI(DBG2,
				("no very large read support, rsize now 127K"));
		}
		if (!(tcon->ses->capabilities & CAP_LARGE_WRITE_X))
			cifs_sb->wsize = min(cifs_sb->wsize,
					     (tcon->ses->server->maxBuf -
					      MAX_CIFS_HDR_SIZE));
		if (!(tcon->ses->capabilities & CAP_LARGE_READ_X))
			cifs_sb->rsize = min(cifs_sb->rsize,
					     (tcon->ses->server->maxBuf -
					      MAX_CIFS_HDR_SIZE));
	}

	/* volume_info.password is freed above when existing session found
	(in which case it is not needed anymore) but when new sesion is created
	the password ptr is put in the new session structure (in which case the
	password will be freed at unmount time) */
out:
	/* zero out password before freeing */
	if (volume_info.password != NULL) {
		memset(volume_info.password, 0, strlen(volume_info.password));
		kfree(volume_info.password);
	}
	kfree(volume_info.UNC);
	kfree(volume_info.prepath);
	FreeXid(xid);
	return rc;
}

static int
CIFSSessSetup(unsigned int xid, struct cifsSesInfo *ses,
	      char session_key[CIFS_SESS_KEY_SIZE],
	      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user;
	char *domain;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	__u32 capabilities;
	__u16 count;

	cFYI(1, ("In sesssetup"));
	if (ses == NULL)
		return -EINVAL;
	user = ses->userName;
	domain = ses->domainName;
	smb_buffer = cifs_buf_get();

	if (smb_buffer == NULL)
		return -ENOMEM;

	smb_buffer_response = smb_buffer;
	pSMBr = pSMB = (SESSION_SETUP_ANDX *) smb_buffer;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			NULL /* no tCon exists yet */ , 13 /* wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	pSMB->req_no_secext.AndXCommand = 0xFF;
	pSMB->req_no_secext.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req_no_secext.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if (ses->server->secMode &
			(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
		CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);

	pSMB->req_no_secext.CaseInsensitivePasswordLength =
		cpu_to_le16(CIFS_SESS_KEY_SIZE);

	pSMB->req_no_secext.CaseSensitivePasswordLength =
	    cpu_to_le16(CIFS_SESS_KEY_SIZE);
	bcc_ptr = pByteArea(smb_buffer);
	memcpy(bcc_ptr, (char *) session_key, CIFS_SESS_KEY_SIZE);
	bcc_ptr += CIFS_SESS_KEY_SIZE;
	memcpy(bcc_ptr, (char *) session_key, CIFS_SESS_KEY_SIZE);
	bcc_ptr += CIFS_SESS_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) { /* must be word aligned for Unicode */
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		if (user == NULL)
			bytes_returned = 0; /* skip null user */
		else
			bytes_returned =
				cifs_strtoUCS((__le16 *) bcc_ptr, user, 100,
					nls_codepage);
		/* convert number of 16 bit words to bytes */
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* trailing null */
		if (domain == NULL)
			bytes_returned =
			    cifs_strtoUCS((__le16 *) bcc_ptr,
					  "CIFS_LINUX_DOM", 32, nls_codepage);
		else
			bytes_returned =
			    cifs_strtoUCS((__le16 *) bcc_ptr, domain, 64,
					  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, utsname()->release,
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;
	} else {
		if (user != NULL) {
		    strncpy(bcc_ptr, user, 200);
		    bcc_ptr += strnlen(user, 200);
		}
		*bcc_ptr = 0;
		bcc_ptr++;
		if (domain == NULL) {
			strcpy(bcc_ptr, "CIFS_LINUX_DOM");
			bcc_ptr += strlen("CIFS_LINUX_DOM") + 1;
		} else {
			strncpy(bcc_ptr, domain, 64);
			bcc_ptr += strnlen(domain, 64);
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, utsname()->release);
		bcc_ptr += strlen(utsname()->release) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
	}
	count = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += count;
	pSMB->req_no_secext.ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, CIFS_LONG_OP);
	if (rc) {
/* rc = map_smb_to_linux_error(smb_buffer_response); now done in SendReceive */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		__u16 action = le16_to_cpu(pSMBr->resp.Action);
		__u16 blob_len = le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (action & GUEST_LOGIN)
			cFYI(1, (" Guest login")); /* BB mark SesInfo struct? */
		ses->Suid = smb_buffer_response->Uid; /* UID left in wire format
							 (little endian) */
		cFYI(1, ("UID = %d ", ses->Suid));
	/* response can have either 3 or 4 word count - Samba sends 3 */
		bcc_ptr = pByteArea(smb_buffer_response);
		if ((pSMBr->resp.hdr.WordCount == 3)
		    || ((pSMBr->resp.hdr.WordCount == 4)
			&& (blob_len < pSMBr->resp.ByteCount))) {
			if (pSMBr->resp.hdr.WordCount == 4)
				bcc_ptr += blob_len;

			if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
				if ((long) (bcc_ptr) % 2) {
					remaining_words =
					    (BCC(smb_buffer_response) - 1) / 2;
					/* Unicode strings must be word
					   aligned */
					bcc_ptr++;
				} else {
					remaining_words =
						BCC(smb_buffer_response) / 2;
				}
				len =
				    UniStrnlen((wchar_t *) bcc_ptr,
					       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
				if (ses->serverOS)
					kfree(ses->serverOS);
				ses->serverOS = kzalloc(2 * (len + 1),
							GFP_KERNEL);
				if (ses->serverOS == NULL)
					goto sesssetup_nomem;
				cifs_strfromUCS_le(ses->serverOS,
						   (__le16 *)bcc_ptr,
						   len, nls_codepage);
				bcc_ptr += 2 * (len + 1);
				remaining_words -= len + 1;
				ses->serverOS[2 * len] = 0;
				ses->serverOS[1 + (2 * len)] = 0;
				if (remaining_words > 0) {
					len = UniStrnlen((wchar_t *)bcc_ptr,
							 remaining_words-1);
					kfree(ses->serverNOS);
					ses->serverNOS = kzalloc(2 * (len + 1),
								 GFP_KERNEL);
					if (ses->serverNOS == NULL)
						goto sesssetup_nomem;
					cifs_strfromUCS_le(ses->serverNOS,
							   (__le16 *)bcc_ptr,
							   len, nls_codepage);
					bcc_ptr += 2 * (len + 1);
					ses->serverNOS[2 * len] = 0;
					ses->serverNOS[1 + (2 * len)] = 0;
					if (strncmp(ses->serverNOS,
						"NT LAN Manager 4", 16) == 0) {
						cFYI(1, ("NT4 server"));
						ses->flags |= CIFS_SES_NT4;
					}
					remaining_words -= len + 1;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);
				/* last string is not always null terminated
				   (for e.g. for Windows XP & 2000) */
						if (ses->serverDomain)
							kfree(ses->serverDomain);
						ses->serverDomain =
						    kzalloc(2*(len+1),
							    GFP_KERNEL);
						if (ses->serverDomain == NULL)
							goto sesssetup_nomem;
						cifs_strfromUCS_le(ses->serverDomain,
							(__le16 *)bcc_ptr,
							len, nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverDomain[2*len] = 0;
						ses->serverDomain[1+(2*len)] = 0;
					} else { /* else no more room so create
						  dummy domain string */
						if (ses->serverDomain)
							kfree(ses->serverDomain);
						ses->serverDomain =
							kzalloc(2, GFP_KERNEL);
					}
				} else { /* no room so create dummy domain
					    and NOS string */

					/* if these kcallocs fail not much we
					   can do, but better to not fail the
					   sesssetup itself */
					kfree(ses->serverDomain);
					ses->serverDomain =
					    kzalloc(2, GFP_KERNEL);
					kfree(ses->serverNOS);
					ses->serverNOS =
					    kzalloc(2, GFP_KERNEL);
				}
			} else {	/* ASCII */
				len = strnlen(bcc_ptr, 1024);
				if (((long) bcc_ptr + len) - (long)
				    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
					kfree(ses->serverOS);
					ses->serverOS = kzalloc(len + 1,
								GFP_KERNEL);
					if (ses->serverOS == NULL)
						goto sesssetup_nomem;
					strncpy(ses->serverOS, bcc_ptr, len);

					bcc_ptr += len;
					/* null terminate the string */
					bcc_ptr[0] = 0;
					bcc_ptr++;

					len = strnlen(bcc_ptr, 1024);
					kfree(ses->serverNOS);
					ses->serverNOS = kzalloc(len + 1,
								 GFP_KERNEL);
					if (ses->serverNOS == NULL)
						goto sesssetup_nomem;
					strncpy(ses->serverNOS, bcc_ptr, len);
					bcc_ptr += len;
					bcc_ptr[0] = 0;
					bcc_ptr++;

					len = strnlen(bcc_ptr, 1024);
					if (ses->serverDomain)
						kfree(ses->serverDomain);
					ses->serverDomain = kzalloc(len + 1,
								    GFP_KERNEL);
					if (ses->serverDomain == NULL)
						goto sesssetup_nomem;
					strncpy(ses->serverDomain, bcc_ptr,
						len);
					bcc_ptr += len;
					bcc_ptr[0] = 0;
					bcc_ptr++;
				} else
					cFYI(1,
					     ("Variable field of length %d "
						"extends beyond end of smb ",
					      len));
			}
		} else {
			cERROR(1,
			       (" Security Blob Length extends beyond "
				"end of SMB"));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}
sesssetup_nomem:	/* do not return an error on nomem for the info strings,
			   since that could make reconnection harder, and
			   reconnection might be needed to free memory */
	cifs_buf_release(smb_buffer);

	return rc;
}

static int
CIFSNTLMSSPNegotiateSessSetup(unsigned int xid,
			      struct cifsSesInfo *ses, bool *pNTLMv2_flag,
			      const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *domain;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof(NEGOTIATE_MESSAGE);
	PNEGOTIATE_MESSAGE SecurityBlob;
	PCHALLENGE_MESSAGE SecurityBlob2;
	__u32 negotiate_flags, capabilities;
	__u16 count;

	cFYI(1, ("In NTLMSSP sesssetup (negotiate)"));
	if (ses == NULL)
		return -EINVAL;
	domain = ses->domainName;
	*pNTLMv2_flag = false;
	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *) smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *) smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			NULL /* no tCon exists yet */ , 12 /* wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);

	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	if (ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
	    CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(capabilities);

	bcc_ptr = (char *) &pSMB->req.SecurityBlob;
	SecurityBlob = (PNEGOTIATE_MESSAGE) bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmNegotiate;
	negotiate_flags =
	    NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_NEGOTIATE_OEM |
	    NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_NTLM |
	    NTLMSSP_NEGOTIATE_56 |
	    /* NTLMSSP_NEGOTIATE_ALWAYS_SIGN | */ NTLMSSP_NEGOTIATE_128;
	if (sign_CIFS_PDUs)
		negotiate_flags |= NTLMSSP_NEGOTIATE_SIGN;
/*	if (ntlmv2_support)
		negotiate_flags |= NTLMSSP_NEGOTIATE_NTLMV2;*/
	/* setup pointers to domain name and workstation name */
	bcc_ptr += SecurityBlobLength;

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;

	/* Domain not sent on first Sesssetup in NTLMSSP, instead it is sent
	along with username on auth request (ie the response to challenge) */
	SecurityBlob->DomainName.Buffer = 0;
	SecurityBlob->DomainName.Length = 0;
	SecurityBlob->DomainName.MaximumLength = 0;
	if (ses->capabilities & CAP_UNICODE) {
		if ((long) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}

		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, utsname()->release, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null terminate Linux version */
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, utsname()->release);
		bcc_ptr += strlen(utsname()->release) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* empty domain field */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags = cpu_to_le32(negotiate_flags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	count = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += count;
	pSMB->req.ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, CIFS_LONG_OP);

	if (smb_buffer_response->Status.CifsError ==
	    cpu_to_le32(NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc) {
/*    rc = map_smb_to_linux_error(smb_buffer_response);  *//* done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3)
		   || (smb_buffer_response->WordCount == 4)) {
		__u16 action = le16_to_cpu(pSMBr->resp.Action);
		__u16 blob_len = le16_to_cpu(pSMBr->resp.SecurityBlobLength);

		if (action & GUEST_LOGIN)
			cFYI(1, (" Guest login"));
	/* Do we want to set anything in SesInfo struct when guest login? */

		bcc_ptr = pByteArea(smb_buffer_response);
	/* response can have either 3 or 4 word count - Samba sends 3 */

		SecurityBlob2 = (PCHALLENGE_MESSAGE) bcc_ptr;
		if (SecurityBlob2->MessageType != NtLmChallenge) {
			cFYI(1,
			     ("Unexpected NTLMSSP message type received %d",
			      SecurityBlob2->MessageType));
		} else if (ses) {
			ses->Suid = smb_buffer_response->Uid; /* UID left in le format */
			cFYI(1, ("UID = %d", ses->Suid));
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (blob_len <
				    pSMBr->resp.ByteCount))) {

				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr += blob_len;
					cFYI(1, ("Security Blob Length %d",
					      blob_len));
				}

				cFYI(1, ("NTLMSSP Challenge rcvd"));

				memcpy(ses->server->cryptKey,
				       SecurityBlob2->Challenge,
				       CIFS_CRYPTO_KEY_SIZE);
				if (SecurityBlob2->NegotiateFlags &
					cpu_to_le32(NTLMSSP_NEGOTIATE_NTLMV2))
					*pNTLMv2_flag = true;

				if ((SecurityBlob2->NegotiateFlags &
					cpu_to_le32(NTLMSSP_NEGOTIATE_ALWAYS_SIGN))
					|| (sign_CIFS_PDUs > 1))
						ses->server->secMode |=
							SECMODE_SIGN_REQUIRED;
				if ((SecurityBlob2->NegotiateFlags &
					cpu_to_le32(NTLMSSP_NEGOTIATE_SIGN)) && (sign_CIFS_PDUs))
						ses->server->secMode |=
							SECMODE_SIGN_ENABLED;

				if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
					 /* Must word align unicode strings */
						bcc_ptr++;
					} else {
						remaining_words =
						    BCC
						    (smb_buffer_response) / 2;
					}
					len =
					    UniStrnlen((wchar_t *) bcc_ptr,
						       remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
					if (ses->serverOS)
						kfree(ses->serverOS);
					ses->serverOS =
					    kzalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (__le16 *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (__le16 *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1 +
							       (2 * len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);
				/* last string not always null terminated
				   (for e.g. for Windows XP & 2000) */
							kfree(ses->serverDomain);
							ses->serverDomain =
							    kzalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->serverDomain,
							     (__le16 *)bcc_ptr,
							     len, nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->serverDomain[2*len]
							    = 0;
							ses->serverDomain
								[1 + (2 * len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else {
							kfree(ses->serverDomain);
							ses->serverDomain =
							    kzalloc(2,
								    GFP_KERNEL);
						}
					} else {	/* no room so create dummy domain and NOS string */
						kfree(ses->serverDomain);
						ses->serverDomain =
						    kzalloc(2, GFP_KERNEL);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) - (long)
					    pByteArea(smb_buffer_response)
					    <= BCC(smb_buffer_response)) {
						if (ses->serverOS)
							kfree(ses->serverOS);
						ses->serverOS =
						    kzalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverOS,
							bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverNOS, bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverDomain);
						ses->serverDomain =
						    kzalloc(len + 1,
							    GFP_KERNEL);
						strncpy(ses->serverDomain,
							bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1,
						     ("field of length %d "
						    "extends beyond end of smb",
						      len));
				}
			} else {
				cERROR(1, ("Security Blob Length extends beyond"
					   " end of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1,
		       (" Invalid Word count %d:",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	cifs_buf_release(smb_buffer);

	return rc;
}
static int
CIFSNTLMSSPAuthSessSetup(unsigned int xid, struct cifsSesInfo *ses,
			char *ntlm_session_key, bool ntlmv2_flag,
			const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	SESSION_SETUP_ANDX *pSMB;
	SESSION_SETUP_ANDX *pSMBr;
	char *bcc_ptr;
	char *user;
	char *domain;
	int rc = 0;
	int remaining_words = 0;
	int bytes_returned = 0;
	int len;
	int SecurityBlobLength = sizeof(AUTHENTICATE_MESSAGE);
	PAUTHENTICATE_MESSAGE SecurityBlob;
	__u32 negotiate_flags, capabilities;
	__u16 count;

	cFYI(1, ("In NTLMSSPSessSetup (Authenticate)"));
	if (ses == NULL)
		return -EINVAL;
	user = ses->userName;
	domain = ses->domainName;
	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL) {
		return -ENOMEM;
	}
	smb_buffer_response = smb_buffer;
	pSMB = (SESSION_SETUP_ANDX *)smb_buffer;
	pSMBr = (SESSION_SETUP_ANDX *)smb_buffer_response;

	/* send SMBsessionSetup here */
	header_assemble(smb_buffer, SMB_COM_SESSION_SETUP_ANDX,
			NULL /* no tCon exists yet */ , 12 /* wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	pSMB->req.hdr.Flags |= (SMBFLG_CASELESS | SMBFLG_CANONICAL_PATH_FORMAT);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	pSMB->req.hdr.Uid = ses->Suid;

	if (ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_EXTENDED_SECURITY;
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	pSMB->req.Capabilities = cpu_to_le32(capabilities);

	bcc_ptr = (char *)&pSMB->req.SecurityBlob;
	SecurityBlob = (PAUTHENTICATE_MESSAGE)bcc_ptr;
	strncpy(SecurityBlob->Signature, NTLMSSP_SIGNATURE, 8);
	SecurityBlob->MessageType = NtLmAuthenticate;
	bcc_ptr += SecurityBlobLength;
	negotiate_flags = NTLMSSP_NEGOTIATE_UNICODE | NTLMSSP_REQUEST_TARGET |
			NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_NEGOTIATE_TARGET_INFO |
			0x80000000 | NTLMSSP_NEGOTIATE_128;
	if (sign_CIFS_PDUs)
		negotiate_flags |= /* NTLMSSP_NEGOTIATE_ALWAYS_SIGN |*/ NTLMSSP_NEGOTIATE_SIGN;
	if (ntlmv2_flag)
		negotiate_flags |= NTLMSSP_NEGOTIATE_NTLMV2;

/* setup pointers to domain name and workstation name */

	SecurityBlob->WorkstationName.Buffer = 0;
	SecurityBlob->WorkstationName.Length = 0;
	SecurityBlob->WorkstationName.MaximumLength = 0;
	SecurityBlob->SessionKey.Length = 0;
	SecurityBlob->SessionKey.MaximumLength = 0;
	SecurityBlob->SessionKey.Buffer = 0;

	SecurityBlob->LmChallengeResponse.Length = 0;
	SecurityBlob->LmChallengeResponse.MaximumLength = 0;
	SecurityBlob->LmChallengeResponse.Buffer = 0;

	SecurityBlob->NtChallengeResponse.Length =
	    cpu_to_le16(CIFS_SESS_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.MaximumLength =
	    cpu_to_le16(CIFS_SESS_KEY_SIZE);
	memcpy(bcc_ptr, ntlm_session_key, CIFS_SESS_KEY_SIZE);
	SecurityBlob->NtChallengeResponse.Buffer =
	    cpu_to_le32(SecurityBlobLength);
	SecurityBlobLength += CIFS_SESS_KEY_SIZE;
	bcc_ptr += CIFS_SESS_KEY_SIZE;

	if (ses->capabilities & CAP_UNICODE) {
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			__u16 ln = cifs_strtoUCS((__le16 *) bcc_ptr, domain, 64,
					  nls_codepage);
			ln *= 2;
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(ln);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->DomainName.Length = cpu_to_le16(ln);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			__u16 ln = cifs_strtoUCS((__le16 *) bcc_ptr, user, 64,
					  nls_codepage);
			ln *= 2;
			SecurityBlob->UserName.MaximumLength =
			    cpu_to_le16(ln);
			SecurityBlob->UserName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->UserName.Length = cpu_to_le16(ln);
		}

		/* SecurityBlob->WorkstationName.Length =
		 cifs_strtoUCS((__le16 *) bcc_ptr, "AMACHINE",64, nls_codepage);
		   SecurityBlob->WorkstationName.Length *= 2;
		   SecurityBlob->WorkstationName.MaximumLength =
			cpu_to_le16(SecurityBlob->WorkstationName.Length);
		   SecurityBlob->WorkstationName.Buffer =
				 cpu_to_le32(SecurityBlobLength);
		   bcc_ptr += SecurityBlob->WorkstationName.Length;
		   SecurityBlobLength += SecurityBlob->WorkstationName.Length;
		   SecurityBlob->WorkstationName.Length =
			cpu_to_le16(SecurityBlob->WorkstationName.Length);  */

		if ((long) bcc_ptr % 2) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, "Linux version ",
				  32, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, utsname()->release, 32,
				  nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		bcc_ptr += 2;	/* null term version string */
		bytes_returned =
		    cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  64, nls_codepage);
		bcc_ptr += 2 * bytes_returned;
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null terminate network opsys string */
		*(bcc_ptr + 1) = 0;
		*(bcc_ptr + 2) = 0;
		bcc_ptr += 2;	/* null domain */
	} else {		/* ASCII */
		if (domain == NULL) {
			SecurityBlob->DomainName.Buffer = 0;
			SecurityBlob->DomainName.Length = 0;
			SecurityBlob->DomainName.MaximumLength = 0;
		} else {
			__u16 ln;
			negotiate_flags |= NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED;
			strncpy(bcc_ptr, domain, 63);
			ln = strnlen(domain, 64);
			SecurityBlob->DomainName.MaximumLength =
			    cpu_to_le16(ln);
			SecurityBlob->DomainName.Buffer =
			    cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->DomainName.Length = cpu_to_le16(ln);
		}
		if (user == NULL) {
			SecurityBlob->UserName.Buffer = 0;
			SecurityBlob->UserName.Length = 0;
			SecurityBlob->UserName.MaximumLength = 0;
		} else {
			__u16 ln;
			strncpy(bcc_ptr, user, 63);
			ln = strnlen(user, 64);
			SecurityBlob->UserName.MaximumLength = cpu_to_le16(ln);
			SecurityBlob->UserName.Buffer =
						cpu_to_le32(SecurityBlobLength);
			bcc_ptr += ln;
			SecurityBlobLength += ln;
			SecurityBlob->UserName.Length = cpu_to_le16(ln);
		}
		/* BB fill in our workstation name if known BB */

		strcpy(bcc_ptr, "Linux version ");
		bcc_ptr += strlen("Linux version ");
		strcpy(bcc_ptr, utsname()->release);
		bcc_ptr += strlen(utsname()->release) + 1;
		strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
		bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;
		bcc_ptr++;	/* null domain */
		*bcc_ptr = 0;
	}
	SecurityBlob->NegotiateFlags = cpu_to_le32(negotiate_flags);
	pSMB->req.SecurityBlobLength = cpu_to_le16(SecurityBlobLength);
	count = (long) bcc_ptr - (long) pByteArea(smb_buffer);
	smb_buffer->smb_buf_length += count;
	pSMB->req.ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response,
			 &bytes_returned, CIFS_LONG_OP);
	if (rc) {
/*   rc = map_smb_to_linux_error(smb_buffer_response) done in SendReceive now */
	} else if ((smb_buffer_response->WordCount == 3) ||
		   (smb_buffer_response->WordCount == 4)) {
		__u16 action = le16_to_cpu(pSMBr->resp.Action);
		__u16 blob_len = le16_to_cpu(pSMBr->resp.SecurityBlobLength);
		if (action & GUEST_LOGIN)
			cFYI(1, (" Guest login")); /* BB Should we set anything
							 in SesInfo struct ? */
/*		if (SecurityBlob2->MessageType != NtLm??) {
			cFYI("Unexpected message type on auth response is %d"));
		} */

		if (ses) {
			cFYI(1,
			     ("Check challenge UID %d vs auth response UID %d",
			      ses->Suid, smb_buffer_response->Uid));
			/* UID left in wire format */
			ses->Suid = smb_buffer_response->Uid;
			bcc_ptr = pByteArea(smb_buffer_response);
		/* response can have either 3 or 4 word count - Samba sends 3 */
			if ((pSMBr->resp.hdr.WordCount == 3)
			    || ((pSMBr->resp.hdr.WordCount == 4)
				&& (blob_len <
				    pSMBr->resp.ByteCount))) {
				if (pSMBr->resp.hdr.WordCount == 4) {
					bcc_ptr +=
					    blob_len;
					cFYI(1,
					     ("Security Blob Length %d ",
					      blob_len));
				}

				cFYI(1,
				     ("NTLMSSP response to Authenticate "));

				if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
					if ((long) (bcc_ptr) % 2) {
						remaining_words =
						    (BCC(smb_buffer_response)
						     - 1) / 2;
						bcc_ptr++;	/* Unicode strings must be word aligned */
					} else {
						remaining_words = BCC(smb_buffer_response) / 2;
					}
					len = UniStrnlen((wchar_t *) bcc_ptr,
							remaining_words - 1);
/* We look for obvious messed up bcc or strings in response so we do not go off
  the end since (at least) WIN2K and Windows XP have a major bug in not null
  terminating last Unicode string in response  */
					if (ses->serverOS)
						kfree(ses->serverOS);
					ses->serverOS =
					    kzalloc(2 * (len + 1), GFP_KERNEL);
					cifs_strfromUCS_le(ses->serverOS,
							   (__le16 *)
							   bcc_ptr, len,
							   nls_codepage);
					bcc_ptr += 2 * (len + 1);
					remaining_words -= len + 1;
					ses->serverOS[2 * len] = 0;
					ses->serverOS[1 + (2 * len)] = 0;
					if (remaining_words > 0) {
						len = UniStrnlen((wchar_t *)
								 bcc_ptr,
								 remaining_words
								 - 1);
						kfree(ses->serverNOS);
						ses->serverNOS =
						    kzalloc(2 * (len + 1),
							    GFP_KERNEL);
						cifs_strfromUCS_le(ses->
								   serverNOS,
								   (__le16 *)
								   bcc_ptr,
								   len,
								   nls_codepage);
						bcc_ptr += 2 * (len + 1);
						ses->serverNOS[2 * len] = 0;
						ses->serverNOS[1+(2*len)] = 0;
						remaining_words -= len + 1;
						if (remaining_words > 0) {
							len = UniStrnlen((wchar_t *) bcc_ptr, remaining_words);
     /* last string not always null terminated (e.g. for Windows XP & 2000) */
							if (ses->serverDomain)
								kfree(ses->serverDomain);
							ses->serverDomain =
							    kzalloc(2 *
								    (len +
								     1),
								    GFP_KERNEL);
							cifs_strfromUCS_le
							    (ses->
							     serverDomain,
							     (__le16 *)
							     bcc_ptr, len,
							     nls_codepage);
							bcc_ptr +=
							    2 * (len + 1);
							ses->
							    serverDomain[2
									 * len]
							    = 0;
							ses->
							    serverDomain[1
									 +
									 (2
									  *
									  len)]
							    = 0;
						} /* else no more room so create dummy domain string */
						else {
							if (ses->serverDomain)
								kfree(ses->serverDomain);
							ses->serverDomain = kzalloc(2,GFP_KERNEL);
						}
					} else {  /* no room so create dummy domain and NOS string */
						if (ses->serverDomain)
							kfree(ses->serverDomain);
						ses->serverDomain = kzalloc(2, GFP_KERNEL);
						kfree(ses->serverNOS);
						ses->serverNOS = kzalloc(2, GFP_KERNEL);
					}
				} else {	/* ASCII */
					len = strnlen(bcc_ptr, 1024);
					if (((long) bcc_ptr + len) -
					   (long) pByteArea(smb_buffer_response)
						<= BCC(smb_buffer_response)) {
						if (ses->serverOS)
							kfree(ses->serverOS);
						ses->serverOS = kzalloc(len + 1, GFP_KERNEL);
						strncpy(ses->serverOS,bcc_ptr, len);

						bcc_ptr += len;
						bcc_ptr[0] = 0;	/* null terminate the string */
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						kfree(ses->serverNOS);
						ses->serverNOS = kzalloc(len+1,
								    GFP_KERNEL);
						strncpy(ses->serverNOS,
							bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;

						len = strnlen(bcc_ptr, 1024);
						if (ses->serverDomain)
							kfree(ses->serverDomain);
						ses->serverDomain =
								kzalloc(len+1,
								    GFP_KERNEL);
						strncpy(ses->serverDomain,
							bcc_ptr, len);
						bcc_ptr += len;
						bcc_ptr[0] = 0;
						bcc_ptr++;
					} else
						cFYI(1, ("field of length %d "
						   "extends beyond end of smb ",
						      len));
				}
			} else {
				cERROR(1, ("Security Blob extends beyond end "
					"of SMB"));
			}
		} else {
			cERROR(1, ("No session structure passed in."));
		}
	} else {
		cERROR(1, ("Invalid Word count %d: ",
			smb_buffer_response->WordCount));
		rc = -EIO;
	}

	cifs_buf_release(smb_buffer);

	return rc;
}

int
CIFSTCon(unsigned int xid, struct cifsSesInfo *ses,
	 const char *tree, struct cifsTconInfo *tcon,
void cifs_setup_cifs_sb(struct smb_vol *pvolume_info,
			struct cifs_sb_info *cifs_sb)
{
	INIT_DELAYED_WORK(&cifs_sb->prune_tlinks, cifs_prune_tlinks);

	spin_lock_init(&cifs_sb->tlink_tree_lock);
	cifs_sb->tlink_tree = RB_ROOT;

	/*
	 * Temporarily set r/wsize for matching superblock. If we end up using
	 * new sb then client will later negotiate it downward if needed.
	 */
	cifs_sb->rsize = pvolume_info->rsize;
	cifs_sb->wsize = pvolume_info->wsize;

	cifs_sb->mnt_uid = pvolume_info->linux_uid;
	cifs_sb->mnt_gid = pvolume_info->linux_gid;
	cifs_sb->mnt_file_mode = pvolume_info->file_mode;
	cifs_sb->mnt_dir_mode = pvolume_info->dir_mode;
	cifs_dbg(FYI, "file mode: %04ho  dir mode: %04ho\n",
		 cifs_sb->mnt_file_mode, cifs_sb->mnt_dir_mode);

	cifs_sb->actimeo = pvolume_info->actimeo;
	cifs_sb->local_nls = pvolume_info->local_nls;

	if (pvolume_info->noperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_PERM;
	if (pvolume_info->setuids)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SET_UID;
	if (pvolume_info->server_ino)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_SERVER_INUM;
	if (pvolume_info->remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SFM_CHR;
	if (pvolume_info->sfu_remap)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MAP_SPECIAL_CHR;
	if (pvolume_info->no_xattr)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_XATTR;
	if (pvolume_info->sfu_emul)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_UNX_EMUL;
	if (pvolume_info->nobrl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NO_BRL;
	if (pvolume_info->nostrictsync)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOSSYNC;
	if (pvolume_info->mand_lock)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_NOPOSIXBRL;
	if (pvolume_info->rwpidforward)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_RWPIDFORWARD;
	if (pvolume_info->cifs_acl)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_ACL;
	if (pvolume_info->backupuid_specified) {
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_BACKUPUID;
		cifs_sb->mnt_backupuid = pvolume_info->backupuid;
	}
	if (pvolume_info->backupgid_specified) {
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_CIFS_BACKUPGID;
		cifs_sb->mnt_backupgid = pvolume_info->backupgid;
	}
	if (pvolume_info->override_uid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_UID;
	if (pvolume_info->override_gid)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_OVERR_GID;
	if (pvolume_info->dynperm)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DYNPERM;
	if (pvolume_info->fsc)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_FSCACHE;
	if (pvolume_info->multiuser)
		cifs_sb->mnt_cifs_flags |= (CIFS_MOUNT_MULTIUSER |
					    CIFS_MOUNT_NO_PERM);
	if (pvolume_info->strict_io)
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_STRICT_IO;
	if (pvolume_info->direct_io) {
		cifs_dbg(FYI, "mounting share using direct i/o\n");
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_DIRECT_IO;
	}
	if (pvolume_info->mfsymlinks) {
		if (pvolume_info->sfu_emul) {
			/*
			 * Our SFU ("Services for Unix" emulation does not allow
			 * creating symlinks but does allow reading existing SFU
			 * symlinks (it does allow both creating and reading SFU
			 * style mknod and FIFOs though). When "mfsymlinks" and
			 * "sfu" are both enabled at the same time, it allows
			 * reading both types of symlinks, but will only create
			 * them with mfsymlinks format. This allows better
			 * Apple compatibility (probably better for Samba too)
			 * while still recognizing old Windows style symlinks.
			 */
			cifs_dbg(VFS, "mount options mfsymlinks and sfu both enabled\n");
		}
		cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_MF_SYMLINKS;
	}

	if ((pvolume_info->cifs_acl) && (pvolume_info->dynperm))
		cifs_dbg(VFS, "mount option dynperm ignored if cifsacl mount option supported\n");
}

static void
cleanup_volume_info_contents(struct smb_vol *volume_info)
{
	kfree(volume_info->username);
	kzfree(volume_info->password);
	kfree(volume_info->UNC);
	kfree(volume_info->domainname);
	kfree(volume_info->iocharset);
	kfree(volume_info->prepath);
}

void
cifs_cleanup_volume_info(struct smb_vol *volume_info)
{
	if (!volume_info)
		return;
	cleanup_volume_info_contents(volume_info);
	kfree(volume_info);
}


#ifdef CONFIG_CIFS_DFS_UPCALL
/*
 * cifs_build_path_to_root returns full path to root when we do not have an
 * exiting connection (tcon)
 */
static char *
build_unc_path_to_root(const struct smb_vol *vol,
		const struct cifs_sb_info *cifs_sb)
{
	char *full_path, *pos;
	unsigned int pplen = vol->prepath ? strlen(vol->prepath) + 1 : 0;
	unsigned int unc_len = strnlen(vol->UNC, MAX_TREE_SIZE + 1);

	full_path = kmalloc(unc_len + pplen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	strncpy(full_path, vol->UNC, unc_len);
	pos = full_path + unc_len;

	if (pplen) {
		*pos = CIFS_DIR_SEP(cifs_sb);
		strncpy(pos + 1, vol->prepath, pplen);
		pos += pplen;
	}

	*pos = '\0'; /* add trailing null */
	convert_delimiter(full_path, CIFS_DIR_SEP(cifs_sb));
	cifs_dbg(FYI, "%s: full_path=%s\n", __func__, full_path);
	return full_path;
}

/*
 * Perform a dfs referral query for a share and (optionally) prefix
 *
 * If a referral is found, cifs_sb->mountdata will be (re-)allocated
 * to a string containing updated options for the submount.  Otherwise it
 * will be left untouched.
 *
 * Returns the rc from get_dfs_path to the caller, which can be used to
 * determine whether there were referrals.
 */
static int
expand_dfs_referral(const unsigned int xid, struct cifs_ses *ses,
		    struct smb_vol *volume_info, struct cifs_sb_info *cifs_sb,
		    int check_prefix)
{
	int rc;
	unsigned int num_referrals = 0;
	struct dfs_info3_param *referrals = NULL;
	char *full_path = NULL, *ref_path = NULL, *mdata = NULL;

	full_path = build_unc_path_to_root(volume_info, cifs_sb);
	if (IS_ERR(full_path))
		return PTR_ERR(full_path);

	/* For DFS paths, skip the first '\' of the UNC */
	ref_path = check_prefix ? full_path + 1 : volume_info->UNC + 1;

	rc = get_dfs_path(xid, ses, ref_path, cifs_sb->local_nls,
			  &num_referrals, &referrals, cifs_remap(cifs_sb));

	if (!rc && num_referrals > 0) {
		char *fake_devname = NULL;

		mdata = cifs_compose_mount_options(cifs_sb->mountdata,
						   full_path + 1, referrals,
						   &fake_devname);

		free_dfs_info_array(referrals, num_referrals);

		if (IS_ERR(mdata)) {
			rc = PTR_ERR(mdata);
			mdata = NULL;
		} else {
			cleanup_volume_info_contents(volume_info);
			rc = cifs_setup_volume_info(volume_info, mdata,
							fake_devname);
		}
		kfree(fake_devname);
		kfree(cifs_sb->mountdata);
		cifs_sb->mountdata = mdata;
	}
	kfree(full_path);
	return rc;
}
#endif

static int
cifs_setup_volume_info(struct smb_vol *volume_info, char *mount_data,
			const char *devname)
{
	int rc = 0;

	if (cifs_parse_mount_options(mount_data, devname, volume_info))
		return -EINVAL;

	if (volume_info->nullauth) {
		cifs_dbg(FYI, "Anonymous login\n");
		kfree(volume_info->username);
		volume_info->username = NULL;
	} else if (volume_info->username) {
		/* BB fixme parse for domain name here */
		cifs_dbg(FYI, "Username: %s\n", volume_info->username);
	} else {
		cifs_dbg(VFS, "No username specified\n");
	/* In userspace mount helper we can get user name from alternate
	   locations such as env variables and files on disk */
		return -EINVAL;
	}

	/* this is needed for ASCII cp to Unicode converts */
	if (volume_info->iocharset == NULL) {
		/* load_nls_default cannot return null */
		volume_info->local_nls = load_nls_default();
	} else {
		volume_info->local_nls = load_nls(volume_info->iocharset);
		if (volume_info->local_nls == NULL) {
			cifs_dbg(VFS, "CIFS mount error: iocharset %s not found\n",
				 volume_info->iocharset);
			return -ELIBACC;
		}
	}

	return rc;
}

struct smb_vol *
cifs_get_volume_info(char *mount_data, const char *devname)
{
	int rc;
	struct smb_vol *volume_info;

	volume_info = kmalloc(sizeof(struct smb_vol), GFP_KERNEL);
	if (!volume_info)
		return ERR_PTR(-ENOMEM);

	rc = cifs_setup_volume_info(volume_info, mount_data, devname);
	if (rc) {
		cifs_cleanup_volume_info(volume_info);
		volume_info = ERR_PTR(rc);
	}

	return volume_info;
}

static int
cifs_are_all_path_components_accessible(struct TCP_Server_Info *server,
					unsigned int xid,
					struct cifs_tcon *tcon,
					struct cifs_sb_info *cifs_sb,
					char *full_path)
{
	int rc;
	char *s;
	char sep, tmp;

	sep = CIFS_DIR_SEP(cifs_sb);
	s = full_path;

	rc = server->ops->is_path_accessible(xid, tcon, cifs_sb, "");
	while (rc == 0) {
		/* skip separators */
		while (*s == sep)
			s++;
		if (!*s)
			break;
		/* next separator */
		while (*s && *s != sep)
			s++;

		/*
		 * temporarily null-terminate the path at the end of
		 * the current component
		 */
		tmp = *s;
		*s = 0;
		rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
						     full_path);
		*s = tmp;
	}
	return rc;
}

int
cifs_mount(struct cifs_sb_info *cifs_sb, struct smb_vol *volume_info)
{
	int rc;
	unsigned int xid;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char   *full_path;
	struct tcon_link *tlink;
#ifdef CONFIG_CIFS_DFS_UPCALL
	int referral_walks_count = 0;
#endif

	rc = bdi_setup_and_register(&cifs_sb->bdi, "cifs");
	if (rc)
		return rc;

#ifdef CONFIG_CIFS_DFS_UPCALL
try_mount_again:
	/* cleanup activities if we're chasing a referral */
	if (referral_walks_count) {
		if (tcon)
			cifs_put_tcon(tcon);
		else if (ses)
			cifs_put_smb_ses(ses);

		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_POSIX_PATHS;

		free_xid(xid);
	}
#endif
	rc = 0;
	tcon = NULL;
	ses = NULL;
	server = NULL;
	full_path = NULL;
	tlink = NULL;

	xid = get_xid();

	/* get a reference to a tcp session */
	server = cifs_get_tcp_session(volume_info);
	if (IS_ERR(server)) {
		rc = PTR_ERR(server);
		bdi_destroy(&cifs_sb->bdi);
		goto out;
	}

	/* get a reference to a SMB session */
	ses = cifs_get_smb_ses(server, volume_info);
	if (IS_ERR(ses)) {
		rc = PTR_ERR(ses);
		ses = NULL;
		goto mount_fail_check;
	}

#ifdef CONFIG_CIFS_SMB2
	if ((volume_info->persistent == true) && ((ses->server->capabilities &
		SMB2_GLOBAL_CAP_PERSISTENT_HANDLES) == 0)) {
		cifs_dbg(VFS, "persistent handles not supported by server\n");
		rc = -EOPNOTSUPP;
		goto mount_fail_check;
	}
#endif /* CONFIG_CIFS_SMB2*/

	/* search for existing tcon to this server share */
	tcon = cifs_get_tcon(ses, volume_info);
	if (IS_ERR(tcon)) {
		rc = PTR_ERR(tcon);
		tcon = NULL;
		if (rc == -EACCES)
			goto mount_fail_check;

		goto remote_path_check;
	}

	/* tell server which Unix caps we support */
	if (cap_unix(tcon->ses)) {
		/* reset of caps checks mount to see if unix extensions
		   disabled for just this mount */
		reset_cifs_unix_caps(xid, tcon, cifs_sb, volume_info);
		if ((tcon->ses->server->tcpStatus == CifsNeedReconnect) &&
		    (le64_to_cpu(tcon->fsUnixInfo.Capability) &
		     CIFS_UNIX_TRANSPORT_ENCRYPTION_MANDATORY_CAP)) {
			rc = -EACCES;
			goto mount_fail_check;
		}
	} else
		tcon->unix_ext = 0; /* server does not support them */

	/* do not care if a following call succeed - informational */
	if (!tcon->ipc && server->ops->qfs_tcon)
		server->ops->qfs_tcon(xid, tcon);

	cifs_sb->wsize = server->ops->negotiate_wsize(tcon, volume_info);
	cifs_sb->rsize = server->ops->negotiate_rsize(tcon, volume_info);

	/* tune readahead according to rsize */
	cifs_sb->bdi.ra_pages = cifs_sb->rsize / PAGE_CACHE_SIZE;

remote_path_check:
#ifdef CONFIG_CIFS_DFS_UPCALL
	/*
	 * Perform an unconditional check for whether there are DFS
	 * referrals for this path without prefix, to provide support
	 * for DFS referrals from w2k8 servers which don't seem to respond
	 * with PATH_NOT_COVERED to requests that include the prefix.
	 * Chase the referral if found, otherwise continue normally.
	 */
	if (referral_walks_count == 0) {
		int refrc = expand_dfs_referral(xid, ses, volume_info, cifs_sb,
						false);
		if (!refrc) {
			referral_walks_count++;
			goto try_mount_again;
		}
	}
#endif

	/* check if a whole path is not remote */
	if (!rc && tcon) {
		if (!server->ops->is_path_accessible) {
			rc = -ENOSYS;
			goto mount_fail_check;
		}
		/*
		 * cifs_build_path_to_root works only when we have a valid tcon
		 */
		full_path = cifs_build_path_to_root(volume_info, cifs_sb, tcon);
		if (full_path == NULL) {
			rc = -ENOMEM;
			goto mount_fail_check;
		}
		rc = server->ops->is_path_accessible(xid, tcon, cifs_sb,
						     full_path);
		if (rc != 0 && rc != -EREMOTE) {
			kfree(full_path);
			goto mount_fail_check;
		}

		if (rc != -EREMOTE) {
			rc = cifs_are_all_path_components_accessible(server,
							     xid, tcon, cifs_sb,
							     full_path);
			if (rc != 0) {
				cifs_dbg(VFS, "cannot query dirs between root and final path, "
					 "enabling CIFS_MOUNT_USE_PREFIX_PATH\n");
				cifs_sb->mnt_cifs_flags |= CIFS_MOUNT_USE_PREFIX_PATH;
				rc = 0;
			}
		}
		kfree(full_path);
	}

	/* get referral if needed */
	if (rc == -EREMOTE) {
#ifdef CONFIG_CIFS_DFS_UPCALL
		if (referral_walks_count > MAX_NESTED_LINKS) {
			/*
			 * BB: when we implement proper loop detection,
			 *     we will remove this check. But now we need it
			 *     to prevent an indefinite loop if 'DFS tree' is
			 *     misconfigured (i.e. has loops).
			 */
			rc = -ELOOP;
			goto mount_fail_check;
		}

		rc = expand_dfs_referral(xid, ses, volume_info, cifs_sb, true);

		if (!rc) {
			referral_walks_count++;
			goto try_mount_again;
		}
		goto mount_fail_check;
#else /* No DFS support, return error on mount */
		rc = -EOPNOTSUPP;
#endif
	}

	if (rc)
		goto mount_fail_check;

	/* now, hang the tcon off of the superblock */
	tlink = kzalloc(sizeof *tlink, GFP_KERNEL);
	if (tlink == NULL) {
		rc = -ENOMEM;
		goto mount_fail_check;
	}

	tlink->tl_uid = ses->linux_uid;
	tlink->tl_tcon = tcon;
	tlink->tl_time = jiffies;
	set_bit(TCON_LINK_MASTER, &tlink->tl_flags);
	set_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);

	cifs_sb->master_tlink = tlink;
	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(cifsiod_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);

mount_fail_check:
	/* on error free sesinfo and tcon struct if needed */
	if (rc) {
		/* If find_unc succeeded then rc == 0 so we can not end */
		/* up accidentally freeing someone elses tcon struct */
		if (tcon)
			cifs_put_tcon(tcon);
		else if (ses)
			cifs_put_smb_ses(ses);
		else
			cifs_put_tcp_session(server, 0);
		bdi_destroy(&cifs_sb->bdi);
	}

out:
	free_xid(xid);
	return rc;
}

/*
 * Issue a TREE_CONNECT request. Note that for IPC$ shares, that the tcon
 * pointer may be NULL.
 */
int
CIFSTCon(const unsigned int xid, struct cifs_ses *ses,
	 const char *tree, struct cifs_tcon *tcon,
	 const struct nls_table *nls_codepage)
{
	struct smb_hdr *smb_buffer;
	struct smb_hdr *smb_buffer_response;
	TCONX_REQ *pSMB;
	TCONX_RSP *pSMBr;
	unsigned char *bcc_ptr;
	int rc = 0;
	int length;
	__u16 count;
	__u16 bytes_left, count;

	if (ses == NULL)
		return -EIO;

	smb_buffer = cifs_buf_get();
	if (smb_buffer == NULL) {
		return -ENOMEM;
	}
	if (smb_buffer == NULL)
		return -ENOMEM;

	smb_buffer_response = smb_buffer;

	header_assemble(smb_buffer, SMB_COM_TREE_CONNECT_ANDX,
			NULL /*no tid */ , 4 /*wct */ );

	smb_buffer->Mid = GetNextMid(ses->server);
	smb_buffer->Mid = get_next_mid(ses->server);
	smb_buffer->Uid = ses->Suid;
	pSMB = (TCONX_REQ *) smb_buffer;
	pSMBr = (TCONX_RSP *) smb_buffer_response;

	pSMB->AndXCommand = 0xFF;
	pSMB->Flags = cpu_to_le16(TCON_EXTENDED_SECINFO);
	bcc_ptr = &pSMB->Password[0];
	if ((ses->server->secMode) & SECMODE_USER) {
	if (!tcon || (ses->server->sec_mode & SECMODE_USER)) {
		pSMB->PasswordLength = cpu_to_le16(1);	/* minimum */
		*bcc_ptr = 0; /* password is null byte */
		bcc_ptr++;              /* skip password */
		/* already aligned so no need to do it below */
	} else {
		pSMB->PasswordLength = cpu_to_le16(CIFS_SESS_KEY_SIZE);
		pSMB->PasswordLength = cpu_to_le16(CIFS_AUTH_RESP_SIZE);
		/* BB FIXME add code to fail this if NTLMv2 or Kerberos
		   specified as required (when that support is added to
		   the vfs in the future) as only NTLM or the much
		   weaker LANMAN (which we do not send by default) is accepted
		   by Samba (not sure whether other servers allow
		   NTLMv2 password here) */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		if ((extended_security & CIFSSEC_MAY_LANMAN) &&
			(ses->server->secType == LANMAN))
			calc_lanman_hash(ses, bcc_ptr);
		else
#endif /* CIFS_WEAK_PW_HASH */
		SMBNTencrypt(ses->password,
			     ses->server->cryptKey,
			     bcc_ptr);

		bcc_ptr += CIFS_SESS_KEY_SIZE;
		if ((global_secflags & CIFSSEC_MAY_LANMAN) &&
		    (ses->sectype == LANMAN))
			calc_lanman_hash(tcon->password, ses->server->cryptkey,
					 ses->server->sec_mode &
					    SECMODE_PW_ENCRYPT ? true : false,
					 bcc_ptr);
		else
#endif /* CIFS_WEAK_PW_HASH */
		rc = SMBNTencrypt(tcon->password, ses->server->cryptkey,
					bcc_ptr, nls_codepage);
		if (rc) {
			cifs_dbg(FYI, "%s Can't generate NTLM rsp. Error: %d\n",
				 __func__, rc);
			cifs_buf_release(smb_buffer);
			return rc;
		}

		bcc_ptr += CIFS_AUTH_RESP_SIZE;
		if (ses->capabilities & CAP_UNICODE) {
			/* must align unicode strings */
			*bcc_ptr = 0; /* null byte password */
			bcc_ptr++;
		}
	}

	if (ses->server->secMode &
			(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
	if (ses->server->sign)
		smb_buffer->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_STATUS32) {
		smb_buffer->Flags2 |= SMBFLG2_ERR_STATUS;
	}
	if (ses->capabilities & CAP_DFS) {
		smb_buffer->Flags2 |= SMBFLG2_DFS;
	}
	if (ses->capabilities & CAP_UNICODE) {
		smb_buffer->Flags2 |= SMBFLG2_UNICODE;
		length =
		    cifs_strtoUCS((__le16 *) bcc_ptr, tree,
		    cifs_strtoUTF16((__le16 *) bcc_ptr, tree,
			6 /* max utf8 char length in bytes */ *
			(/* server len*/ + 256 /* share len */), nls_codepage);
		bcc_ptr += 2 * length;	/* convert num 16 bit words to bytes */
		bcc_ptr += 2;	/* skip trailing null */
	} else {		/* ASCII */
		strcpy(bcc_ptr, tree);
		bcc_ptr += strlen(tree) + 1;
	}
	strcpy(bcc_ptr, "?????");
	bcc_ptr += strlen("?????");
	bcc_ptr += 1;
	count = bcc_ptr - &pSMB->Password[0];
	pSMB->hdr.smb_buf_length += count;
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length,
			 CIFS_STD_OP);

	/* if (rc) rc = map_smb_to_linux_error(smb_buffer_response); */
	/* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
		tcon->tidStatus = CifsGood;
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		length = strnlen(bcc_ptr, BCC(smb_buffer_response) - 2);
	pSMB->hdr.smb_buf_length = cpu_to_be32(be32_to_cpu(
					pSMB->hdr.smb_buf_length) + count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, smb_buffer, smb_buffer_response, &length,
			 0);

	/* above now done in SendReceive */
	if ((rc == 0) && (tcon != NULL)) {
		bool is_unicode;

		tcon->tidStatus = CifsGood;
		tcon->need_reconnect = false;
		tcon->tid = smb_buffer_response->Tid;
		bcc_ptr = pByteArea(smb_buffer_response);
		bytes_left = get_bcc(smb_buffer_response);
		length = strnlen(bcc_ptr, bytes_left - 2);
		if (smb_buffer->Flags2 & SMBFLG2_UNICODE)
			is_unicode = true;
		else
			is_unicode = false;


		/* skip service field (NB: this field is always ASCII) */
		if (length == 3) {
			if ((bcc_ptr[0] == 'I') && (bcc_ptr[1] == 'P') &&
			    (bcc_ptr[2] == 'C')) {
				cFYI(1, ("IPC connection"));
				cifs_dbg(FYI, "IPC connection\n");
				tcon->ipc = 1;
			}
		} else if (length == 2) {
			if ((bcc_ptr[0] == 'A') && (bcc_ptr[1] == ':')) {
				/* the most common case */
				cFYI(1, ("disk share connection"));
			}
		}
		bcc_ptr += length + 1;
		strncpy(tcon->treeName, tree, MAX_TREE_SIZE);
		if (smb_buffer->Flags2 & SMBFLG2_UNICODE) {
			length = UniStrnlen((wchar_t *) bcc_ptr, 512);
			if ((bcc_ptr + (2 * length)) -
			     pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				kfree(tcon->nativeFileSystem);
				tcon->nativeFileSystem =
				    kzalloc(length + 2, GFP_KERNEL);
				if (tcon->nativeFileSystem)
					cifs_strfromUCS_le(
						tcon->nativeFileSystem,
						(__le16 *) bcc_ptr,
						length, nls_codepage);
				bcc_ptr += 2 * length;
				bcc_ptr[0] = 0;	/* null terminate the string */
				bcc_ptr[1] = 0;
				bcc_ptr += 2;
			}
			/* else do not bother copying these information fields*/
		} else {
			length = strnlen(bcc_ptr, 1024);
			if ((bcc_ptr + length) -
			    pByteArea(smb_buffer_response) <=
			    BCC(smb_buffer_response)) {
				kfree(tcon->nativeFileSystem);
				tcon->nativeFileSystem =
				    kzalloc(length + 1, GFP_KERNEL);
				if (tcon->nativeFileSystem)
					strncpy(tcon->nativeFileSystem, bcc_ptr,
						length);
			}
			/* else do not bother copying these information fields*/
		}
				cifs_dbg(FYI, "disk share connection\n");
			}
		}
		bcc_ptr += length + 1;
		bytes_left -= (length + 1);
		strlcpy(tcon->treeName, tree, sizeof(tcon->treeName));

		/* mostly informational -- no need to fail on error here */
		kfree(tcon->nativeFileSystem);
		tcon->nativeFileSystem = cifs_strndup_from_utf16(bcc_ptr,
						      bytes_left, is_unicode,
						      nls_codepage);

		cifs_dbg(FYI, "nativeFileSystem=%s\n", tcon->nativeFileSystem);

		if ((smb_buffer_response->WordCount == 3) ||
			 (smb_buffer_response->WordCount == 7))
			/* field is in same location */
			tcon->Flags = le16_to_cpu(pSMBr->OptionalSupport);
		else
			tcon->Flags = 0;
		cFYI(1, ("Tcon flags: 0x%x ", tcon->Flags));
		cifs_dbg(FYI, "Tcon flags: 0x%x\n", tcon->Flags);
	} else if ((rc == 0) && tcon == NULL) {
		/* all we need to save for IPC$ connection */
		ses->ipc_tid = smb_buffer_response->Tid;
	}

	cifs_buf_release(smb_buffer);
	return rc;
}

int
cifs_umount(struct super_block *sb, struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	int xid;
	struct cifsSesInfo *ses = NULL;
	struct task_struct *cifsd_task;
	char *tmp;

	xid = GetXid();

	if (cifs_sb->tcon) {
		ses = cifs_sb->tcon->ses; /* save ptr to ses before delete tcon!*/
		rc = CIFSSMBTDis(xid, cifs_sb->tcon);
		if (rc == -EBUSY) {
			FreeXid(xid);
			return 0;
		}
		DeleteTconOplockQEntries(cifs_sb->tcon);
		tconInfoFree(cifs_sb->tcon);
		if ((ses) && (ses->server)) {
			/* save off task so we do not refer to ses later */
			cifsd_task = ses->server->tsk;
			cFYI(1, ("About to do SMBLogoff "));
			rc = CIFSSMBLogoff(xid, ses);
			if (rc == -EBUSY) {
				FreeXid(xid);
				return 0;
			} else if (rc == -ESHUTDOWN) {
				cFYI(1, ("Waking up socket by sending signal"));
				if (cifsd_task) {
					force_sig(SIGKILL, cifsd_task);
					kthread_stop(cifsd_task);
				}
				rc = 0;
			} /* else - we have an smb session
				left on this socket do not kill cifsd */
		} else
			cFYI(1, ("No session or bad tcon"));
	}

	cifs_sb->tcon = NULL;
	tmp = cifs_sb->prepath;
	cifs_sb->prepathlen = 0;
	cifs_sb->prepath = NULL;
	kfree(tmp);
	if (ses)
		sesInfoFree(ses);

	FreeXid(xid);
	return rc;
}

int cifs_setup_session(unsigned int xid, struct cifsSesInfo *pSesInfo,
					   struct nls_table *nls_info)
{
	int rc = 0;
	char ntlm_session_key[CIFS_SESS_KEY_SIZE];
	bool ntlmv2_flag = false;
	int first_time = 0;
	struct TCP_Server_Info *server = pSesInfo->server;

	/* what if server changes its buffer size after dropping the session? */
	if (server->maxBuf == 0) /* no need to send on reconnect */ {
		rc = CIFSSMBNegotiate(xid, pSesInfo);
		if (rc == -EAGAIN) {
			/* retry only once on 1st time connection */
			rc = CIFSSMBNegotiate(xid, pSesInfo);
			if (rc == -EAGAIN)
				rc = -EHOSTDOWN;
		}
		if (rc == 0) {
			spin_lock(&GlobalMid_Lock);
			if (server->tcpStatus != CifsExiting)
				server->tcpStatus = CifsGood;
			else
				rc = -EHOSTDOWN;
			spin_unlock(&GlobalMid_Lock);

		}
		first_time = 1;
	}

	if (rc)
		goto ss_err_exit;

	pSesInfo->flags = 0;
	pSesInfo->capabilities = server->capabilities;
	if (linuxExtEnabled == 0)
		pSesInfo->capabilities &= (~CAP_UNIX);
	/*	pSesInfo->sequence_number = 0;*/
	cFYI(1, ("Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d",
		 server->secMode, server->capabilities, server->timeAdj));

	if (experimEnabled < 2)
		rc = CIFS_SessSetup(xid, pSesInfo, first_time, nls_info);
	else if (extended_security
			&& (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
			&& (server->secType == NTLMSSP)) {
		rc = -EOPNOTSUPP;
	} else if (extended_security
			&& (pSesInfo->capabilities & CAP_EXTENDED_SECURITY)
			&& (server->secType == RawNTLMSSP)) {
		cFYI(1, ("NTLMSSP sesssetup"));
		rc = CIFSNTLMSSPNegotiateSessSetup(xid, pSesInfo, &ntlmv2_flag,
						   nls_info);
		if (!rc) {
			if (ntlmv2_flag) {
				char *v2_response;
				cFYI(1, ("more secure NTLM ver2 hash"));
				if (CalcNTLMv2_partial_mac_key(pSesInfo,
								nls_info)) {
					rc = -ENOMEM;
					goto ss_err_exit;
				} else
					v2_response = kmalloc(16 + 64 /* blob*/,
								GFP_KERNEL);
				if (v2_response) {
					CalcNTLMv2_response(pSesInfo,
								v2_response);
				/*	if (first_time)
						cifs_calculate_ntlmv2_mac_key */
					kfree(v2_response);
					/* BB Put dummy sig in SessSetup PDU? */
				} else {
					rc = -ENOMEM;
					goto ss_err_exit;
				}

			} else {
				SMBNTencrypt(pSesInfo->password,
					     server->cryptKey,
					     ntlm_session_key);

				if (first_time)
					cifs_calculate_mac_key(
					     &server->mac_signing_key,
					     ntlm_session_key,
					     pSesInfo->password);
			}
			/* for better security the weaker lanman hash not sent
			   in AuthSessSetup so we no longer calculate it */

			rc = CIFSNTLMSSPAuthSessSetup(xid, pSesInfo,
						      ntlm_session_key,
						      ntlmv2_flag,
						      nls_info);
		}
	} else { /* old style NTLM 0.12 session setup */
		SMBNTencrypt(pSesInfo->password, server->cryptKey,
			     ntlm_session_key);

		if (first_time)
			cifs_calculate_mac_key(&server->mac_signing_key,
						ntlm_session_key,
						pSesInfo->password);

		rc = CIFSSessSetup(xid, pSesInfo, ntlm_session_key, nls_info);
	}
	if (rc) {
		cERROR(1, ("Send error in SessSetup = %d", rc));
	} else {
		cFYI(1, ("CIFS Session Established successfully"));
			pSesInfo->status = CifsGood;
	}

ss_err_exit:
	return rc;
}

static void delayed_free(struct rcu_head *p)
{
	struct cifs_sb_info *sbi = container_of(p, struct cifs_sb_info, rcu);
	unload_nls(sbi->local_nls);
	kfree(sbi);
}

void
cifs_umount(struct cifs_sb_info *cifs_sb)
{
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node;
	struct tcon_link *tlink;

	cancel_delayed_work_sync(&cifs_sb->prune_tlinks);

	spin_lock(&cifs_sb->tlink_tree_lock);
	while ((node = rb_first(root))) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);
		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(node, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	bdi_destroy(&cifs_sb->bdi);
	kfree(cifs_sb->mountdata);
	kfree(cifs_sb->prepath);
	call_rcu(&cifs_sb->rcu, delayed_free);
}

int
cifs_negotiate_protocol(const unsigned int xid, struct cifs_ses *ses)
{
	int rc = 0;
	struct TCP_Server_Info *server = ses->server;

	if (!server->ops->need_neg || !server->ops->negotiate)
		return -ENOSYS;

	/* only send once per connect */
	if (!server->ops->need_neg(server))
		return 0;

	set_credits(server, 1);

	rc = server->ops->negotiate(xid, ses);
	if (rc == 0) {
		spin_lock(&GlobalMid_Lock);
		if (server->tcpStatus == CifsNeedNegotiate)
			server->tcpStatus = CifsGood;
		else
			rc = -EHOSTDOWN;
		spin_unlock(&GlobalMid_Lock);
	}

	return rc;
}

int
cifs_setup_session(const unsigned int xid, struct cifs_ses *ses,
		   struct nls_table *nls_info)
{
	int rc = -ENOSYS;
	struct TCP_Server_Info *server = ses->server;

	ses->capabilities = server->capabilities;
	if (linuxExtEnabled == 0)
		ses->capabilities &= (~server->vals->cap_unix);

	cifs_dbg(FYI, "Security Mode: 0x%x Capabilities: 0x%x TimeAdjust: %d\n",
		 server->sec_mode, server->capabilities, server->timeAdj);

	if (ses->auth_key.response) {
		cifs_dbg(VFS, "Free previous auth_key.response = %p\n",
			 ses->auth_key.response);
		kfree(ses->auth_key.response);
		ses->auth_key.response = NULL;
		ses->auth_key.len = 0;
	}

	if (server->ops->sess_setup)
		rc = server->ops->sess_setup(xid, ses, nls_info);

	if (rc)
		cifs_dbg(VFS, "Send error in SessSetup = %d\n", rc);

	return rc;
}

static int
cifs_set_vol_auth(struct smb_vol *vol, struct cifs_ses *ses)
{
	vol->sectype = ses->sectype;

	/* krb5 is special, since we don't need username or pw */
	if (vol->sectype == Kerberos)
		return 0;

	return cifs_set_cifscreds(vol, ses);
}

static struct cifs_tcon *
cifs_construct_tcon(struct cifs_sb_info *cifs_sb, kuid_t fsuid)
{
	int rc;
	struct cifs_tcon *master_tcon = cifs_sb_master_tcon(cifs_sb);
	struct cifs_ses *ses;
	struct cifs_tcon *tcon = NULL;
	struct smb_vol *vol_info;

	vol_info = kzalloc(sizeof(*vol_info), GFP_KERNEL);
	if (vol_info == NULL)
		return ERR_PTR(-ENOMEM);

	vol_info->local_nls = cifs_sb->local_nls;
	vol_info->linux_uid = fsuid;
	vol_info->cred_uid = fsuid;
	vol_info->UNC = master_tcon->treeName;
	vol_info->retry = master_tcon->retry;
	vol_info->nocase = master_tcon->nocase;
	vol_info->local_lease = master_tcon->local_lease;
	vol_info->resilient = master_tcon->use_resilient;
	vol_info->persistent = master_tcon->use_persistent;
	vol_info->no_linux_ext = !master_tcon->unix_ext;
	vol_info->sectype = master_tcon->ses->sectype;
	vol_info->sign = master_tcon->ses->sign;
	vol_info->seal = master_tcon->seal;

	rc = cifs_set_vol_auth(vol_info, master_tcon->ses);
	if (rc) {
		tcon = ERR_PTR(rc);
		goto out;
	}

	/* get a reference for the same TCP session */
	spin_lock(&cifs_tcp_ses_lock);
	++master_tcon->ses->server->srv_count;
	spin_unlock(&cifs_tcp_ses_lock);

	ses = cifs_get_smb_ses(master_tcon->ses->server, vol_info);
	if (IS_ERR(ses)) {
		tcon = (struct cifs_tcon *)ses;
		cifs_put_tcp_session(master_tcon->ses->server, 0);
		goto out;
	}

	tcon = cifs_get_tcon(ses, vol_info);
	if (IS_ERR(tcon)) {
		cifs_put_smb_ses(ses);
		goto out;
	}

	if (cap_unix(ses))
		reset_cifs_unix_caps(0, tcon, NULL, vol_info);
out:
	kfree(vol_info->username);
	kzfree(vol_info->password);
	kfree(vol_info);

	return tcon;
}

struct cifs_tcon *
cifs_sb_master_tcon(struct cifs_sb_info *cifs_sb)
{
	return tlink_tcon(cifs_sb_master_tlink(cifs_sb));
}

/* find and return a tlink with given uid */
static struct tcon_link *
tlink_rb_search(struct rb_root *root, kuid_t uid)
{
	struct rb_node *node = root->rb_node;
	struct tcon_link *tlink;

	while (node) {
		tlink = rb_entry(node, struct tcon_link, tl_rbnode);

		if (uid_gt(tlink->tl_uid, uid))
			node = node->rb_left;
		else if (uid_lt(tlink->tl_uid, uid))
			node = node->rb_right;
		else
			return tlink;
	}
	return NULL;
}

/* insert a tcon_link into the tree */
static void
tlink_rb_insert(struct rb_root *root, struct tcon_link *new_tlink)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct tcon_link *tlink;

	while (*new) {
		tlink = rb_entry(*new, struct tcon_link, tl_rbnode);
		parent = *new;

		if (uid_gt(tlink->tl_uid, new_tlink->tl_uid))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&new_tlink->tl_rbnode, parent, new);
	rb_insert_color(&new_tlink->tl_rbnode, root);
}

/*
 * Find or construct an appropriate tcon given a cifs_sb and the fsuid of the
 * current task.
 *
 * If the superblock doesn't refer to a multiuser mount, then just return
 * the master tcon for the mount.
 *
 * First, search the rbtree for an existing tcon for this fsuid. If one
 * exists, then check to see if it's pending construction. If it is then wait
 * for construction to complete. Once it's no longer pending, check to see if
 * it failed and either return an error or retry construction, depending on
 * the timeout.
 *
 * If one doesn't exist then insert a new tcon_link struct into the tree and
 * try to construct a new one.
 */
struct tcon_link *
cifs_sb_tlink(struct cifs_sb_info *cifs_sb)
{
	int ret;
	kuid_t fsuid = current_fsuid();
	struct tcon_link *tlink, *newtlink;

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		return cifs_get_tlink(cifs_sb_master_tlink(cifs_sb));

	spin_lock(&cifs_sb->tlink_tree_lock);
	tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
	if (tlink)
		cifs_get_tlink(tlink);
	spin_unlock(&cifs_sb->tlink_tree_lock);

	if (tlink == NULL) {
		newtlink = kzalloc(sizeof(*tlink), GFP_KERNEL);
		if (newtlink == NULL)
			return ERR_PTR(-ENOMEM);
		newtlink->tl_uid = fsuid;
		newtlink->tl_tcon = ERR_PTR(-EACCES);
		set_bit(TCON_LINK_PENDING, &newtlink->tl_flags);
		set_bit(TCON_LINK_IN_TREE, &newtlink->tl_flags);
		cifs_get_tlink(newtlink);

		spin_lock(&cifs_sb->tlink_tree_lock);
		/* was one inserted after previous search? */
		tlink = tlink_rb_search(&cifs_sb->tlink_tree, fsuid);
		if (tlink) {
			cifs_get_tlink(tlink);
			spin_unlock(&cifs_sb->tlink_tree_lock);
			kfree(newtlink);
			goto wait_for_construction;
		}
		tlink = newtlink;
		tlink_rb_insert(&cifs_sb->tlink_tree, tlink);
		spin_unlock(&cifs_sb->tlink_tree_lock);
	} else {
wait_for_construction:
		ret = wait_on_bit(&tlink->tl_flags, TCON_LINK_PENDING,
				  TASK_INTERRUPTIBLE);
		if (ret) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-ERESTARTSYS);
		}

		/* if it's good, return it */
		if (!IS_ERR(tlink->tl_tcon))
			return tlink;

		/* return error if we tried this already recently */
		if (time_before(jiffies, tlink->tl_time + TLINK_ERROR_EXPIRE)) {
			cifs_put_tlink(tlink);
			return ERR_PTR(-EACCES);
		}

		if (test_and_set_bit(TCON_LINK_PENDING, &tlink->tl_flags))
			goto wait_for_construction;
	}

	tlink->tl_tcon = cifs_construct_tcon(cifs_sb, fsuid);
	clear_bit(TCON_LINK_PENDING, &tlink->tl_flags);
	wake_up_bit(&tlink->tl_flags, TCON_LINK_PENDING);

	if (IS_ERR(tlink->tl_tcon)) {
		cifs_put_tlink(tlink);
		return ERR_PTR(-EACCES);
	}

	return tlink;
}

/*
 * periodic workqueue job that scans tcon_tree for a superblock and closes
 * out tcons.
 */
static void
cifs_prune_tlinks(struct work_struct *work)
{
	struct cifs_sb_info *cifs_sb = container_of(work, struct cifs_sb_info,
						    prune_tlinks.work);
	struct rb_root *root = &cifs_sb->tlink_tree;
	struct rb_node *node = rb_first(root);
	struct rb_node *tmp;
	struct tcon_link *tlink;

	/*
	 * Because we drop the spinlock in the loop in order to put the tlink
	 * it's not guarded against removal of links from the tree. The only
	 * places that remove entries from the tree are this function and
	 * umounts. Because this function is non-reentrant and is canceled
	 * before umount can proceed, this is safe.
	 */
	spin_lock(&cifs_sb->tlink_tree_lock);
	node = rb_first(root);
	while (node != NULL) {
		tmp = node;
		node = rb_next(tmp);
		tlink = rb_entry(tmp, struct tcon_link, tl_rbnode);

		if (test_bit(TCON_LINK_MASTER, &tlink->tl_flags) ||
		    atomic_read(&tlink->tl_count) != 0 ||
		    time_after(tlink->tl_time + TLINK_IDLE_EXPIRE, jiffies))
			continue;

		cifs_get_tlink(tlink);
		clear_bit(TCON_LINK_IN_TREE, &tlink->tl_flags);
		rb_erase(tmp, root);

		spin_unlock(&cifs_sb->tlink_tree_lock);
		cifs_put_tlink(tlink);
		spin_lock(&cifs_sb->tlink_tree_lock);
	}
	spin_unlock(&cifs_sb->tlink_tree_lock);

	queue_delayed_work(cifsiod_wq, &cifs_sb->prune_tlinks,
				TLINK_IDLE_EXPIRE);
}
