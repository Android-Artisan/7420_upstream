/*
 *   fs/cifs/file.c
 *
 *   vfs operations that deal with files
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2007
 *   Copyright (C) International Business Machines  Corp., 2002,2010
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
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
#include <linux/backing-dev.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/delay.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

static inline struct cifsFileInfo *cifs_init_private(
	struct cifsFileInfo *private_data, struct inode *inode,
	struct file *file, __u16 netfid)
{
	memset(private_data, 0, sizeof(struct cifsFileInfo));
	private_data->netfid = netfid;
	private_data->pid = current->tgid;
	init_MUTEX(&private_data->fh_sem);
	mutex_init(&private_data->lock_mutex);
	INIT_LIST_HEAD(&private_data->llist);
	private_data->pfile = file; /* needed for writepage */
	private_data->pInode = inode;
	private_data->invalidHandle = false;
	private_data->closePend = false;
	/* we have to track num writers to the inode, since writepages
	does not tell us which handle the write is for so there can
	be a close (overlapping with write) of the filehandle that
	cifs_writepages chose to use */
	atomic_set(&private_data->wrtPending, 0);

	return private_data;
}
#include "fscache.h"


static inline int cifs_convert_flags(unsigned int flags)
{
	if ((flags & O_ACCMODE) == O_RDONLY)
		return GENERIC_READ;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		return GENERIC_WRITE;
	else if ((flags & O_ACCMODE) == O_RDWR) {
		/* GENERIC_ALL is too much permission to request
		   can cause unnecessary access denied on create */
		/* return GENERIC_ALL; */
		return (GENERIC_READ | GENERIC_WRITE);
	}

	return (READ_CONTROL | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES |
		FILE_WRITE_EA | FILE_APPEND_DATA | FILE_WRITE_DATA |
		FILE_READ_DATA);


}

static u32 cifs_posix_convert_flags(unsigned int flags)
{
	u32 posix_flags = 0;

	if ((flags & O_ACCMODE) == O_RDONLY)
		posix_flags = SMB_O_RDONLY;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		posix_flags = SMB_O_WRONLY;
	else if ((flags & O_ACCMODE) == O_RDWR)
		posix_flags = SMB_O_RDWR;

	if (flags & O_CREAT) {
		posix_flags |= SMB_O_CREAT;
		if (flags & O_EXCL)
			posix_flags |= SMB_O_EXCL;
	} else if (flags & O_EXCL)
		cifs_dbg(FYI, "Application %s pid %d has incorrectly set O_EXCL flag but not O_CREAT on file open. Ignoring O_EXCL\n",
			 current->comm, current->tgid);

	if (flags & O_TRUNC)
		posix_flags |= SMB_O_TRUNC;
	/* be safe and imply O_SYNC for O_DSYNC */
	if (flags & O_DSYNC)
		posix_flags |= SMB_O_SYNC;
	if (flags & O_DIRECTORY)
		posix_flags |= SMB_O_DIRECTORY;
	if (flags & O_NOFOLLOW)
		posix_flags |= SMB_O_NOFOLLOW;
	if (flags & O_DIRECT)
		posix_flags |= SMB_O_DIRECT;

	return posix_flags;
}

static inline int cifs_get_disposition(unsigned int flags)
{
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		return FILE_CREATE;
	else if ((flags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
		return FILE_OVERWRITE_IF;
	else if ((flags & O_CREAT) == O_CREAT)
		return FILE_OPEN_IF;
	else if ((flags & O_TRUNC) == O_TRUNC)
		return FILE_OVERWRITE;
	else
		return FILE_OPEN;
}

/* all arguments to this function must be checked for validity in caller */
static inline int cifs_open_inode_helper(struct inode *inode, struct file *file,
	struct cifsInodeInfo *pCifsInode, struct cifsFileInfo *pCifsFile,
	struct cifsTconInfo *pTcon, int *oplock, FILE_ALL_INFO *buf,
	char *full_path, int xid)
{
	struct timespec temp;
	int rc;

	/* want handles we can use to read with first
	   in the list so we do not have to walk the
	   list to search for one in prepare_write */
	if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		list_add_tail(&pCifsFile->flist,
			      &pCifsInode->openFileList);
	} else {
		list_add(&pCifsFile->flist,
			 &pCifsInode->openFileList);
	}
	write_unlock(&GlobalSMBSeslock);
	if (pCifsInode->clientCanCacheRead) {
		/* we have the inode open somewhere else
		   no need to discard cache data */
		goto client_can_cache;
	}

	/* BB need same check in cifs_create too? */
	/* if not oplocked, invalidate inode pages if mtime or file
	   size changed */
	temp = cifs_NTtimeToUnix(le64_to_cpu(buf->LastWriteTime));
	if (timespec_equal(&file->f_path.dentry->d_inode->i_mtime, &temp) &&
			   (file->f_path.dentry->d_inode->i_size ==
			    (loff_t)le64_to_cpu(buf->EndOfFile))) {
		cFYI(1, ("inode unchanged on server"));
	} else {
		if (file->f_path.dentry->d_inode->i_mapping) {
		/* BB no need to lock inode until after invalidate
		   since namei code should already have it locked? */
			rc = filemap_write_and_wait(file->f_path.dentry->d_inode->i_mapping);
			if (rc != 0)
				CIFS_I(file->f_path.dentry->d_inode)->write_behind_rc = rc;
		}
		cFYI(1, ("invalidating remote inode since open detected it "
			 "changed"));
		invalidate_remote_inode(file->f_path.dentry->d_inode);
	}

client_can_cache:
	if (pTcon->unix_ext)
		rc = cifs_get_inode_info_unix(&file->f_path.dentry->d_inode,
			full_path, inode->i_sb, xid);
	else
		rc = cifs_get_inode_info(&file->f_path.dentry->d_inode,
			full_path, buf, inode->i_sb, xid, NULL);

	if ((*oplock & 0xF) == OPLOCK_EXCLUSIVE) {
		pCifsInode->clientCanCacheAll = true;
		pCifsInode->clientCanCacheRead = true;
		cFYI(1, ("Exclusive Oplock granted on inode %p",
			 file->f_path.dentry->d_inode));
	} else if ((*oplock & 0xF) == OPLOCK_READ)
		pCifsInode->clientCanCacheRead = true;

	return rc;
}

int cifs_open(struct inode *inode, struct file *file)
{
	int rc = -EACCES;
	int xid, oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *pCifsFile;
	struct cifsInodeInfo *pCifsInode;
	struct list_head *tmp;
	char *full_path = NULL;
	int desiredAccess;
	int disposition;
	__u16 netfid;
	FILE_ALL_INFO *buf = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	if (file->f_flags & O_CREAT) {
		/* search inode for this file and fill in file->private_data */
		pCifsInode = CIFS_I(file->f_path.dentry->d_inode);
		read_lock(&GlobalSMBSeslock);
		list_for_each(tmp, &pCifsInode->openFileList) {
			pCifsFile = list_entry(tmp, struct cifsFileInfo,
					       flist);
			if ((pCifsFile->pfile == NULL) &&
			    (pCifsFile->pid == current->tgid)) {
				/* mode set in cifs_create */

				/* needed for writepage */
				pCifsFile->pfile = file;

				file->private_data = pCifsFile;
				break;
			}
		}
		read_unlock(&GlobalSMBSeslock);
		if (file->private_data != NULL) {
			rc = 0;
			FreeXid(xid);
			return rc;
		} else {
			if (file->f_flags & O_EXCL)
				cERROR(1, ("could not find file instance for "
					   "new file %p", file));
		}
	}

	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	cFYI(1, ("inode = 0x%p file flags are 0x%x for %s",
		 inode, file->f_flags, full_path));
	desiredAccess = cifs_convert_flags(file->f_flags);
int cifs_posix_open(char *full_path, struct inode **pinode,
			struct super_block *sb, int mode, unsigned int f_flags,
			__u32 *poplock, __u16 *pnetfid, unsigned int xid)
{
	int rc;
	FILE_UNIX_BASIC_INFO *presp_data;
	__u32 posix_flags = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_fattr fattr;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;

	cifs_dbg(FYI, "posix open %s\n", full_path);

	presp_data = kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
	if (presp_data == NULL)
		return -ENOMEM;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		rc = PTR_ERR(tlink);
		goto posix_open_ret;
	}

	tcon = tlink_tcon(tlink);
	mode &= ~current_umask();

	posix_flags = cifs_posix_convert_flags(f_flags);
	rc = CIFSPOSIXCreate(xid, tcon, posix_flags, mode, pnetfid, presp_data,
			     poplock, full_path, cifs_sb->local_nls,
			     cifs_remap(cifs_sb));
	cifs_put_tlink(tlink);

	if (rc)
		goto posix_open_ret;

	if (presp_data->Type == cpu_to_le32(-1))
		goto posix_open_ret; /* open ok, caller does qpathinfo */

	if (!pinode)
		goto posix_open_ret; /* caller does not need info */

	cifs_unix_basic_to_fattr(&fattr, presp_data, cifs_sb);

	/* get new inode and set it up */
	if (*pinode == NULL) {
		cifs_fill_uniqueid(sb, &fattr);
		*pinode = cifs_iget(sb, &fattr);
		if (!*pinode) {
			rc = -ENOMEM;
			goto posix_open_ret;
		}
	} else {
		cifs_revalidate_mapping(*pinode);
		cifs_fattr_to_inode(*pinode, &fattr);
	}

posix_open_ret:
	kfree(presp_data);
	return rc;
}

static int
cifs_nt_open(char *full_path, struct inode *inode, struct cifs_sb_info *cifs_sb,
	     struct cifs_tcon *tcon, unsigned int f_flags, __u32 *oplock,
	     struct cifs_fid *fid, unsigned int xid)
{
	int rc;
	int desired_access;
	int disposition;
	int create_options = CREATE_NOT_DIR;
	FILE_ALL_INFO *buf;
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifs_open_parms oparms;

	if (!server->ops->open)
		return -ENOSYS;

	desired_access = cifs_convert_flags(f_flags);

/*********************************************************************
 *  open flag mapping table:
 *
 *	POSIX Flag            CIFS Disposition
 *	----------            ----------------
 *	O_CREAT               FILE_OPEN_IF
 *	O_CREAT | O_EXCL      FILE_CREATE
 *	O_CREAT | O_TRUNC     FILE_OVERWRITE_IF
 *	O_TRUNC               FILE_OVERWRITE
 *	none of the above     FILE_OPEN
 *
 *	Note that there is not a direct match between disposition
 *	FILE_SUPERSEDE (ie create whether or not file exists although
 *	O_CREAT | O_TRUNC is similar but truncates the existing
 *	file rather than creating a new file as FILE_SUPERSEDE does
 *	(which uses the attributes / metadata passed in on open call)
 *?
 *?  O_SYNC is a reasonable match to CIFS writethrough flag
 *?  and the read write flags match reasonably.  O_LARGEFILE
 *?  is irrelevant because largefile support is always used
 *?  by this client. Flags O_APPEND, O_DIRECT, O_DIRECTORY,
 *	 O_FASYNC, O_NOFOLLOW, O_NONBLOCK need further investigation
 *********************************************************************/

	disposition = cifs_get_disposition(file->f_flags);

	if (oplockEnabled)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	/* BB pass O_SYNC flag through on file attributes .. BB */

	/* Also refresh inode by passing in file_info buf returned by SMBOpen
	   and calling get_inode_info with returned buf (at least helps
	   non-Unix server case) */

	/* BB we can not do this if this is the second open of a file
	   and the first handle has writebehind data, we might be
	   able to simply do a filemap_fdatawrite/filemap_fdatawait first */
	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	if (cifs_sb->tcon->ses->capabilities & CAP_NT_SMBS)
		rc = CIFSSMBOpen(xid, pTcon, full_path, disposition,
			 desiredAccess, CREATE_NOT_DIR, &netfid, &oplock, buf,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
				 & CIFS_MOUNT_MAP_SPECIAL_CHR);
	else
		rc = -EIO; /* no NT SMB support fall into legacy open below */

	if (rc == -EIO) {
		/* Old server, try legacy style OpenX */
		rc = SMBLegacyOpen(xid, pTcon, full_path, disposition,
			desiredAccess, CREATE_NOT_DIR, &netfid, &oplock, buf,
			cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
				& CIFS_MOUNT_MAP_SPECIAL_CHR);
	}
	if (rc) {
		cFYI(1, ("cifs_open returned 0x%x", rc));
		goto out;
	}
	file->private_data =
		kmalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (file->private_data == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	pCifsFile = cifs_init_private(file->private_data, inode, file, netfid);
	write_lock(&GlobalSMBSeslock);
	list_add(&pCifsFile->tlist, &pTcon->openFileList);

	pCifsInode = CIFS_I(file->f_path.dentry->d_inode);
	if (pCifsInode) {
		rc = cifs_open_inode_helper(inode, file, pCifsInode,
					    pCifsFile, pTcon,
					    &oplock, buf, full_path, xid);
	} else {
		write_unlock(&GlobalSMBSeslock);
	}

	if (oplock & CIFS_CREATE_ACTION) {
		/* time to set mode which we can not set earlier due to
		   problems creating new read-only files */
		if (pTcon->unix_ext) {
			struct cifs_unix_set_info_args args = {
				.mode	= inode->i_mode,
				.uid	= NO_CHANGE_64,
				.gid	= NO_CHANGE_64,
				.ctime	= NO_CHANGE_64,
				.atime	= NO_CHANGE_64,
				.mtime	= NO_CHANGE_64,
				.device	= 0,
			};
			CIFSSMBUnixSetInfo(xid, pTcon, full_path, &args,
					    cifs_sb->local_nls,
					    cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		}
	}

out:
	kfree(buf);
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

/* Try to reacquire byte range locks that were released when session */
/* to server was lost */
static int cifs_relock_file(struct cifsFileInfo *cifsFile)
{
	int rc = 0;

/* BB list all locks open on this file and relock */

	return rc;
}

static int cifs_reopen_file(struct file *file, bool can_flush)
{
	int rc = -EACCES;
	int xid, oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *pCifsFile;
	struct cifsInodeInfo *pCifsInode;
	struct inode *inode;
	char *full_path = NULL;
	int desiredAccess;
	int disposition = FILE_OPEN;
	__u16 netfid;

	if (file->private_data)
		pCifsFile = (struct cifsFileInfo *)file->private_data;
	else
		return -EBADF;

	xid = GetXid();
	down(&pCifsFile->fh_sem);
	if (!pCifsFile->invalidHandle) {
		up(&pCifsFile->fh_sem);
		FreeXid(xid);
		return 0;
	}

	if (file->f_path.dentry == NULL) {
		cERROR(1, ("no valid name if dentry freed"));
		dump_stack();
		rc = -EBADF;
		goto reopen_error_exit;
	}

	inode = file->f_path.dentry->d_inode;
	if (inode == NULL) {
		cERROR(1, ("inode not valid"));
		dump_stack();
		rc = -EBADF;
		goto reopen_error_exit;
	}

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

/* can not grab rename sem here because various ops, including
   those that already have the rename sem can end up causing writepage
   to get called and if the server was down that means we end up here,
   and we can never tell if the caller already has the rename_sem */
	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
reopen_error_exit:
		up(&pCifsFile->fh_sem);
		FreeXid(xid);
		return rc;
	}

	cFYI(1, ("inode = 0x%p file flags 0x%x for %s",
		 inode, file->f_flags, full_path));
	desiredAccess = cifs_convert_flags(file->f_flags);

	if (oplockEnabled)
	disposition = cifs_get_disposition(f_flags);

	/* BB pass O_SYNC flag through on file attributes .. BB */

	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (backup_cred(cifs_sb))
		create_options |= CREATE_OPEN_BACKUP_INTENT;

	/* O_SYNC also has bit for O_DSYNC so following check picks up either */
	if (f_flags & O_SYNC)
		create_options |= CREATE_WRITE_THROUGH;

	if (f_flags & O_DIRECT)
		create_options |= CREATE_NO_BUFFER;

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = desired_access;
	oparms.create_options = create_options;
	oparms.disposition = disposition;
	oparms.path = full_path;
	oparms.fid = fid;
	oparms.reconnect = false;

	rc = server->ops->open(xid, &oparms, oplock, buf);

	if (rc)
		goto out;

	if (tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, full_path, inode->i_sb,
					      xid);
	else
		rc = cifs_get_inode_info(&inode, full_path, buf, inode->i_sb,
					 xid, fid);

	if (rc) {
		server->ops->close(xid, tcon, fid);
		if (rc == -ESTALE)
			rc = -EOPENSTALE;
	}

out:
	kfree(buf);
	return rc;
}

static bool
cifs_has_mand_locks(struct cifsInodeInfo *cinode)
{
	struct cifs_fid_locks *cur;
	bool has_locks = false;

	down_read(&cinode->lock_sem);
	list_for_each_entry(cur, &cinode->llist, llist) {
		if (!list_empty(&cur->locks)) {
			has_locks = true;
			break;
		}
	}
	up_read(&cinode->lock_sem);
	return has_locks;
}

void
cifs_down_write(struct rw_semaphore *sem)
{
	while (!down_write_trylock(sem))
		msleep(10);
}

struct cifsFileInfo *
cifs_new_fileinfo(struct cifs_fid *fid, struct file *file,
		  struct tcon_link *tlink, __u32 oplock)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = d_inode(dentry);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifsFileInfo *cfile;
	struct cifs_fid_locks *fdlocks;
	struct cifs_tcon *tcon = tlink_tcon(tlink);
	struct TCP_Server_Info *server = tcon->ses->server;

	cfile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (cfile == NULL)
		return cfile;

	fdlocks = kzalloc(sizeof(struct cifs_fid_locks), GFP_KERNEL);
	if (!fdlocks) {
		kfree(cfile);
		return NULL;
	}

	INIT_LIST_HEAD(&fdlocks->locks);
	fdlocks->cfile = cfile;
	cfile->llist = fdlocks;

	cfile->count = 1;
	cfile->pid = current->tgid;
	cfile->uid = current_fsuid();
	cfile->dentry = dget(dentry);
	cfile->f_flags = file->f_flags;
	cfile->invalidHandle = false;
	cfile->tlink = cifs_get_tlink(tlink);
	INIT_WORK(&cfile->oplock_break, cifs_oplock_break);
	mutex_init(&cfile->fh_mutex);
	spin_lock_init(&cfile->file_info_lock);

	cifs_sb_active(inode->i_sb);

	/*
	 * If the server returned a read oplock and we have mandatory brlocks,
	 * set oplock level to None.
	 */
	if (server->ops->is_read_op(oplock) && cifs_has_mand_locks(cinode)) {
		cifs_dbg(FYI, "Reset oplock val from read to None due to mand locks\n");
		oplock = 0;
	}

	cifs_down_write(&cinode->lock_sem);
	list_add(&fdlocks->llist, &cinode->llist);
	up_write(&cinode->lock_sem);

	spin_lock(&tcon->open_file_lock);
	if (fid->pending_open->oplock != CIFS_OPLOCK_NO_CHANGE && oplock)
		oplock = fid->pending_open->oplock;
	list_del(&fid->pending_open->olist);

	fid->purge_cache = false;
	server->ops->set_fid(cfile, fid, oplock);

	list_add(&cfile->tlist, &tcon->openFileList);

	/* if readable file instance put first in list*/
	if (file->f_mode & FMODE_READ)
		list_add(&cfile->flist, &cinode->openFileList);
	else
		list_add_tail(&cfile->flist, &cinode->openFileList);
	spin_unlock(&tcon->open_file_lock);

	if (fid->purge_cache)
		cifs_zap_mapping(inode);

	file->private_data = cfile;
	return cfile;
}

struct cifsFileInfo *
cifsFileInfo_get(struct cifsFileInfo *cifs_file)
{
	spin_lock(&cifs_file->file_info_lock);
	cifsFileInfo_get_locked(cifs_file);
	spin_unlock(&cifs_file->file_info_lock);
	return cifs_file;
}

/*
 * Release a reference on the file private data. This may involve closing
 * the filehandle out on the server. Must be called without holding
 * tcon->open_file_lock and cifs_file->file_info_lock.
 */
void cifsFileInfo_put(struct cifsFileInfo *cifs_file)
{
	struct inode *inode = d_inode(cifs_file->dentry);
	struct cifs_tcon *tcon = tlink_tcon(cifs_file->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifsLockInfo *li, *tmp;
	struct cifs_fid fid;
	struct cifs_pending_open open;
	bool oplock_break_cancelled;

	spin_lock(&tcon->open_file_lock);

	spin_lock(&cifs_file->file_info_lock);
	if (--cifs_file->count > 0) {
		spin_unlock(&cifs_file->file_info_lock);
		spin_unlock(&tcon->open_file_lock);
		return;
	}
	spin_unlock(&cifs_file->file_info_lock);

	if (server->ops->get_lease_key)
		server->ops->get_lease_key(inode, &fid);

	/* store open in pending opens to make sure we don't miss lease break */
	cifs_add_pending_open_locked(&fid, cifs_file->tlink, &open);

	/* remove it from the lists */
	list_del(&cifs_file->flist);
	list_del(&cifs_file->tlist);

	if (list_empty(&cifsi->openFileList)) {
		cifs_dbg(FYI, "closing last open instance for inode %p\n",
			 d_inode(cifs_file->dentry));
		/*
		 * In strict cache mode we need invalidate mapping on the last
		 * close  because it may cause a error when we open this file
		 * again and get at least level II oplock.
		 */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO)
			set_bit(CIFS_INO_INVALID_MAPPING, &cifsi->flags);
		cifs_set_oplock_level(cifsi, 0);
	}

	spin_unlock(&tcon->open_file_lock);

	oplock_break_cancelled = cancel_work_sync(&cifs_file->oplock_break);

	if (!tcon->need_reconnect && !cifs_file->invalidHandle) {
		struct TCP_Server_Info *server = tcon->ses->server;
		unsigned int xid;

		xid = get_xid();
		if (server->ops->close)
			server->ops->close(xid, tcon, &cifs_file->fid);
		_free_xid(xid);
	}

	if (oplock_break_cancelled)
		cifs_done_oplock_break(cifsi);

	cifs_del_pending_open(&open);

	/*
	 * Delete any outstanding lock records. We'll lose them when the file
	 * is closed anyway.
	 */
	cifs_down_write(&cifsi->lock_sem);
	list_for_each_entry_safe(li, tmp, &cifs_file->llist->locks, llist) {
		list_del(&li->llist);
		cifs_del_lock_waiters(li);
		kfree(li);
	}
	list_del(&cifs_file->llist->llist);
	kfree(cifs_file->llist);
	up_write(&cifsi->lock_sem);

	cifs_put_tlink(cifs_file->tlink);
	dput(cifs_file->dentry);
	cifs_sb_deactive(sb);
	kfree(cifs_file);
}

int cifs_open(struct inode *inode, struct file *file)

{
	int rc = -EACCES;
	unsigned int xid;
	__u32 oplock;
	struct cifs_sb_info *cifs_sb;
	struct TCP_Server_Info *server;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	struct cifsFileInfo *cfile = NULL;
	char *full_path = NULL;
	bool posix_open_ok = false;
	struct cifs_fid fid;
	struct cifs_pending_open open;

	xid = get_xid();

	cifs_sb = CIFS_SB(inode->i_sb);
	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		free_xid(xid);
		return PTR_ERR(tlink);
	}
	tcon = tlink_tcon(tlink);
	server = tcon->ses->server;

	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cifs_dbg(FYI, "inode = 0x%p file flags are 0x%x for %s\n",
		 inode, file->f_flags, full_path);

	if (file->f_flags & O_DIRECT &&
	    cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO) {
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			file->f_op = &cifs_file_direct_nobrl_ops;
		else
			file->f_op = &cifs_file_direct_ops;
	}

	if (server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	/* Can not refresh inode by passing in file_info buf to be returned
	   by SMBOpen and then calling get_inode_info with returned buf
	   since file might have write behind data that needs to be flushed
	   and server version of file size can be stale. If we knew for sure
	   that inode was not dirty locally we could do this */

	rc = CIFSSMBOpen(xid, pTcon, full_path, disposition, desiredAccess,
			 CREATE_NOT_DIR, &netfid, &oplock, NULL,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc) {
		up(&pCifsFile->fh_sem);
		cFYI(1, ("cifs_open returned 0x%x", rc));
		cFYI(1, ("oplock: %d", oplock));
	} else {
		pCifsFile->netfid = netfid;
		pCifsFile->invalidHandle = false;
		up(&pCifsFile->fh_sem);
		pCifsInode = CIFS_I(inode);
		if (pCifsInode) {
			if (can_flush) {
				rc = filemap_write_and_wait(inode->i_mapping);
				if (rc != 0)
					CIFS_I(inode)->write_behind_rc = rc;
			/* temporarily disable caching while we
			   go to server to get inode info */
				pCifsInode->clientCanCacheAll = false;
				pCifsInode->clientCanCacheRead = false;
				if (pTcon->unix_ext)
					rc = cifs_get_inode_info_unix(&inode,
						full_path, inode->i_sb, xid);
				else
					rc = cifs_get_inode_info(&inode,
						full_path, NULL, inode->i_sb,
						xid, NULL);
			} /* else we are writing out data to server already
			     and could deadlock if we tried to flush data, and
			     since we do not know if we have data that would
			     invalidate the current end of file on the server
			     we can not go to the server to get the new inod
			     info */
			if ((oplock & 0xF) == OPLOCK_EXCLUSIVE) {
				pCifsInode->clientCanCacheAll = true;
				pCifsInode->clientCanCacheRead = true;
				cFYI(1, ("Exclusive Oplock granted on inode %p",
					 file->f_path.dentry->d_inode));
			} else if ((oplock & 0xF) == OPLOCK_READ) {
				pCifsInode->clientCanCacheRead = true;
				pCifsInode->clientCanCacheAll = false;
			} else {
				pCifsInode->clientCanCacheRead = false;
				pCifsInode->clientCanCacheAll = false;
			}
			cifs_relock_file(pCifsFile);
		}
	}

	kfree(full_path);
	FreeXid(xid);
	if (!tcon->broken_posix_open && tcon->unix_ext &&
	    cap_unix(tcon->ses) && (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		/* can not refresh inode info since size could be stale */
		rc = cifs_posix_open(full_path, &inode, inode->i_sb,
				cifs_sb->mnt_file_mode /* ignored */,
				file->f_flags, &oplock, &fid.netfid, xid);
		if (rc == 0) {
			cifs_dbg(FYI, "posix open succeeded\n");
			posix_open_ok = true;
		} else if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			if (tcon->ses->serverNOS)
				cifs_dbg(VFS, "server %s of type %s returned unexpected error on SMB posix open, disabling posix open support. Check if server update available.\n",
					 tcon->ses->serverName,
					 tcon->ses->serverNOS);
			tcon->broken_posix_open = true;
		} else if ((rc != -EIO) && (rc != -EREMOTE) &&
			 (rc != -EOPNOTSUPP)) /* path not found or net err */
			goto out;
		/*
		 * Else fallthrough to retry open the old way on network i/o
		 * or DFS errors.
		 */
	}

	if (server->ops->get_lease_key)
		server->ops->get_lease_key(inode, &fid);

	cifs_add_pending_open(&fid, tlink, &open);

	if (!posix_open_ok) {
		if (server->ops->get_lease_key)
			server->ops->get_lease_key(inode, &fid);

		rc = cifs_nt_open(full_path, inode, cifs_sb, tcon,
				  file->f_flags, &oplock, &fid, xid);
		if (rc) {
			cifs_del_pending_open(&open);
			goto out;
		}
	}

	cfile = cifs_new_fileinfo(&fid, file, tlink, oplock);
	if (cfile == NULL) {
		if (server->ops->close)
			server->ops->close(xid, tcon, &fid);
		cifs_del_pending_open(&open);
		rc = -ENOMEM;
		goto out;
	}

	cifs_fscache_set_inode_cookie(inode, file);

	if ((oplock & CIFS_CREATE_ACTION) && !posix_open_ok && tcon->unix_ext) {
		/*
		 * Time to set mode which we can not set earlier due to
		 * problems creating new read-only files.
		 */
		struct cifs_unix_set_info_args args = {
			.mode	= inode->i_mode,
			.uid	= INVALID_UID, /* no change */
			.gid	= INVALID_GID, /* no change */
			.ctime	= NO_CHANGE_64,
			.atime	= NO_CHANGE_64,
			.mtime	= NO_CHANGE_64,
			.device	= 0,
		};
		CIFSSMBUnixSetFileInfo(xid, tcon, &args, fid.netfid,
				       cfile->pid);
	}

out:
	kfree(full_path);
	free_xid(xid);
	cifs_put_tlink(tlink);
	return rc;
}

static int cifs_push_posix_locks(struct cifsFileInfo *cfile);

/*
 * Try to reacquire byte range locks that were released when session
 * to server was lost.
 */
static int
cifs_relock_file(struct cifsFileInfo *cfile)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	int rc = 0;

	down_read_nested(&cinode->lock_sem, SINGLE_DEPTH_NESTING);
	if (cinode->can_cache_brlcks) {
		/* can cache locks - no need to relock */
		up_read(&cinode->lock_sem);
		return rc;
	}

	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		rc = cifs_push_posix_locks(cfile);
	else
		rc = tcon->ses->server->ops->push_mand_locks(cfile);

	up_read(&cinode->lock_sem);
	return rc;
}

static int
cifs_reopen_file(struct cifsFileInfo *cfile, bool can_flush)
{
	int rc = -EACCES;
	unsigned int xid;
	__u32 oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsInodeInfo *cinode;
	struct inode *inode;
	char *full_path = NULL;
	int desired_access;
	int disposition = FILE_OPEN;
	int create_options = CREATE_NOT_DIR;
	struct cifs_open_parms oparms;

	xid = get_xid();
	mutex_lock(&cfile->fh_mutex);
	if (!cfile->invalidHandle) {
		mutex_unlock(&cfile->fh_mutex);
		rc = 0;
		free_xid(xid);
		return rc;
	}

	inode = d_inode(cfile->dentry);
	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = tlink_tcon(cfile->tlink);
	server = tcon->ses->server;

	/*
	 * Can not grab rename sem here because various ops, including those
	 * that already have the rename sem can end up causing writepage to get
	 * called and if the server was down that means we end up here, and we
	 * can never tell if the caller already has the rename_sem.
	 */
	full_path = build_path_from_dentry(cfile->dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		mutex_unlock(&cfile->fh_mutex);
		free_xid(xid);
		return rc;
	}

	cifs_dbg(FYI, "inode = 0x%p file flags 0x%x for %s\n",
		 inode, cfile->f_flags, full_path);

	if (tcon->ses->server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	if (tcon->unix_ext && cap_unix(tcon->ses) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
				le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		/*
		 * O_CREAT, O_EXCL and O_TRUNC already had their effect on the
		 * original open. Must mask them off for a reopen.
		 */
		unsigned int oflags = cfile->f_flags &
						~(O_CREAT | O_EXCL | O_TRUNC);

		rc = cifs_posix_open(full_path, NULL, inode->i_sb,
				     cifs_sb->mnt_file_mode /* ignored */,
				     oflags, &oplock, &cfile->fid.netfid, xid);
		if (rc == 0) {
			cifs_dbg(FYI, "posix reopen succeeded\n");
			oparms.reconnect = true;
			goto reopen_success;
		}
		/*
		 * fallthrough to retry open the old way on errors, especially
		 * in the reconnect path it is important to retry hard
		 */
	}

	desired_access = cifs_convert_flags(cfile->f_flags);

	if (backup_cred(cifs_sb))
		create_options |= CREATE_OPEN_BACKUP_INTENT;

	/* O_SYNC also has bit for O_DSYNC so following check picks up either */
	if (cfile->f_flags & O_SYNC)
		create_options |= CREATE_WRITE_THROUGH;

	if (cfile->f_flags & O_DIRECT)
		create_options |= CREATE_NO_BUFFER;

	if (server->ops->get_lease_key)
		server->ops->get_lease_key(inode, &cfile->fid);

	oparms.tcon = tcon;
	oparms.cifs_sb = cifs_sb;
	oparms.desired_access = desired_access;
	oparms.create_options = create_options;
	oparms.disposition = disposition;
	oparms.path = full_path;
	oparms.fid = &cfile->fid;
	oparms.reconnect = true;

	/*
	 * Can not refresh inode by passing in file_info buf to be returned by
	 * ops->open and then calling get_inode_info with returned buf since
	 * file might have write behind data that needs to be flushed and server
	 * version of file size can be stale. If we knew for sure that inode was
	 * not dirty locally we could do this.
	 */
	rc = server->ops->open(xid, &oparms, &oplock, NULL);
	if (rc == -ENOENT && oparms.reconnect == false) {
		/* durable handle timeout is expired - open the file again */
		rc = server->ops->open(xid, &oparms, &oplock, NULL);
		/* indicate that we need to relock the file */
		oparms.reconnect = true;
	}

	if (rc) {
		mutex_unlock(&cfile->fh_mutex);
		cifs_dbg(FYI, "cifs_reopen returned 0x%x\n", rc);
		cifs_dbg(FYI, "oplock: %d\n", oplock);
		goto reopen_error_exit;
	}

reopen_success:
	cfile->invalidHandle = false;
	mutex_unlock(&cfile->fh_mutex);
	cinode = CIFS_I(inode);

	if (can_flush) {
		rc = filemap_write_and_wait(inode->i_mapping);
		mapping_set_error(inode->i_mapping, rc);

		if (tcon->unix_ext)
			rc = cifs_get_inode_info_unix(&inode, full_path,
						      inode->i_sb, xid);
		else
			rc = cifs_get_inode_info(&inode, full_path, NULL,
						 inode->i_sb, xid, NULL);
	}
	/*
	 * Else we are writing out data to server already and could deadlock if
	 * we tried to flush data, and since we do not know if we have data that
	 * would invalidate the current end of file on the server we can not go
	 * to the server to get the new inode info.
	 */

	server->ops->set_fid(cfile, &cfile->fid, oplock);
	if (oparms.reconnect)
		cifs_relock_file(cfile);

reopen_error_exit:
	kfree(full_path);
	free_xid(xid);
	return rc;
}

int cifs_close(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid, timeout;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *pSMBFile =
		(struct cifsFileInfo *)file->private_data;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;
	if (pSMBFile) {
		struct cifsLockInfo *li, *tmp;

		pSMBFile->closePend = true;
		if (pTcon) {
			/* no sense reconnecting to close a file that is
			   already closed */
			if (pTcon->tidStatus != CifsNeedReconnect) {
				timeout = 2;
				while ((atomic_read(&pSMBFile->wrtPending) != 0)
					&& (timeout <= 2048)) {
					/* Give write a better chance to get to
					server ahead of the close.  We do not
					want to add a wait_q here as it would
					increase the memory utilization as
					the struct would be in each open file,
					but this should give enough time to
					clear the socket */
					cFYI(DBG2,
						("close delay, write pending"));
					msleep(timeout);
					timeout *= 4;
				}
				if (atomic_read(&pSMBFile->wrtPending))
					cERROR(1,
						("close with pending writes"));
				rc = CIFSSMBClose(xid, pTcon,
						  pSMBFile->netfid);
			}
		}

		/* Delete any outstanding lock records.
		   We'll lose them when the file is closed anyway. */
		mutex_lock(&pSMBFile->lock_mutex);
		list_for_each_entry_safe(li, tmp, &pSMBFile->llist, llist) {
			list_del(&li->llist);
			kfree(li);
		}
		mutex_unlock(&pSMBFile->lock_mutex);

		write_lock(&GlobalSMBSeslock);
		list_del(&pSMBFile->flist);
		list_del(&pSMBFile->tlist);
		write_unlock(&GlobalSMBSeslock);
		timeout = 10;
		/* We waited above to give the SMBWrite a chance to issue
		   on the wire (so we do not get SMBWrite returning EBADF
		   if writepages is racing with close.  Note that writepages
		   does not specify a file handle, so it is possible for a file
		   to be opened twice, and the application close the "wrong"
		   file handle - in these cases we delay long enough to allow
		   the SMBWrite to get on the wire before the SMB Close.
		   We allow total wait here over 45 seconds, more than
		   oplock break time, and more than enough to allow any write
		   to complete on the server, or to time out on the client */
		while ((atomic_read(&pSMBFile->wrtPending) != 0)
				&& (timeout <= 50000)) {
			cERROR(1, ("writes pending, delay free of handle"));
			msleep(timeout);
			timeout *= 8;
		}
		kfree(file->private_data);
		file->private_data = NULL;
	} else
		rc = -EBADF;

	read_lock(&GlobalSMBSeslock);
	if (list_empty(&(CIFS_I(inode)->openFileList))) {
		cFYI(1, ("closing last open instance for inode %p", inode));
		/* if the file is not open we do not know if we can cache info
		   on this inode, much less write behind and read ahead */
		CIFS_I(inode)->clientCanCacheRead = false;
		CIFS_I(inode)->clientCanCacheAll  = false;
	}
	read_unlock(&GlobalSMBSeslock);
	if ((rc == 0) && CIFS_I(inode)->write_behind_rc)
		rc = CIFS_I(inode)->write_behind_rc;
	FreeXid(xid);
	return rc;
	if (file->private_data != NULL) {
		cifsFileInfo_put(file->private_data);
		file->private_data = NULL;
	}

	/* return code from the ->release op is always ignored */
	return 0;
}

int cifs_closedir(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid;
	struct cifsFileInfo *pCFileStruct =
	    (struct cifsFileInfo *)file->private_data;
	char *ptmp;

	cFYI(1, ("Closedir inode = 0x%p", inode));

	xid = GetXid();

	if (pCFileStruct) {
		struct cifsTconInfo *pTcon;
		struct cifs_sb_info *cifs_sb =
			CIFS_SB(file->f_path.dentry->d_sb);

		pTcon = cifs_sb->tcon;

		cFYI(1, ("Freeing private data in close dir"));
		if (!pCFileStruct->srch_inf.endOfSearch &&
		    !pCFileStruct->invalidHandle) {
			pCFileStruct->invalidHandle = true;
			rc = CIFSFindClose(xid, pTcon, pCFileStruct->netfid);
			cFYI(1, ("Closing uncompleted readdir with rc %d",
				 rc));
			/* not much we can do if it fails anyway, ignore rc */
			rc = 0;
		}
		ptmp = pCFileStruct->srch_inf.ntwrk_buf_start;
		if (ptmp) {
			cFYI(1, ("closedir free smb buf in srch struct"));
			pCFileStruct->srch_inf.ntwrk_buf_start = NULL;
			if (pCFileStruct->srch_inf.smallBuf)
				cifs_small_buf_release(ptmp);
			else
				cifs_buf_release(ptmp);
		}
		kfree(file->private_data);
		file->private_data = NULL;
	}
	/* BB can we lock the filestruct while this is going on? */
	FreeXid(xid);
	return rc;
}

static int store_file_lock(struct cifsFileInfo *fid, __u64 len,
				__u64 offset, __u8 lockType)
{
	struct cifsLockInfo *li =
		kmalloc(sizeof(struct cifsLockInfo), GFP_KERNEL);
	if (li == NULL)
		return -ENOMEM;
	li->offset = offset;
	li->length = len;
	li->type = lockType;
	mutex_lock(&fid->lock_mutex);
	list_add(&li->llist, &fid->llist);
	mutex_unlock(&fid->lock_mutex);
	return 0;
}

int cifs_lock(struct file *file, int cmd, struct file_lock *pfLock)
{
	int rc, xid;
	__u32 numLock = 0;
	__u32 numUnlock = 0;
	__u64 length;
	bool wait_flag = false;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	__u16 netfid;
	__u8 lockType = LOCKING_ANDX_LARGE_FILES;
	bool posix_locking;

	length = 1 + pfLock->fl_end - pfLock->fl_start;
	rc = -EACCES;
	xid = GetXid();

	cFYI(1, ("Lock parm: 0x%x flockflags: "
		 "0x%x flocktype: 0x%x start: %lld end: %lld",
		cmd, pfLock->fl_flags, pfLock->fl_type, pfLock->fl_start,
		pfLock->fl_end));

	if (pfLock->fl_flags & FL_POSIX)
		cFYI(1, ("Posix"));
	if (pfLock->fl_flags & FL_FLOCK)
		cFYI(1, ("Flock"));
	if (pfLock->fl_flags & FL_SLEEP) {
		cFYI(1, ("Blocking lock"));
		wait_flag = true;
	}
	if (pfLock->fl_flags & FL_ACCESS)
		cFYI(1, ("Process suspended by mandatory locking - "
			 "not implemented yet"));
	if (pfLock->fl_flags & FL_LEASE)
		cFYI(1, ("Lease on file - not implemented yet"));
	if (pfLock->fl_flags &
	    (~(FL_POSIX | FL_FLOCK | FL_SLEEP | FL_ACCESS | FL_LEASE)))
		cFYI(1, ("Unknown lock flags 0x%x", pfLock->fl_flags));

	if (pfLock->fl_type == F_WRLCK) {
		cFYI(1, ("F_WRLCK "));
		numLock = 1;
	} else if (pfLock->fl_type == F_UNLCK) {
		cFYI(1, ("F_UNLCK"));
		numUnlock = 1;
		/* Check if unlock includes more than
		one lock range */
	} else if (pfLock->fl_type == F_RDLCK) {
		cFYI(1, ("F_RDLCK"));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else if (pfLock->fl_type == F_EXLCK) {
		cFYI(1, ("F_EXLCK"));
		numLock = 1;
	} else if (pfLock->fl_type == F_SHLCK) {
		cFYI(1, ("F_SHLCK"));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else
		cFYI(1, ("Unknown type of lock"));

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	netfid = ((struct cifsFileInfo *)file->private_data)->netfid;

	posix_locking = (cifs_sb->tcon->ses->capabilities & CAP_UNIX) &&
			(CIFS_UNIX_FCNTL_CAP & le64_to_cpu(cifs_sb->tcon->fsUnixInfo.Capability));

	/* BB add code here to normalize offset and length to
	account for negative length which we can not accept over the
	wire */
	if (IS_GETLK(cmd)) {
		if (posix_locking) {
			int posix_lock_type;
			if (lockType & LOCKING_ANDX_SHARED_LOCK)
				posix_lock_type = CIFS_RDLCK;
			else
				posix_lock_type = CIFS_WRLCK;
			rc = CIFSSMBPosixLock(xid, pTcon, netfid, 1 /* get */,
					length,	pfLock,
					posix_lock_type, wait_flag);
			FreeXid(xid);
			return rc;
		}

		/* BB we could chain these into one lock request BB */
		rc = CIFSSMBLock(xid, pTcon, netfid, length, pfLock->fl_start,
				 0, 1, lockType, 0 /* wait flag */ );
		if (rc == 0) {
			rc = CIFSSMBLock(xid, pTcon, netfid, length,
					 pfLock->fl_start, 1 /* numUnlock */ ,
					 0 /* numLock */ , lockType,
					 0 /* wait flag */ );
			pfLock->fl_type = F_UNLCK;
			if (rc != 0)
				cERROR(1, ("Error unlocking previously locked "
					   "range %d during test of lock", rc));
			rc = 0;

		} else {
			/* if rc == ERR_SHARING_VIOLATION ? */
			rc = 0;	/* do not change lock type to unlock
				   since range in use */
		}

		FreeXid(xid);
		return rc;
	}

	if (!numLock && !numUnlock) {
		/* if no lock or unlock then nothing
		to do since we do not know what it is */
		FreeXid(xid);
		return -EOPNOTSUPP;
	}

	if (posix_locking) {
		int posix_lock_type;
		if (lockType & LOCKING_ANDX_SHARED_LOCK)
	unsigned int xid;
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	char *buf;

	cifs_dbg(FYI, "Closedir inode = 0x%p\n", inode);

	if (cfile == NULL)
		return rc;

	xid = get_xid();
	tcon = tlink_tcon(cfile->tlink);
	server = tcon->ses->server;

	cifs_dbg(FYI, "Freeing private data in close dir\n");
	spin_lock(&cfile->file_info_lock);
	if (server->ops->dir_needs_close(cfile)) {
		cfile->invalidHandle = true;
		spin_unlock(&cfile->file_info_lock);
		if (server->ops->close_dir)
			rc = server->ops->close_dir(xid, tcon, &cfile->fid);
		else
			rc = -ENOSYS;
		cifs_dbg(FYI, "Closing uncompleted readdir with rc %d\n", rc);
		/* not much we can do if it fails anyway, ignore rc */
		rc = 0;
	} else
		spin_unlock(&cfile->file_info_lock);

	buf = cfile->srch_inf.ntwrk_buf_start;
	if (buf) {
		cifs_dbg(FYI, "closedir free smb buf in srch struct\n");
		cfile->srch_inf.ntwrk_buf_start = NULL;
		if (cfile->srch_inf.smallBuf)
			cifs_small_buf_release(buf);
		else
			cifs_buf_release(buf);
	}

	cifs_put_tlink(cfile->tlink);
	kfree(file->private_data);
	file->private_data = NULL;
	/* BB can we lock the filestruct while this is going on? */
	free_xid(xid);
	return rc;
}

static struct cifsLockInfo *
cifs_lock_init(__u64 offset, __u64 length, __u8 type)
{
	struct cifsLockInfo *lock =
		kmalloc(sizeof(struct cifsLockInfo), GFP_KERNEL);
	if (!lock)
		return lock;
	lock->offset = offset;
	lock->length = length;
	lock->type = type;
	lock->pid = current->tgid;
	INIT_LIST_HEAD(&lock->blist);
	init_waitqueue_head(&lock->block_q);
	return lock;
}

void
cifs_del_lock_waiters(struct cifsLockInfo *lock)
{
	struct cifsLockInfo *li, *tmp;
	list_for_each_entry_safe(li, tmp, &lock->blist, blist) {
		list_del_init(&li->blist);
		wake_up(&li->block_q);
	}
}

#define CIFS_LOCK_OP	0
#define CIFS_READ_OP	1
#define CIFS_WRITE_OP	2

/* @rw_check : 0 - no op, 1 - read, 2 - write */
static bool
cifs_find_fid_lock_conflict(struct cifs_fid_locks *fdlocks, __u64 offset,
			    __u64 length, __u8 type, struct cifsFileInfo *cfile,
			    struct cifsLockInfo **conf_lock, int rw_check)
{
	struct cifsLockInfo *li;
	struct cifsFileInfo *cur_cfile = fdlocks->cfile;
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;

	list_for_each_entry(li, &fdlocks->locks, llist) {
		if (offset + length <= li->offset ||
		    offset >= li->offset + li->length)
			continue;
		if (rw_check != CIFS_LOCK_OP && current->tgid == li->pid &&
		    server->ops->compare_fids(cfile, cur_cfile)) {
			/* shared lock prevents write op through the same fid */
			if (!(li->type & server->vals->shared_lock_type) ||
			    rw_check != CIFS_WRITE_OP)
				continue;
		}
		if ((type & server->vals->shared_lock_type) &&
		    ((server->ops->compare_fids(cfile, cur_cfile) &&
		     current->tgid == li->pid) || type == li->type))
			continue;
		if (conf_lock)
			*conf_lock = li;
		return true;
	}
	return false;
}

bool
cifs_find_lock_conflict(struct cifsFileInfo *cfile, __u64 offset, __u64 length,
			__u8 type, struct cifsLockInfo **conf_lock,
			int rw_check)
{
	bool rc = false;
	struct cifs_fid_locks *cur;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));

	list_for_each_entry(cur, &cinode->llist, llist) {
		rc = cifs_find_fid_lock_conflict(cur, offset, length, type,
						 cfile, conf_lock, rw_check);
		if (rc)
			break;
	}

	return rc;
}

/*
 * Check if there is another lock that prevents us to set the lock (mandatory
 * style). If such a lock exists, update the flock structure with its
 * properties. Otherwise, set the flock type to F_UNLCK if we can cache brlocks
 * or leave it the same if we can't. Returns 0 if we don't need to request to
 * the server or 1 otherwise.
 */
static int
cifs_lock_test(struct cifsFileInfo *cfile, __u64 offset, __u64 length,
	       __u8 type, struct file_lock *flock)
{
	int rc = 0;
	struct cifsLockInfo *conf_lock;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;
	bool exist;

	down_read(&cinode->lock_sem);

	exist = cifs_find_lock_conflict(cfile, offset, length, type,
					&conf_lock, CIFS_LOCK_OP);
	if (exist) {
		flock->fl_start = conf_lock->offset;
		flock->fl_end = conf_lock->offset + conf_lock->length - 1;
		flock->fl_pid = conf_lock->pid;
		if (conf_lock->type & server->vals->shared_lock_type)
			flock->fl_type = F_RDLCK;
		else
			flock->fl_type = F_WRLCK;
	} else if (!cinode->can_cache_brlcks)
		rc = 1;
	else
		flock->fl_type = F_UNLCK;

	up_read(&cinode->lock_sem);
	return rc;
}

static void
cifs_lock_add(struct cifsFileInfo *cfile, struct cifsLockInfo *lock)
{
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	cifs_down_write(&cinode->lock_sem);
	list_add_tail(&lock->llist, &cfile->llist->locks);
	up_write(&cinode->lock_sem);
}

/*
 * Set the byte-range lock (mandatory style). Returns:
 * 1) 0, if we set the lock and don't need to request to the server;
 * 2) 1, if no locks prevent us but we need to request to the server;
 * 3) -EACCESS, if there is a lock that prevents us and wait is false.
 */
static int
cifs_lock_add_if(struct cifsFileInfo *cfile, struct cifsLockInfo *lock,
		 bool wait)
{
	struct cifsLockInfo *conf_lock;
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	bool exist;
	int rc = 0;

try_again:
	exist = false;
	cifs_down_write(&cinode->lock_sem);

	exist = cifs_find_lock_conflict(cfile, lock->offset, lock->length,
					lock->type, &conf_lock, CIFS_LOCK_OP);
	if (!exist && cinode->can_cache_brlcks) {
		list_add_tail(&lock->llist, &cfile->llist->locks);
		up_write(&cinode->lock_sem);
		return rc;
	}

	if (!exist)
		rc = 1;
	else if (!wait)
		rc = -EACCES;
	else {
		list_add_tail(&lock->blist, &conf_lock->blist);
		up_write(&cinode->lock_sem);
		rc = wait_event_interruptible(lock->block_q,
					(lock->blist.prev == &lock->blist) &&
					(lock->blist.next == &lock->blist));
		if (!rc)
			goto try_again;
		cifs_down_write(&cinode->lock_sem);
		list_del_init(&lock->blist);
	}

	up_write(&cinode->lock_sem);
	return rc;
}

/*
 * Check if there is another lock that prevents us to set the lock (posix
 * style). If such a lock exists, update the flock structure with its
 * properties. Otherwise, set the flock type to F_UNLCK if we can cache brlocks
 * or leave it the same if we can't. Returns 0 if we don't need to request to
 * the server or 1 otherwise.
 */
static int
cifs_posix_lock_test(struct file *file, struct file_lock *flock)
{
	int rc = 0;
	struct cifsInodeInfo *cinode = CIFS_I(file_inode(file));
	unsigned char saved_type = flock->fl_type;

	if ((flock->fl_flags & FL_POSIX) == 0)
		return 1;

	down_read(&cinode->lock_sem);
	posix_test_lock(file, flock);

	if (flock->fl_type == F_UNLCK && !cinode->can_cache_brlcks) {
		flock->fl_type = saved_type;
		rc = 1;
	}

	up_read(&cinode->lock_sem);
	return rc;
}

/*
 * Set the byte-range lock (posix style). Returns:
 * 1) 0, if we set the lock and don't need to request to the server;
 * 2) 1, if we need to request to the server;
 * 3) <0, if the error occurs while setting the lock.
 */
static int
cifs_posix_lock_set(struct file *file, struct file_lock *flock)
{
	struct cifsInodeInfo *cinode = CIFS_I(file_inode(file));
	int rc = 1;

	if ((flock->fl_flags & FL_POSIX) == 0)
		return rc;

try_again:
	cifs_down_write(&cinode->lock_sem);
	if (!cinode->can_cache_brlcks) {
		up_write(&cinode->lock_sem);
		return rc;
	}

	rc = posix_lock_file(file, flock, NULL);
	up_write(&cinode->lock_sem);
	if (rc == FILE_LOCK_DEFERRED) {
		rc = wait_event_interruptible(flock->fl_wait, !flock->fl_next);
		if (!rc)
			goto try_again;
		posix_unblock_lock(flock);
	}
	return rc;
}

int
cifs_push_mandatory_locks(struct cifsFileInfo *cfile)
{
	unsigned int xid;
	int rc = 0, stored_rc;
	struct cifsLockInfo *li, *tmp;
	struct cifs_tcon *tcon;
	unsigned int num, max_num, max_buf;
	LOCKING_ANDX_RANGE *buf, *cur;
	int types[] = {LOCKING_ANDX_LARGE_FILES,
		       LOCKING_ANDX_SHARED_LOCK | LOCKING_ANDX_LARGE_FILES};
	int i;

	xid = get_xid();
	tcon = tlink_tcon(cfile->tlink);

	/*
	 * Accessing maxBuf is racy with cifs_reconnect - need to store value
	 * and check it before using.
	 */
	max_buf = tcon->ses->server->maxBuf;
	if (max_buf < (sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE))) {
		free_xid(xid);
		return -EINVAL;
	}

	BUILD_BUG_ON(sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE) >
		     PAGE_SIZE);
	max_buf = min_t(unsigned int, max_buf - sizeof(struct smb_hdr),
			PAGE_SIZE);
	max_num = (max_buf - sizeof(struct smb_hdr)) /
						sizeof(LOCKING_ANDX_RANGE);
	buf = kcalloc(max_num, sizeof(LOCKING_ANDX_RANGE), GFP_KERNEL);
	if (!buf) {
		free_xid(xid);
		return -ENOMEM;
	}

	for (i = 0; i < 2; i++) {
		cur = buf;
		num = 0;
		list_for_each_entry_safe(li, tmp, &cfile->llist->locks, llist) {
			if (li->type != types[i])
				continue;
			cur->Pid = cpu_to_le16(li->pid);
			cur->LengthLow = cpu_to_le32((u32)li->length);
			cur->LengthHigh = cpu_to_le32((u32)(li->length>>32));
			cur->OffsetLow = cpu_to_le32((u32)li->offset);
			cur->OffsetHigh = cpu_to_le32((u32)(li->offset>>32));
			if (++num == max_num) {
				stored_rc = cifs_lockv(xid, tcon,
						       cfile->fid.netfid,
						       (__u8)li->type, 0, num,
						       buf);
				if (stored_rc)
					rc = stored_rc;
				cur = buf;
				num = 0;
			} else
				cur++;
		}

		if (num) {
			stored_rc = cifs_lockv(xid, tcon, cfile->fid.netfid,
					       (__u8)types[i], 0, num, buf);
			if (stored_rc)
				rc = stored_rc;
		}
	}

	kfree(buf);
	free_xid(xid);
	return rc;
}

struct lock_to_push {
	struct list_head llist;
	__u64 offset;
	__u64 length;
	__u32 pid;
	__u16 netfid;
	__u8 type;
};

static int
cifs_push_posix_locks(struct cifsFileInfo *cfile)
{
	struct inode *inode = d_inode(cfile->dentry);
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct file_lock *flock;
	struct file_lock_context *flctx = inode->i_flctx;
	unsigned int count = 0, i;
	int rc = 0, xid, type;
	struct list_head locks_to_send, *el;
	struct lock_to_push *lck, *tmp;
	__u64 length;

	xid = get_xid();

	if (!flctx)
		goto out;

	spin_lock(&flctx->flc_lock);
	list_for_each(el, &flctx->flc_posix) {
		count++;
	}
	spin_unlock(&flctx->flc_lock);

	INIT_LIST_HEAD(&locks_to_send);

	/*
	 * Allocating count locks is enough because no FL_POSIX locks can be
	 * added to the list while we are holding cinode->lock_sem that
	 * protects locking operations of this inode.
	 */
	for (i = 0; i < count; i++) {
		lck = kmalloc(sizeof(struct lock_to_push), GFP_KERNEL);
		if (!lck) {
			rc = -ENOMEM;
			goto err_out;
		}
		list_add_tail(&lck->llist, &locks_to_send);
	}

	el = locks_to_send.next;
	spin_lock(&flctx->flc_lock);
	list_for_each_entry(flock, &flctx->flc_posix, fl_list) {
		if (el == &locks_to_send) {
			/*
			 * The list ended. We don't have enough allocated
			 * structures - something is really wrong.
			 */
			cifs_dbg(VFS, "Can't push all brlocks!\n");
			break;
		}
		length = 1 + flock->fl_end - flock->fl_start;
		if (flock->fl_type == F_RDLCK || flock->fl_type == F_SHLCK)
			type = CIFS_RDLCK;
		else
			type = CIFS_WRLCK;
		lck = list_entry(el, struct lock_to_push, llist);
		lck->pid = flock->fl_pid;
		lck->netfid = cfile->fid.netfid;
		lck->length = length;
		lck->type = type;
		lck->offset = flock->fl_start;
	}
	spin_unlock(&flctx->flc_lock);

	list_for_each_entry_safe(lck, tmp, &locks_to_send, llist) {
		int stored_rc;

		stored_rc = CIFSSMBPosixLock(xid, tcon, lck->netfid, lck->pid,
					     lck->offset, lck->length, NULL,
					     lck->type, 0);
		if (stored_rc)
			rc = stored_rc;
		list_del(&lck->llist);
		kfree(lck);
	}

out:
	free_xid(xid);
	return rc;
err_out:
	list_for_each_entry_safe(lck, tmp, &locks_to_send, llist) {
		list_del(&lck->llist);
		kfree(lck);
	}
	goto out;
}

static int
cifs_push_locks(struct cifsFileInfo *cfile)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	int rc = 0;

	/* we are going to update can_cache_brlcks here - need a write access */
	cifs_down_write(&cinode->lock_sem);
	if (!cinode->can_cache_brlcks) {
		up_write(&cinode->lock_sem);
		return rc;
	}

	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		rc = cifs_push_posix_locks(cfile);
	else
		rc = tcon->ses->server->ops->push_mand_locks(cfile);

	cinode->can_cache_brlcks = false;
	up_write(&cinode->lock_sem);
	return rc;
}

static void
cifs_read_flock(struct file_lock *flock, __u32 *type, int *lock, int *unlock,
		bool *wait_flag, struct TCP_Server_Info *server)
{
	if (flock->fl_flags & FL_POSIX)
		cifs_dbg(FYI, "Posix\n");
	if (flock->fl_flags & FL_FLOCK)
		cifs_dbg(FYI, "Flock\n");
	if (flock->fl_flags & FL_SLEEP) {
		cifs_dbg(FYI, "Blocking lock\n");
		*wait_flag = true;
	}
	if (flock->fl_flags & FL_ACCESS)
		cifs_dbg(FYI, "Process suspended by mandatory locking - not implemented yet\n");
	if (flock->fl_flags & FL_LEASE)
		cifs_dbg(FYI, "Lease on file - not implemented yet\n");
	if (flock->fl_flags &
	    (~(FL_POSIX | FL_FLOCK | FL_SLEEP |
	       FL_ACCESS | FL_LEASE | FL_CLOSE)))
		cifs_dbg(FYI, "Unknown lock flags 0x%x\n", flock->fl_flags);

	*type = server->vals->large_lock_type;
	if (flock->fl_type == F_WRLCK) {
		cifs_dbg(FYI, "F_WRLCK\n");
		*type |= server->vals->exclusive_lock_type;
		*lock = 1;
	} else if (flock->fl_type == F_UNLCK) {
		cifs_dbg(FYI, "F_UNLCK\n");
		*type |= server->vals->unlock_lock_type;
		*unlock = 1;
		/* Check if unlock includes more than one lock range */
	} else if (flock->fl_type == F_RDLCK) {
		cifs_dbg(FYI, "F_RDLCK\n");
		*type |= server->vals->shared_lock_type;
		*lock = 1;
	} else if (flock->fl_type == F_EXLCK) {
		cifs_dbg(FYI, "F_EXLCK\n");
		*type |= server->vals->exclusive_lock_type;
		*lock = 1;
	} else if (flock->fl_type == F_SHLCK) {
		cifs_dbg(FYI, "F_SHLCK\n");
		*type |= server->vals->shared_lock_type;
		*lock = 1;
	} else
		cifs_dbg(FYI, "Unknown type of lock\n");
}

static int
cifs_getlk(struct file *file, struct file_lock *flock, __u32 type,
	   bool wait_flag, bool posix_lck, unsigned int xid)
{
	int rc = 0;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	__u16 netfid = cfile->fid.netfid;

	if (posix_lck) {
		int posix_lock_type;

		rc = cifs_posix_lock_test(file, flock);
		if (!rc)
			return rc;

		if (type & server->vals->shared_lock_type)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;
		rc = CIFSSMBPosixLock(xid, tcon, netfid, current->tgid,
				      flock->fl_start, length, flock,
				      posix_lock_type, wait_flag);
		return rc;
	}

	rc = cifs_lock_test(cfile, flock->fl_start, length, type, flock);
	if (!rc)
		return rc;

	/* BB we could chain these into one lock request BB */
	rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length, type,
				    1, 0, false);
	if (rc == 0) {
		rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
					    type, 0, 1, false);
		flock->fl_type = F_UNLCK;
		if (rc != 0)
			cifs_dbg(VFS, "Error unlocking previously locked range %d during test of lock\n",
				 rc);
		return 0;
	}

	if (type & server->vals->shared_lock_type) {
		flock->fl_type = F_WRLCK;
		return 0;
	}

	type &= ~server->vals->exclusive_lock_type;

	rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
				    type | server->vals->shared_lock_type,
				    1, 0, false);
	if (rc == 0) {
		rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
			type | server->vals->shared_lock_type, 0, 1, false);
		flock->fl_type = F_RDLCK;
		if (rc != 0)
			cifs_dbg(VFS, "Error unlocking previously locked range %d during test of lock\n",
				 rc);
	} else
		flock->fl_type = F_WRLCK;

	return 0;
}

void
cifs_move_llist(struct list_head *source, struct list_head *dest)
{
	struct list_head *li, *tmp;
	list_for_each_safe(li, tmp, source)
		list_move(li, dest);
}

void
cifs_free_llist(struct list_head *llist)
{
	struct cifsLockInfo *li, *tmp;
	list_for_each_entry_safe(li, tmp, llist, llist) {
		cifs_del_lock_waiters(li);
		list_del(&li->llist);
		kfree(li);
	}
}

int
cifs_unlock_range(struct cifsFileInfo *cfile, struct file_lock *flock,
		  unsigned int xid)
{
	int rc = 0, stored_rc;
	int types[] = {LOCKING_ANDX_LARGE_FILES,
		       LOCKING_ANDX_SHARED_LOCK | LOCKING_ANDX_LARGE_FILES};
	unsigned int i;
	unsigned int max_num, num, max_buf;
	LOCKING_ANDX_RANGE *buf, *cur;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct cifsInodeInfo *cinode = CIFS_I(d_inode(cfile->dentry));
	struct cifsLockInfo *li, *tmp;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct list_head tmp_llist;

	INIT_LIST_HEAD(&tmp_llist);

	/*
	 * Accessing maxBuf is racy with cifs_reconnect - need to store value
	 * and check it before using.
	 */
	max_buf = tcon->ses->server->maxBuf;
	if (max_buf < (sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE)))
		return -EINVAL;

	BUILD_BUG_ON(sizeof(struct smb_hdr) + sizeof(LOCKING_ANDX_RANGE) >
		     PAGE_SIZE);
	max_buf = min_t(unsigned int, max_buf - sizeof(struct smb_hdr),
			PAGE_SIZE);
	max_num = (max_buf - sizeof(struct smb_hdr)) /
						sizeof(LOCKING_ANDX_RANGE);
	buf = kcalloc(max_num, sizeof(LOCKING_ANDX_RANGE), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	cifs_down_write(&cinode->lock_sem);
	for (i = 0; i < 2; i++) {
		cur = buf;
		num = 0;
		list_for_each_entry_safe(li, tmp, &cfile->llist->locks, llist) {
			if (flock->fl_start > li->offset ||
			    (flock->fl_start + length) <
			    (li->offset + li->length))
				continue;
			if (current->tgid != li->pid)
				continue;
			if (types[i] != li->type)
				continue;
			if (cinode->can_cache_brlcks) {
				/*
				 * We can cache brlock requests - simply remove
				 * a lock from the file's list.
				 */
				list_del(&li->llist);
				cifs_del_lock_waiters(li);
				kfree(li);
				continue;
			}
			cur->Pid = cpu_to_le16(li->pid);
			cur->LengthLow = cpu_to_le32((u32)li->length);
			cur->LengthHigh = cpu_to_le32((u32)(li->length>>32));
			cur->OffsetLow = cpu_to_le32((u32)li->offset);
			cur->OffsetHigh = cpu_to_le32((u32)(li->offset>>32));
			/*
			 * We need to save a lock here to let us add it again to
			 * the file's list if the unlock range request fails on
			 * the server.
			 */
			list_move(&li->llist, &tmp_llist);
			if (++num == max_num) {
				stored_rc = cifs_lockv(xid, tcon,
						       cfile->fid.netfid,
						       li->type, num, 0, buf);
				if (stored_rc) {
					/*
					 * We failed on the unlock range
					 * request - add all locks from the tmp
					 * list to the head of the file's list.
					 */
					cifs_move_llist(&tmp_llist,
							&cfile->llist->locks);
					rc = stored_rc;
				} else
					/*
					 * The unlock range request succeed -
					 * free the tmp list.
					 */
					cifs_free_llist(&tmp_llist);
				cur = buf;
				num = 0;
			} else
				cur++;
		}
		if (num) {
			stored_rc = cifs_lockv(xid, tcon, cfile->fid.netfid,
					       types[i], num, 0, buf);
			if (stored_rc) {
				cifs_move_llist(&tmp_llist,
						&cfile->llist->locks);
				rc = stored_rc;
			} else
				cifs_free_llist(&tmp_llist);
		}
	}

	up_write(&cinode->lock_sem);
	kfree(buf);
	return rc;
}

static int
cifs_setlk(struct file *file, struct file_lock *flock, __u32 type,
	   bool wait_flag, bool posix_lck, int lock, int unlock,
	   unsigned int xid)
{
	int rc = 0;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	struct inode *inode = d_inode(cfile->dentry);

	if (posix_lck) {
		int posix_lock_type;

		rc = cifs_posix_lock_set(file, flock);
		if (!rc || rc < 0)
			return rc;

		if (type & server->vals->shared_lock_type)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;

		if (numUnlock == 1)
			posix_lock_type = CIFS_UNLCK;

		rc = CIFSSMBPosixLock(xid, pTcon, netfid, 0 /* set */,
				      length, pfLock,
				      posix_lock_type, wait_flag);
	} else {
		struct cifsFileInfo *fid =
			(struct cifsFileInfo *)file->private_data;

		if (numLock) {
			rc = CIFSSMBLock(xid, pTcon, netfid, length,
					pfLock->fl_start,
					0, numLock, lockType, wait_flag);

			if (rc == 0) {
				/* For Windows locks we must store them. */
				rc = store_file_lock(fid, length,
						pfLock->fl_start, lockType);
			}
		} else if (numUnlock) {
			/* For each stored lock that this unlock overlaps
			   completely, unlock it. */
			int stored_rc = 0;
			struct cifsLockInfo *li, *tmp;

			rc = 0;
			mutex_lock(&fid->lock_mutex);
			list_for_each_entry_safe(li, tmp, &fid->llist, llist) {
				if (pfLock->fl_start <= li->offset &&
						(pfLock->fl_start + length) >=
						(li->offset + li->length)) {
					stored_rc = CIFSSMBLock(xid, pTcon,
							netfid,
							li->length, li->offset,
							1, 0, li->type, false);
					if (stored_rc)
						rc = stored_rc;

					list_del(&li->llist);
					kfree(li);
				}
			}
			mutex_unlock(&fid->lock_mutex);
		}
	}

	if (pfLock->fl_flags & FL_POSIX)
		posix_lock_file_wait(file, pfLock);
	FreeXid(xid);
	return rc;
}

ssize_t cifs_user_write(struct file *file, const char __user *write_data,
	size_t write_size, loff_t *poffset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;
	struct cifsFileInfo *open_file;

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	pTcon = cifs_sb->tcon;

	/* cFYI(1,
	   (" write %d bytes to offset %lld of %s", write_size,
	   *poffset, file->f_path.dentry->d_name.name)); */

	if (file->private_data == NULL)
		return -EBADF;
	open_file = (struct cifsFileInfo *) file->private_data;

	rc = generic_write_checks(file, poffset, &write_size, 0);
	if (rc)
		return rc;

	xid = GetXid();

	if (*poffset > file->f_path.dentry->d_inode->i_size)
		long_op = CIFS_VLONG_OP; /* writes past EOF take long time */
	else
		long_op = CIFS_LONG_OP;

	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if (file->private_data == NULL) {
				/* file has been closed on us */
				FreeXid(xid);
			/* if we have gotten here we have written some data
			   and blocked, and the file has been freed on us while
			   we blocked so return what we managed to write */
				return total_written;
			}
			if (open_file->closePend) {
				FreeXid(xid);
				if (total_written)
					return total_written;
				else
					return -EBADF;
			}
			if (open_file->invalidHandle) {
				/* we could deadlock if we called
				   filemap_fdatawait from here so tell
				   reopen_file not to flush data to server
				   now */
				rc = cifs_reopen_file(file, false);
				if (rc != 0)
					break;
			}

			rc = CIFSSMBWrite(xid, pTcon,
				open_file->netfid,
				min_t(const int, cifs_sb->wsize,
				      write_size - total_written),
				*poffset, &bytes_written,
				NULL, write_data + total_written, long_op);
		}
		if (rc || (bytes_written == 0)) {
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else
			*poffset += bytes_written;
		long_op = CIFS_STD_OP; /* subsequent writes fast -
				    15 seconds is plenty */
	}

	cifs_stats_bytes_written(pTcon, total_written);

	/* since the write may have blocked check these pointers again */
	if ((file->f_path.dentry) && (file->f_path.dentry->d_inode)) {
		struct inode *inode = file->f_path.dentry->d_inode;
/* Do not update local mtime - server will set its actual value on write
 *		inode->i_ctime = inode->i_mtime =
 * 			current_fs_time(inode->i_sb);*/
		if (total_written > 0) {
			spin_lock(&inode->i_lock);
			if (*poffset > file->f_path.dentry->d_inode->i_size)
				i_size_write(file->f_path.dentry->d_inode,
					*poffset);
			spin_unlock(&inode->i_lock);
		}
		mark_inode_dirty_sync(file->f_path.dentry->d_inode);
	}
	FreeXid(xid);
	return total_written;
}

static ssize_t cifs_write(struct file *file, const char *write_data,
	size_t write_size, loff_t *poffset)
		if (unlock == 1)
			posix_lock_type = CIFS_UNLCK;

		rc = CIFSSMBPosixLock(xid, tcon, cfile->fid.netfid,
				      current->tgid, flock->fl_start, length,
				      NULL, posix_lock_type, wait_flag);
		goto out;
	}

	if (lock) {
		struct cifsLockInfo *lock;

		lock = cifs_lock_init(flock->fl_start, length, type);
		if (!lock)
			return -ENOMEM;

		rc = cifs_lock_add_if(cfile, lock, wait_flag);
		if (rc < 0) {
			kfree(lock);
			return rc;
		}
		if (!rc)
			goto out;

		/*
		 * Windows 7 server can delay breaking lease from read to None
		 * if we set a byte-range lock on a file - break it explicitly
		 * before sending the lock to the server to be sure the next
		 * read won't conflict with non-overlapted locks due to
		 * pagereading.
		 */
		if (!CIFS_CACHE_WRITE(CIFS_I(inode)) &&
					CIFS_CACHE_READ(CIFS_I(inode))) {
			cifs_zap_mapping(inode);
			cifs_dbg(FYI, "Set no oplock for inode=%p due to mand locks\n",
				 inode);
			CIFS_I(inode)->oplock = 0;
		}

		rc = server->ops->mand_lock(xid, cfile, flock->fl_start, length,
					    type, 1, 0, wait_flag);
		if (rc) {
			kfree(lock);
			return rc;
		}

		cifs_lock_add(cfile, lock);
	} else if (unlock)
		rc = server->ops->mand_unlock_range(cfile, flock, xid);

out:
	if (flock->fl_flags & FL_POSIX) {
		/*
		 * If this is a request to remove all locks because we
		 * are closing the file, it doesn't matter if the
		 * unlocking failed as both cifs.ko and the SMB server
		 * remove the lock on file close
		 */
		if (rc) {
			cifs_dbg(VFS, "%s failed rc=%d\n", __func__, rc);
			if (!(flock->fl_flags & FL_CLOSE))
				return rc;
		}
		rc = locks_lock_file_wait(file, flock);
	}
	return rc;
}

int cifs_lock(struct file *file, int cmd, struct file_lock *flock)
{
	int rc, xid;
	int lock = 0, unlock = 0;
	bool wait_flag = false;
	bool posix_lck = false;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *cinode;
	struct cifsFileInfo *cfile;
	__u16 netfid;
	__u32 type;

	rc = -EACCES;
	xid = get_xid();

	cifs_dbg(FYI, "Lock parm: 0x%x flockflags: 0x%x flocktype: 0x%x start: %lld end: %lld\n",
		 cmd, flock->fl_flags, flock->fl_type,
		 flock->fl_start, flock->fl_end);

	cfile = (struct cifsFileInfo *)file->private_data;
	tcon = tlink_tcon(cfile->tlink);

	cifs_read_flock(flock, &type, &lock, &unlock, &wait_flag,
			tcon->ses->server);

	cifs_sb = CIFS_FILE_SB(file);
	netfid = cfile->fid.netfid;
	cinode = CIFS_I(file_inode(file));

	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		posix_lck = true;
	/*
	 * BB add code here to normalize offset and length to account for
	 * negative length which we can not accept over the wire.
	 */
	if (IS_GETLK(cmd)) {
		rc = cifs_getlk(file, flock, type, wait_flag, posix_lck, xid);
		free_xid(xid);
		return rc;
	}

	if (!lock && !unlock) {
		/*
		 * if no lock or unlock then nothing to do since we do not
		 * know what it is
		 */
		free_xid(xid);
		return -EOPNOTSUPP;
	}

	rc = cifs_setlk(file, flock, type, wait_flag, posix_lck, lock, unlock,
			xid);
	free_xid(xid);
	return rc;
}

/*
 * update the file size (if needed) after a write. Should be called with
 * the inode->i_lock held
 */
void
cifs_update_eof(struct cifsInodeInfo *cifsi, loff_t offset,
		      unsigned int bytes_written)
{
	loff_t end_of_write = offset + bytes_written;

	if (end_of_write > cifsi->server_eof)
		cifsi->server_eof = end_of_write;
}

static ssize_t
cifs_write(struct cifsFileInfo *open_file, __u32 pid, const char *write_data,
	   size_t write_size, loff_t *offset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;
	struct cifsFileInfo *open_file;

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	pTcon = cifs_sb->tcon;

	cFYI(1, ("write %zd bytes to offset %lld of %s", write_size,
	   *poffset, file->f_path.dentry->d_name.name));

	if (file->private_data == NULL)
		return -EBADF;
	open_file = (struct cifsFileInfo *)file->private_data;

	xid = GetXid();

	if (*poffset > file->f_path.dentry->d_inode->i_size)
		long_op = CIFS_VLONG_OP; /* writes past EOF can be slow */
	else
		long_op = CIFS_LONG_OP;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	unsigned int xid;
	struct dentry *dentry = open_file->dentry;
	struct cifsInodeInfo *cifsi = CIFS_I(d_inode(dentry));
	struct cifs_io_parms io_parms;

	cifs_sb = CIFS_SB(dentry->d_sb);

	cifs_dbg(FYI, "write %zd bytes to offset %lld of %pd\n",
		 write_size, *offset, dentry);

	tcon = tlink_tcon(open_file->tlink);
	server = tcon->ses->server;

	if (!server->ops->sync_write)
		return -ENOSYS;

	xid = get_xid();

	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if (file->private_data == NULL) {
				/* file has been closed on us */
				FreeXid(xid);
			/* if we have gotten here we have written some data
			   and blocked, and the file has been freed on us
			   while we blocked so return what we managed to
			   write */
				return total_written;
			}
			if (open_file->closePend) {
				FreeXid(xid);
				if (total_written)
					return total_written;
				else
					return -EBADF;
			}
			struct kvec iov[2];
			unsigned int len;

			if (open_file->invalidHandle) {
				/* we could deadlock if we called
				   filemap_fdatawait from here so tell
				   reopen_file not to flush data to
				   server now */
				rc = cifs_reopen_file(file, false);
				if (rc != 0)
					break;
			}
			if (experimEnabled || (pTcon->ses->server &&
				((pTcon->ses->server->secMode &
				(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
				== 0))) {
				struct kvec iov[2];
				unsigned int len;

				len = min((size_t)cifs_sb->wsize,
					  write_size - total_written);
				/* iov[0] is reserved for smb header */
				iov[1].iov_base = (char *)write_data +
						  total_written;
				iov[1].iov_len = len;
				rc = CIFSSMBWrite2(xid, pTcon,
						open_file->netfid, len,
						*poffset, &bytes_written,
						iov, 1, long_op);
			} else
				rc = CIFSSMBWrite(xid, pTcon,
					 open_file->netfid,
					 min_t(const int, cifs_sb->wsize,
					       write_size - total_written),
					 *poffset, &bytes_written,
					 write_data + total_written,
					 NULL, long_op);
				rc = cifs_reopen_file(open_file, false);
				if (rc != 0)
					break;
			}

			len = min(server->ops->wp_retry_size(d_inode(dentry)),
				  (unsigned int)write_size - total_written);
			/* iov[0] is reserved for smb header */
			iov[1].iov_base = (char *)write_data + total_written;
			iov[1].iov_len = len;
			io_parms.pid = pid;
			io_parms.tcon = tcon;
			io_parms.offset = *offset;
			io_parms.length = len;
			rc = server->ops->sync_write(xid, &open_file->fid,
					&io_parms, &bytes_written, iov, 1);
		}
		if (rc || (bytes_written == 0)) {
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else
			*poffset += bytes_written;
		long_op = CIFS_STD_OP; /* subsequent writes fast -
				    15 seconds is plenty */
	}

	cifs_stats_bytes_written(pTcon, total_written);

	/* since the write may have blocked check these pointers again */
	if ((file->f_path.dentry) && (file->f_path.dentry->d_inode)) {
/*BB We could make this contingent on superblock ATIME flag too */
/*		file->f_path.dentry->d_inode->i_ctime =
		file->f_path.dentry->d_inode->i_mtime = CURRENT_TIME;*/
		if (total_written > 0) {
			spin_lock(&file->f_path.dentry->d_inode->i_lock);
			if (*poffset > file->f_path.dentry->d_inode->i_size)
				i_size_write(file->f_path.dentry->d_inode,
					     *poffset);
			spin_unlock(&file->f_path.dentry->d_inode->i_lock);
		}
		mark_inode_dirty_sync(file->f_path.dentry->d_inode);
	}
	FreeXid(xid);
	return total_written;
}

#ifdef CONFIG_CIFS_EXPERIMENTAL
struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file = NULL;

	read_lock(&GlobalSMBSeslock);
				free_xid(xid);
				return rc;
			}
		} else {
			spin_lock(&d_inode(dentry)->i_lock);
			cifs_update_eof(cifsi, *offset, bytes_written);
			spin_unlock(&d_inode(dentry)->i_lock);
			*offset += bytes_written;
		}
	}

	cifs_stats_bytes_written(tcon, total_written);

	if (total_written > 0) {
		spin_lock(&d_inode(dentry)->i_lock);
		if (*offset > d_inode(dentry)->i_size)
			i_size_write(d_inode(dentry), *offset);
		spin_unlock(&d_inode(dentry)->i_lock);
	}
	mark_inode_dirty_sync(d_inode(dentry));
	free_xid(xid);
	return total_written;
}

struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *cifs_inode,
					bool fsuid_only)
{
	struct cifsFileInfo *open_file = NULL;
	struct cifs_sb_info *cifs_sb = CIFS_SB(cifs_inode->vfs_inode.i_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);

	/* only filter by fsuid on multiuser mounts */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		fsuid_only = false;

	spin_lock(&tcon->open_file_lock);
	/* we could simply get the first_list_entry since write-only entries
	   are always at the end of the list but since the first entry might
	   have a close pending, we go through the whole list */
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (open_file->closePend)
			continue;
		if (open_file->pfile && ((open_file->pfile->f_flags & O_RDWR) ||
		    (open_file->pfile->f_flags & O_RDONLY))) {
			if (!open_file->invalidHandle) {
				/* found a good file */
				/* lock it so it will not be closed on us */
				atomic_inc(&open_file->wrtPending);
				read_unlock(&GlobalSMBSeslock);
		if (fsuid_only && !uid_eq(open_file->uid, current_fsuid()))
			continue;
		if (OPEN_FMODE(open_file->f_flags) & FMODE_READ) {
			if (!open_file->invalidHandle) {
				/* found a good file */
				/* lock it so it will not be closed on us */
				cifsFileInfo_get(open_file);
				spin_unlock(&tcon->open_file_lock);
				return open_file;
			} /* else might as well continue, and look for
			     another, or simply have the caller reopen it
			     again rather than trying to fix this handle */
		} else /* write only file */
			break; /* write only files are last so must be done */
	}
	read_unlock(&GlobalSMBSeslock);
	return NULL;
}
#endif

struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file;
	int rc;
	spin_unlock(&cifs_file_list_lock);
	spin_unlock(&tcon->open_file_lock);
	return NULL;
}

struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *cifs_inode,
					bool fsuid_only)
{
	struct cifsFileInfo *open_file, *inv_file = NULL;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	bool any_available = false;
	int rc;
	unsigned int refind = 0;

	/* Having a null inode here (because mapping->host was set to zero by
	the VFS or MM) should not happen but we had reports of on oops (due to
	it being zero) during stress testcases so we need to check for it */

	if (cifs_inode == NULL) {
		cERROR(1, ("Null inode passed to cifs_writeable_file"));
		cifs_dbg(VFS, "Null inode passed to cifs_writeable_file\n");
		dump_stack();
		return NULL;
	}

	read_lock(&GlobalSMBSeslock);
refind_writable:
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (open_file->closePend)
			continue;
		if (open_file->pfile &&
		    ((open_file->pfile->f_flags & O_RDWR) ||
		     (open_file->pfile->f_flags & O_WRONLY))) {
			atomic_inc(&open_file->wrtPending);

			if (!open_file->invalidHandle) {
				/* found a good writable file */
				read_unlock(&GlobalSMBSeslock);
				return open_file;
			}

			read_unlock(&GlobalSMBSeslock);
			/* Had to unlock since following call can block */
			rc = cifs_reopen_file(open_file->pfile, false);
			if (!rc) {
				if (!open_file->closePend)
					return open_file;
				else { /* start over in case this was deleted */
				       /* since the list could be modified */
					read_lock(&GlobalSMBSeslock);
					atomic_dec(&open_file->wrtPending);
					goto refind_writable;
				}
			}

			/* if it fails, try another handle if possible -
			(we can not do this if closePending since
			loop could be modified - in which case we
			have to start at the beginning of the list
			again. Note that it would be bad
			to hold up writepages here (rather than
			in caller) with continuous retries */
			cFYI(1, ("wp failed on reopen file"));
			read_lock(&GlobalSMBSeslock);
			/* can not use this handle, no write
			   pending on this one after all */
			atomic_dec(&open_file->wrtPending);

			if (open_file->closePend) /* list could have changed */
				goto refind_writable;
			/* else we simply continue to the next entry. Thus
			   we do not loop on reopen errors.  If we
			   can not reopen the file, for example if we
			   reconnected to a server with another client
			   racing to delete or lock the file we would not
			   make progress if we restarted before the beginning
			   of the loop here. */
		}
	}
	read_unlock(&GlobalSMBSeslock);
	cifs_sb = CIFS_SB(cifs_inode->vfs_inode.i_sb);
	tcon = cifs_sb_master_tcon(cifs_sb);

	/* only filter by fsuid on multiuser mounts */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		fsuid_only = false;

	spin_lock(&tcon->open_file_lock);
refind_writable:
	if (refind > MAX_REOPEN_ATT) {
		spin_unlock(&tcon->open_file_lock);
		return NULL;
	}
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (!any_available && open_file->pid != current->tgid)
			continue;
		if (fsuid_only && !uid_eq(open_file->uid, current_fsuid()))
			continue;
		if (OPEN_FMODE(open_file->f_flags) & FMODE_WRITE) {
			if (!open_file->invalidHandle) {
				/* found a good writable file */
				cifsFileInfo_get(open_file);
				spin_unlock(&tcon->open_file_lock);
				return open_file;
			} else {
				if (!inv_file)
					inv_file = open_file;
			}
		}
	}
	/* couldn't find useable FH with same pid, try any available */
	if (!any_available) {
		any_available = true;
		goto refind_writable;
	}

	if (inv_file) {
		any_available = false;
		cifsFileInfo_get(inv_file);
	}

	spin_unlock(&tcon->open_file_lock);

	if (inv_file) {
		rc = cifs_reopen_file(inv_file, false);
		if (!rc)
			return inv_file;
		else {
			spin_lock(&tcon->open_file_lock);
			list_move_tail(&inv_file->flist,
					&cifs_inode->openFileList);
			spin_unlock(&tcon->open_file_lock);
			cifsFileInfo_put(inv_file);
			++refind;
			inv_file = NULL;
			spin_lock(&tcon->open_file_lock);
			goto refind_writable;
		}
	}

	return NULL;
}

static int cifs_partialpagewrite(struct page *page, unsigned from, unsigned to)
{
	struct address_space *mapping = page->mapping;
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char *write_data;
	int rc = -EFAULT;
	int bytes_written = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifsFileInfo *open_file;

	if (!mapping || !mapping->host)
		return -EFAULT;

	inode = page->mapping->host;
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	offset += (loff_t)from;
	write_data = kmap(page);
	write_data += from;

	if ((to > PAGE_CACHE_SIZE) || (from > to)) {
		kunmap(page);
		return -EIO;
	}

	/* racing with truncate? */
	if (offset > mapping->host->i_size) {
		kunmap(page);
		return 0; /* don't care */
	}

	/* check to make sure that we are not extending the file */
	if (mapping->host->i_size - offset < (loff_t)to)
		to = (unsigned)(mapping->host->i_size - offset);

	open_file = find_writable_file(CIFS_I(mapping->host));
	if (open_file) {
		bytes_written = cifs_write(open_file->pfile, write_data,
					   to-from, &offset);
		atomic_dec(&open_file->wrtPending);
	open_file = find_writable_file(CIFS_I(mapping->host), false);
	if (open_file) {
		bytes_written = cifs_write(open_file, open_file->pid,
					   write_data, to - from, &offset);
		cifsFileInfo_put(open_file);
		/* Does mm or vfs already set times? */
		inode->i_atime = inode->i_mtime = current_fs_time(inode->i_sb);
		if ((bytes_written > 0) && (offset))
			rc = 0;
		else if (bytes_written < 0)
			rc = bytes_written;
	} else {
		cFYI(1, ("No writeable filehandles for inode"));
		cifs_dbg(FYI, "No writeable filehandles for inode\n");
		rc = -EIO;
	}

	kunmap(page);
	return rc;
}

static int cifs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	unsigned int bytes_to_write;
	unsigned int bytes_written;
	struct cifs_sb_info *cifs_sb;
	int done = 0;
	pgoff_t end;
	pgoff_t index;
	int range_whole = 0;
	struct kvec *iov;
	int len;
	int n_iov = 0;
	pgoff_t next;
	int nr_pages;
	__u64 offset = 0;
	struct cifsFileInfo *open_file;
	struct page *page;
	struct pagevec pvec;
	int rc = 0;
	int scanned = 0;
	int xid;

	cifs_sb = CIFS_SB(mapping->host->i_sb);

	/*
	 * If wsize is smaller that the page cache size, default to writing
static struct cifs_writedata *
wdata_alloc_and_fillpages(pgoff_t tofind, struct address_space *mapping,
			  pgoff_t end, pgoff_t *index,
			  unsigned int *found_pages)
{
	unsigned int nr_pages;
	struct page **pages;
	struct cifs_writedata *wdata;

	wdata = cifs_writedata_alloc((unsigned int)tofind,
				     cifs_writev_complete);
	if (!wdata)
		return NULL;

	/*
	 * find_get_pages_tag seems to return a max of 256 on each
	 * iteration, so we must call it several times in order to
	 * fill the array or the wsize is effectively limited to
	 * 256 * PAGE_CACHE_SIZE.
	 */
	*found_pages = 0;
	pages = wdata->pages;
	do {
		nr_pages = find_get_pages_tag(mapping, index,
					      PAGECACHE_TAG_DIRTY, tofind,
					      pages);
		*found_pages += nr_pages;
		tofind -= nr_pages;
		pages += nr_pages;
	} while (nr_pages && tofind && *index <= end);

	return wdata;
}

static unsigned int
wdata_prepare_pages(struct cifs_writedata *wdata, unsigned int found_pages,
		    struct address_space *mapping,
		    struct writeback_control *wbc,
		    pgoff_t end, pgoff_t *index, pgoff_t *next, bool *done)
{
	unsigned int nr_pages = 0, i;
	struct page *page;

	for (i = 0; i < found_pages; i++) {
		page = wdata->pages[i];
		/*
		 * At this point we hold neither mapping->tree_lock nor
		 * lock on the page itself: the page may be truncated or
		 * invalidated (changing page->mapping to NULL), or even
		 * swizzled back from swapper_space to tmpfs file
		 * mapping
		 */

		if (nr_pages == 0)
			lock_page(page);
		else if (!trylock_page(page))
			break;

		if (unlikely(page->mapping != mapping)) {
			unlock_page(page);
			break;
		}

		if (!wbc->range_cyclic && page->index > end) {
			*done = true;
			unlock_page(page);
			break;
		}

		if (*next && (page->index != *next)) {
			/* Not next consecutive page */
			unlock_page(page);
			break;
		}

		if (wbc->sync_mode != WB_SYNC_NONE)
			wait_on_page_writeback(page);

		if (PageWriteback(page) ||
				!clear_page_dirty_for_io(page)) {
			unlock_page(page);
			break;
		}

		/*
		 * This actually clears the dirty bit in the radix tree.
		 * See cifs_writepage() for more commentary.
		 */
		set_page_writeback(page);
		if (page_offset(page) >= i_size_read(mapping->host)) {
			*done = true;
			unlock_page(page);
			end_page_writeback(page);
			break;
		}

		wdata->pages[i] = page;
		*next = page->index + 1;
		++nr_pages;
	}

	/* reset index to refind any pages skipped */
	if (nr_pages == 0)
		*index = wdata->pages[0]->index + 1;

	/* put any pages we aren't going to use */
	for (i = nr_pages; i < found_pages; i++) {
		page_cache_release(wdata->pages[i]);
		wdata->pages[i] = NULL;
	}

	return nr_pages;
}

static int
wdata_send_pages(struct cifs_writedata *wdata, unsigned int nr_pages,
		 struct address_space *mapping, struct writeback_control *wbc)
{
	int rc = 0;
	struct TCP_Server_Info *server;
	unsigned int i;

	wdata->sync_mode = wbc->sync_mode;
	wdata->nr_pages = nr_pages;
	wdata->offset = page_offset(wdata->pages[0]);
	wdata->pagesz = PAGE_CACHE_SIZE;
	wdata->tailsz = min(i_size_read(mapping->host) -
			page_offset(wdata->pages[nr_pages - 1]),
			(loff_t)PAGE_CACHE_SIZE);
	wdata->bytes = ((nr_pages - 1) * PAGE_CACHE_SIZE) + wdata->tailsz;

	if (wdata->cfile != NULL)
		cifsFileInfo_put(wdata->cfile);
	wdata->cfile = find_writable_file(CIFS_I(mapping->host), false);
	if (!wdata->cfile) {
		cifs_dbg(VFS, "No writable handles for inode\n");
		rc = -EBADF;
	} else {
		wdata->pid = wdata->cfile->pid;
		server = tlink_tcon(wdata->cfile->tlink)->ses->server;
		rc = server->ops->async_writev(wdata, cifs_writedata_release);
	}

	for (i = 0; i < nr_pages; ++i)
		unlock_page(wdata->pages[i]);

	return rc;
}

static int cifs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(mapping->host->i_sb);
	struct TCP_Server_Info *server;
	bool done = false, scanned = false, range_whole = false;
	pgoff_t end, index;
	struct cifs_writedata *wdata;
	int rc = 0;

	/*
	 * If wsize is smaller than the page cache size, default to writing
	 * one page at a time via cifs_writepage
	 */
	if (cifs_sb->wsize < PAGE_CACHE_SIZE)
		return generic_writepages(mapping, wbc);

	if ((cifs_sb->tcon->ses) && (cifs_sb->tcon->ses->server))
		if (cifs_sb->tcon->ses->server->secMode &
				(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
			if (!experimEnabled)
				return generic_writepages(mapping, wbc);

	iov = kmalloc(32 * sizeof(struct kvec), GFP_KERNEL);
	if (iov == NULL)
		return generic_writepages(mapping, wbc);


	/*
	 * BB: Is this meaningful for a non-block-device file system?
	 * If it is, we should test it again after we do I/O
	 */
	if (wbc->nonblocking && bdi_write_congested(bdi)) {
		wbc->encountered_congestion = 1;
		kfree(iov);
		return 0;
	}

	xid = GetXid();

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		scanned = 1;
	}
retry:
	while (!done && (index <= end) &&
	       (nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
			PAGECACHE_TAG_DIRTY,
			min(end - index, (pgoff_t)PAGEVEC_SIZE - 1) + 1))) {
		int first;
		unsigned int i;

		first = -1;
		next = 0;
		n_iov = 0;
		bytes_to_write = 0;

		for (i = 0; i < nr_pages; i++) {
			page = pvec.pages[i];
			/*
			 * At this point we hold neither mapping->tree_lock nor
			 * lock on the page itself: the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or even
			 * swizzled back from swapper_space to tmpfs file
			 * mapping
			 */

			if (first < 0)
				lock_page(page);
			else if (!trylock_page(page))
				break;

			if (unlikely(page->mapping != mapping)) {
				unlock_page(page);
				break;
			}

			if (!wbc->range_cyclic && page->index > end) {
				done = 1;
				unlock_page(page);
				break;
			}

			if (next && (page->index != next)) {
				/* Not next consecutive page */
				unlock_page(page);
				break;
			}

			if (wbc->sync_mode != WB_SYNC_NONE)
				wait_on_page_writeback(page);

			if (PageWriteback(page) ||
					!clear_page_dirty_for_io(page)) {
				unlock_page(page);
				break;
			}

			/*
			 * This actually clears the dirty bit in the radix tree.
			 * See cifs_writepage() for more commentary.
			 */
			set_page_writeback(page);

			if (page_offset(page) >= mapping->host->i_size) {
				done = 1;
				unlock_page(page);
				end_page_writeback(page);
				break;
			}

			/*
			 * BB can we get rid of this?  pages are held by pvec
			 */
			page_cache_get(page);

			len = min(mapping->host->i_size - page_offset(page),
				  (loff_t)PAGE_CACHE_SIZE);

			/* reserve iov[0] for the smb header */
			n_iov++;
			iov[n_iov].iov_base = kmap(page);
			iov[n_iov].iov_len = len;
			bytes_to_write += len;

			if (first < 0) {
				first = i;
				offset = page_offset(page);
			}
			next = page->index + 1;
			if (bytes_to_write + PAGE_CACHE_SIZE > cifs_sb->wsize)
				break;
		}
		if (n_iov) {
			/* Search for a writable handle every time we call
			 * CIFSSMBWrite2.  We can't rely on the last handle
			 * we used to still be valid
			 */
			open_file = find_writable_file(CIFS_I(mapping->host));
			if (!open_file) {
				cERROR(1, ("No writable handles for inode"));
				rc = -EBADF;
			} else {
				rc = CIFSSMBWrite2(xid, cifs_sb->tcon,
						   open_file->netfid,
						   bytes_to_write, offset,
						   &bytes_written, iov, n_iov,
						   CIFS_LONG_OP);
				atomic_dec(&open_file->wrtPending);
				if (rc || bytes_written < bytes_to_write) {
					cERROR(1, ("Write2 ret %d, wrote %d",
						  rc, bytes_written));
					/* BB what if continued retry is
					   requested via mount flags? */
					if (rc == -ENOSPC)
						set_bit(AS_ENOSPC, &mapping->flags);
					else
						set_bit(AS_EIO, &mapping->flags);
				} else {
					cifs_stats_bytes_written(cifs_sb->tcon,
								 bytes_written);
				}
			}
			for (i = 0; i < n_iov; i++) {
				page = pvec.pages[first + i];
				/* Should we also set page error on
				success rc but too little data written? */
				/* BB investigate retry logic on temporary
				server crash cases and how recovery works
				when page marked as error */
				if (rc)
					SetPageError(page);
				kunmap(page);
				unlock_page(page);
				end_page_writeback(page);
				page_cache_release(page);
			}
			if ((wbc->nr_to_write -= n_iov) <= 0)
				done = 1;
			index = next;
		}
		pagevec_release(&pvec);
	}
			range_whole = true;
		scanned = true;
	}
	server = cifs_sb_master_tcon(cifs_sb)->ses->server;
retry:
	while (!done && index <= end) {
		unsigned int i, nr_pages, found_pages, wsize, credits;
		pgoff_t next = 0, tofind, saved_index = index;

		rc = server->ops->wait_mtu_credits(server, cifs_sb->wsize,
						   &wsize, &credits);
		if (rc)
			break;

		tofind = min((wsize / PAGE_CACHE_SIZE) - 1, end - index) + 1;

		wdata = wdata_alloc_and_fillpages(tofind, mapping, end, &index,
						  &found_pages);
		if (!wdata) {
			rc = -ENOMEM;
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		if (found_pages == 0) {
			kref_put(&wdata->refcount, cifs_writedata_release);
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		nr_pages = wdata_prepare_pages(wdata, found_pages, mapping, wbc,
					       end, &index, &next, &done);

		/* nothing to write? */
		if (nr_pages == 0) {
			kref_put(&wdata->refcount, cifs_writedata_release);
			add_credits_and_wake_if(server, credits, 0);
			continue;
		}

		wdata->credits = credits;

		rc = wdata_send_pages(wdata, nr_pages, mapping, wbc);

		/* send failure -- clean up the mess */
		if (rc != 0) {
			add_credits_and_wake_if(server, wdata->credits, 0);
			for (i = 0; i < nr_pages; ++i) {
				if (rc == -EAGAIN)
					redirty_page_for_writepage(wbc,
							   wdata->pages[i]);
				else
					SetPageError(wdata->pages[i]);
				end_page_writeback(wdata->pages[i]);
				page_cache_release(wdata->pages[i]);
			}
			if (rc != -EAGAIN)
				mapping_set_error(mapping, rc);
		}
		kref_put(&wdata->refcount, cifs_writedata_release);

		if (wbc->sync_mode == WB_SYNC_ALL && rc == -EAGAIN) {
			index = saved_index;
			continue;
		}

		wbc->nr_to_write -= nr_pages;
		if (wbc->nr_to_write <= 0)
			done = true;

		index = next;
	}

	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = index;

	FreeXid(xid);
	kfree(iov);
	return rc;
}

static int cifs_writepage(struct page *page, struct writeback_control *wbc)
{
	int rc = -EFAULT;
	int xid;

	xid = GetXid();
/* BB add check for wbc flags */
	page_cache_get(page);
	if (!PageUptodate(page))
		cFYI(1, ("ppw - page not up to date"));
		scanned = true;
		index = 0;
		goto retry;
	}

	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = index;

	return rc;
}

static int
cifs_writepage_locked(struct page *page, struct writeback_control *wbc)
{
	int rc;
	unsigned int xid;

	xid = get_xid();
/* BB add check for wbc flags */
	page_cache_get(page);
	if (!PageUptodate(page))
		cifs_dbg(FYI, "ppw - page not up to date\n");

	/*
	 * Set the "writeback" flag, and clear "dirty" in the radix tree.
	 *
	 * A writepage() implementation always needs to do either this,
	 * or re-dirty the page with "redirty_page_for_writepage()" in
	 * the case of a failure.
	 *
	 * Just unlocking the page will cause the radix tree tag-bits
	 * to fail to update with the state of the page correctly.
	 */
	set_page_writeback(page);
	rc = cifs_partialpagewrite(page, 0, PAGE_CACHE_SIZE);
	SetPageUptodate(page); /* BB add check for error and Clearuptodate? */
	unlock_page(page);
	end_page_writeback(page);
	page_cache_release(page);
	FreeXid(xid);
	return rc;
}

static int cifs_commit_write(struct file *file, struct page *page,
	unsigned offset, unsigned to)
{
	int xid;
	int rc = 0;
	struct inode *inode = page->mapping->host;
	loff_t position = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;
	char *page_data;

	xid = GetXid();
	cFYI(1, ("commit write for page %p up to position %lld for %d",
		 page, position, to));
	spin_lock(&inode->i_lock);
	if (position > inode->i_size)
		i_size_write(inode, position);

	spin_unlock(&inode->i_lock);
	if (!PageUptodate(page)) {
		position =  ((loff_t)page->index << PAGE_CACHE_SHIFT) + offset;
		/* can not rely on (or let) writepage write this data */
		if (to < offset) {
			cFYI(1, ("Illegal offsets, can not copy from %d to %d",
				offset, to));
			FreeXid(xid);
			return rc;
		}
retry_write:
	rc = cifs_partialpagewrite(page, 0, PAGE_CACHE_SIZE);
	if (rc == -EAGAIN && wbc->sync_mode == WB_SYNC_ALL)
		goto retry_write;
	else if (rc == -EAGAIN)
		redirty_page_for_writepage(wbc, page);
	else if (rc != 0)
		SetPageError(page);
	else
		SetPageUptodate(page);
	end_page_writeback(page);
	page_cache_release(page);
	free_xid(xid);
	return rc;
}

static int cifs_writepage(struct page *page, struct writeback_control *wbc)
{
	int rc = cifs_writepage_locked(page, wbc);
	unlock_page(page);
	return rc;
}

static int cifs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int rc;
	struct inode *inode = mapping->host;
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
	__u32 pid;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = cfile->pid;
	else
		pid = current->tgid;

	cifs_dbg(FYI, "write_end for page %p from pos %lld with %d bytes\n",
		 page, pos, copied);

	if (PageChecked(page)) {
		if (copied == len)
			SetPageUptodate(page);
		ClearPageChecked(page);
	} else if (!PageUptodate(page) && copied == PAGE_CACHE_SIZE)
		SetPageUptodate(page);

	if (!PageUptodate(page)) {
		char *page_data;
		unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
		unsigned int xid;

		xid = get_xid();
		/* this is probably better than directly calling
		   partialpage_write since in this function the file handle is
		   known which we might as well	leverage */
		/* BB check if anything else missing out of ppw
		   such as updating last write time */
		page_data = kmap(page);
		rc = cifs_write(file, page_data + offset, to-offset,
				&position);
		if (rc > 0)
			rc = 0;
		/* else if (rc < 0) should we set writebehind rc? */
		kunmap(page);
	} else {
		set_page_dirty(page);
	}

	FreeXid(xid);
	return rc;
}

int cifs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int xid;
	int rc = 0;
	struct inode *inode = file->f_path.dentry->d_inode;

	xid = GetXid();

	cFYI(1, ("Sync file - name: %s datasync: 0x%x",
		dentry->d_name.name, datasync));

	rc = filemap_write_and_wait(inode->i_mapping);
	if (rc == 0) {
		rc = CIFS_I(inode)->write_behind_rc;
		CIFS_I(inode)->write_behind_rc = 0;
	}
	FreeXid(xid);
	return rc;
}

/* static void cifs_sync_page(struct page *page)
{
	struct address_space *mapping;
	struct inode *inode;
	unsigned long index = page->index;
	unsigned int rpages = 0;
	int rc = 0;

	cFYI(1, ("sync page %p",page));
	mapping = page->mapping;
	if (!mapping)
		return 0;
	inode = mapping->host;
	if (!inode)
		return; */

/*	fill in rpages then
	result = cifs_pagein_inode(inode, index, rpages); */ /* BB finish */

/*	cFYI(1, ("rpages is %d for sync page of Index %ld", rpages, index));

#if 0
	if (rc < 0)
		return rc;
	return 0;
#endif
} */
		rc = cifs_write(cfile, pid, page_data + offset, copied, &pos);
		/* if (rc < 0) should we set writebehind rc? */
		kunmap(page);

		free_xid(xid);
	} else {
		rc = copied;
		pos += copied;
		set_page_dirty(page);
	}

	if (rc > 0) {
		spin_lock(&inode->i_lock);
		if (pos > inode->i_size)
			i_size_write(inode, pos);
		spin_unlock(&inode->i_lock);
	}

	unlock_page(page);
	page_cache_release(page);

	return rc;
}

int cifs_strict_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync)
{
	unsigned int xid;
	int rc = 0;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsFileInfo *smbfile = file->private_data;
	struct inode *inode = file_inode(file);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	rc = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (rc)
		return rc;
	mutex_lock(&inode->i_mutex);

	xid = get_xid();

	cifs_dbg(FYI, "Sync file - name: %pD datasync: 0x%x\n",
		 file, datasync);

	if (!CIFS_CACHE_READ(CIFS_I(inode))) {
		rc = cifs_zap_mapping(inode);
		if (rc) {
			cifs_dbg(FYI, "rc: %d during invalidate phase\n", rc);
			rc = 0; /* don't care about it in fsync */
		}
	}

	tcon = tlink_tcon(smbfile->tlink);
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC)) {
		server = tcon->ses->server;
		if (server->ops->flush)
			rc = server->ops->flush(xid, tcon, &smbfile->fid);
		else
			rc = -ENOSYS;
	}

	free_xid(xid);
	mutex_unlock(&inode->i_mutex);
	return rc;
}

int cifs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	unsigned int xid;
	int rc = 0;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	struct cifsFileInfo *smbfile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct inode *inode = file->f_mapping->host;

	rc = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (rc)
		return rc;
	mutex_lock(&inode->i_mutex);

	xid = get_xid();

	cifs_dbg(FYI, "Sync file - name: %pD datasync: 0x%x\n",
		 file, datasync);

	tcon = tlink_tcon(smbfile->tlink);
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC)) {
		server = tcon->ses->server;
		if (server->ops->flush)
			rc = server->ops->flush(xid, tcon, &smbfile->fid);
		else
			rc = -ENOSYS;
	}

	free_xid(xid);
	mutex_unlock(&inode->i_mutex);
	return rc;
}

/*
 * As file closes, flush all cached write data for this inode checking
 * for write behind errors.
 */
int cifs_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int rc = 0;

	/* Rather than do the steps manually:
	   lock the inode for writing
	   loop through pages looking for write behind data (dirty pages)
	   coalesce into contiguous 16K (or smaller) chunks to write to server
	   send to server (prefer in parallel)
	   deal with writebehind errors
	   unlock inode for writing
	   filemapfdatawrite appears easier for the time being */

	rc = filemap_fdatawrite(inode->i_mapping);
	/* reset wb rc if we were able to write out dirty pages */
	if (!rc) {
		rc = CIFS_I(inode)->write_behind_rc;
		CIFS_I(inode)->write_behind_rc = 0;
	}

	cFYI(1, ("Flush inode %p file %p rc %d", inode, file, rc));
	struct inode *inode = file_inode(file);
	int rc = 0;

	if (file->f_mode & FMODE_WRITE)
		rc = filemap_write_and_wait(inode->i_mapping);

	cifs_dbg(FYI, "Flush inode %p file %p rc %d\n", inode, file, rc);

	return rc;
}

ssize_t cifs_user_read(struct file *file, char __user *read_data,
	size_t read_size, loff_t *poffset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read = 0;
	unsigned int current_read_size;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	struct cifsFileInfo *open_file;
	char *smb_read_data;
	char __user *current_offset;
	struct smb_com_read_rsp *pSMBr;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cFYI(1, ("attempting read on write only file instance"));

	for (total_read = 0, current_offset = read_data;
	     read_size > total_read;
	     total_read += bytes_read, current_offset += bytes_read) {
		current_read_size = min_t(const int, read_size - total_read,
					  cifs_sb->rsize);
		rc = -EAGAIN;
		smb_read_data = NULL;
		while (rc == -EAGAIN) {
			int buf_type = CIFS_NO_BUFFER;
			if ((open_file->invalidHandle) &&
			    (!open_file->closePend)) {
				rc = cifs_reopen_file(file, true);
				if (rc != 0)
					break;
			}
			rc = CIFSSMBRead(xid, pTcon,
					 open_file->netfid,
					 current_read_size, *poffset,
					 &bytes_read, &smb_read_data,
					 &buf_type);
			pSMBr = (struct smb_com_read_rsp *)smb_read_data;
			if (smb_read_data) {
				if (copy_to_user(current_offset,
						smb_read_data +
						4 /* RFC1001 length field */ +
						le16_to_cpu(pSMBr->DataOffset),
						bytes_read))
					rc = -EFAULT;

				if (buf_type == CIFS_SMALL_BUFFER)
					cifs_small_buf_release(smb_read_data);
				else if (buf_type == CIFS_LARGE_BUFFER)
					cifs_buf_release(smb_read_data);
				smb_read_data = NULL;
			}
		}
		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else {
			cifs_stats_bytes_read(pTcon, bytes_read);
			*poffset += bytes_read;
		}
	}
	FreeXid(xid);
	return total_read;
}


static ssize_t cifs_read(struct file *file, char *read_data, size_t read_size,
	loff_t *poffset)
static int
cifs_write_allocate_pages(struct page **pages, unsigned long num_pages)
{
	int rc = 0;
	unsigned long i;

	for (i = 0; i < num_pages; i++) {
		pages[i] = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
		if (!pages[i]) {
			/*
			 * save number of pages we have already allocated and
			 * return with ENOMEM error
			 */
			num_pages = i;
			rc = -ENOMEM;
			break;
		}
	}

	if (rc) {
		for (i = 0; i < num_pages; i++)
			put_page(pages[i]);
	}
	return rc;
}

static inline
size_t get_numpages(const size_t wsize, const size_t len, size_t *cur_len)
{
	size_t num_pages;
	size_t clen;

	clen = min_t(const size_t, len, wsize);
	num_pages = DIV_ROUND_UP(clen, PAGE_SIZE);

	if (cur_len)
		*cur_len = clen;

	return num_pages;
}

static void
cifs_uncached_writedata_release(struct kref *refcount)
{
	int i;
	struct cifs_writedata *wdata = container_of(refcount,
					struct cifs_writedata, refcount);

	for (i = 0; i < wdata->nr_pages; i++)
		put_page(wdata->pages[i]);
	cifs_writedata_release(refcount);
}

static void
cifs_uncached_writev_complete(struct work_struct *work)
{
	struct cifs_writedata *wdata = container_of(work,
					struct cifs_writedata, work);
	struct inode *inode = d_inode(wdata->cfile->dentry);
	struct cifsInodeInfo *cifsi = CIFS_I(inode);

	spin_lock(&inode->i_lock);
	cifs_update_eof(cifsi, wdata->offset, wdata->bytes);
	if (cifsi->server_eof > inode->i_size)
		i_size_write(inode, cifsi->server_eof);
	spin_unlock(&inode->i_lock);

	complete(&wdata->done);

	kref_put(&wdata->refcount, cifs_uncached_writedata_release);
}

static int
wdata_fill_from_iovec(struct cifs_writedata *wdata, struct iov_iter *from,
		      size_t *len, unsigned long *num_pages)
{
	size_t save_len, copied, bytes, cur_len = *len;
	unsigned long i, nr_pages = *num_pages;

	save_len = cur_len;
	for (i = 0; i < nr_pages; i++) {
		bytes = min_t(const size_t, cur_len, PAGE_SIZE);
		copied = copy_page_from_iter(wdata->pages[i], 0, bytes, from);
		cur_len -= copied;
		/*
		 * If we didn't copy as much as we expected, then that
		 * may mean we trod into an unmapped area. Stop copying
		 * at that point. On the next pass through the big
		 * loop, we'll likely end up getting a zero-length
		 * write and bailing out of it.
		 */
		if (copied < bytes)
			break;
	}
	cur_len = save_len - cur_len;
	*len = cur_len;

	/*
	 * If we have no data to send, then that probably means that
	 * the copy above failed altogether. That's most likely because
	 * the address in the iovec was bogus. Return -EFAULT and let
	 * the caller free anything we allocated and bail out.
	 */
	if (!cur_len)
		return -EFAULT;

	/*
	 * i + 1 now represents the number of pages we actually used in
	 * the copy phase above.
	 */
	*num_pages = i + 1;
	return 0;
}

static int
cifs_write_from_iter(loff_t offset, size_t len, struct iov_iter *from,
		     struct cifsFileInfo *open_file,
		     struct cifs_sb_info *cifs_sb, struct list_head *wdata_list)
{
	int rc = 0;
	size_t cur_len;
	unsigned long nr_pages, num_pages, i;
	struct cifs_writedata *wdata;
	struct iov_iter saved_from;
	loff_t saved_offset = offset;
	pid_t pid;
	struct TCP_Server_Info *server;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	server = tlink_tcon(open_file->tlink)->ses->server;
	memcpy(&saved_from, from, sizeof(struct iov_iter));

	do {
		unsigned int wsize, credits;

		rc = server->ops->wait_mtu_credits(server, cifs_sb->wsize,
						   &wsize, &credits);
		if (rc)
			break;

		nr_pages = get_numpages(wsize, len, &cur_len);
		wdata = cifs_writedata_alloc(nr_pages,
					     cifs_uncached_writev_complete);
		if (!wdata) {
			rc = -ENOMEM;
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		rc = cifs_write_allocate_pages(wdata->pages, nr_pages);
		if (rc) {
			kfree(wdata);
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		num_pages = nr_pages;
		rc = wdata_fill_from_iovec(wdata, from, &cur_len, &num_pages);
		if (rc) {
			for (i = 0; i < nr_pages; i++)
				put_page(wdata->pages[i]);
			kfree(wdata);
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		/*
		 * Bring nr_pages down to the number of pages we actually used,
		 * and free any pages that we didn't use.
		 */
		for ( ; nr_pages > num_pages; nr_pages--)
			put_page(wdata->pages[nr_pages - 1]);

		wdata->sync_mode = WB_SYNC_ALL;
		wdata->nr_pages = nr_pages;
		wdata->offset = (__u64)offset;
		wdata->cfile = cifsFileInfo_get(open_file);
		wdata->pid = pid;
		wdata->bytes = cur_len;
		wdata->pagesz = PAGE_SIZE;
		wdata->tailsz = cur_len - ((nr_pages - 1) * PAGE_SIZE);
		wdata->credits = credits;

		if (!wdata->cfile->invalidHandle ||
		    !(rc = cifs_reopen_file(wdata->cfile, false)))
			rc = server->ops->async_writev(wdata,
					cifs_uncached_writedata_release);
		if (rc) {
			add_credits_and_wake_if(server, wdata->credits, 0);
			kref_put(&wdata->refcount,
				 cifs_uncached_writedata_release);
			if (rc == -EAGAIN) {
				memcpy(from, &saved_from,
				       sizeof(struct iov_iter));
				iov_iter_advance(from, offset - saved_offset);
				continue;
			}
			break;
		}

		list_add_tail(&wdata->list, wdata_list);
		offset += cur_len;
		len -= cur_len;
	} while (len > 0);

	return rc;
}

ssize_t cifs_user_writev(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	ssize_t total_written = 0;
	struct cifsFileInfo *open_file;
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb;
	struct cifs_writedata *wdata, *tmp;
	struct list_head wdata_list;
	struct iov_iter saved_from;
	int rc;

	/*
	 * BB - optimize the way when signing is disabled. We can drop this
	 * extra memory-to-memory copying and use iovec buffers for constructing
	 * write request.
	 */

	rc = generic_write_checks(iocb, from);
	if (rc <= 0)
		return rc;

	INIT_LIST_HEAD(&wdata_list);
	cifs_sb = CIFS_FILE_SB(file);
	open_file = file->private_data;
	tcon = tlink_tcon(open_file->tlink);

	if (!tcon->ses->server->ops->async_writev)
		return -ENOSYS;

	memcpy(&saved_from, from, sizeof(struct iov_iter));

	rc = cifs_write_from_iter(iocb->ki_pos, iov_iter_count(from), from,
				  open_file, cifs_sb, &wdata_list);

	/*
	 * If at least one write was successfully sent, then discard any rc
	 * value from the later writes. If the other write succeeds, then
	 * we'll end up returning whatever was written. If it fails, then
	 * we'll get a new rc value from that.
	 */
	if (!list_empty(&wdata_list))
		rc = 0;

	/*
	 * Wait for and collect replies for any successful sends in order of
	 * increasing offset. Once an error is hit or we get a fatal signal
	 * while waiting, then return without waiting for any more replies.
	 */
restart_loop:
	list_for_each_entry_safe(wdata, tmp, &wdata_list, list) {
		if (!rc) {
			/* FIXME: freezable too? */
			rc = wait_for_completion_killable(&wdata->done);
			if (rc)
				rc = -EINTR;
			else if (wdata->result)
				rc = wdata->result;
			else
				total_written += wdata->bytes;

			/* resend call if it's a retryable error */
			if (rc == -EAGAIN) {
				struct list_head tmp_list;
				struct iov_iter tmp_from;

				INIT_LIST_HEAD(&tmp_list);
				list_del_init(&wdata->list);

				memcpy(&tmp_from, &saved_from,
				       sizeof(struct iov_iter));
				iov_iter_advance(&tmp_from,
						 wdata->offset - iocb->ki_pos);

				rc = cifs_write_from_iter(wdata->offset,
						wdata->bytes, &tmp_from,
						open_file, cifs_sb, &tmp_list);

				list_splice(&tmp_list, &wdata_list);

				kref_put(&wdata->refcount,
					 cifs_uncached_writedata_release);
				goto restart_loop;
			}
		}
		list_del_init(&wdata->list);
		kref_put(&wdata->refcount, cifs_uncached_writedata_release);
	}

	if (unlikely(!total_written))
		return rc;

	iocb->ki_pos += total_written;
	set_bit(CIFS_INO_INVALID_MAPPING, &CIFS_I(file_inode(file))->flags);
	cifs_stats_bytes_written(tcon, total_written);
	return total_written;
}

static ssize_t
cifs_writev(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct inode *inode = file->f_mapping->host;
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct TCP_Server_Info *server = tlink_tcon(cfile->tlink)->ses->server;
	ssize_t rc;

	/*
	 * We need to hold the sem to be sure nobody modifies lock list
	 * with a brlock that prevents writing.
	 */
	down_read(&cinode->lock_sem);
	mutex_lock(&inode->i_mutex);

	rc = generic_write_checks(iocb, from);
	if (rc <= 0)
		goto out;

	if (!cifs_find_lock_conflict(cfile, iocb->ki_pos, iov_iter_count(from),
				     server->vals->exclusive_lock_type, NULL,
				     CIFS_WRITE_OP))
		rc = __generic_file_write_iter(iocb, from);
	else
		rc = -EACCES;
out:
	mutex_unlock(&inode->i_mutex);

	if (rc > 0) {
		ssize_t err = generic_write_sync(file, iocb->ki_pos - rc, rc);
		if (err < 0)
			rc = err;
	}
	up_read(&cinode->lock_sem);
	return rc;
}

ssize_t
cifs_strict_writev(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)
						iocb->ki_filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	ssize_t written;

	written = cifs_get_writer(cinode);
	if (written)
		return written;

	if (CIFS_CACHE_WRITE(cinode)) {
		if (cap_unix(tcon->ses) &&
		(CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability))
		  && ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0)) {
			written = generic_file_write_iter(iocb, from);
			goto out;
		}
		written = cifs_writev(iocb, from);
		goto out;
	}
	/*
	 * For non-oplocked files in strict cache mode we need to write the data
	 * to the server exactly from the pos to pos+len-1 rather than flush all
	 * affected pages because it may cause a error with mandatory locks on
	 * these pages but not on the region from pos to ppos+len-1.
	 */
	written = cifs_user_writev(iocb, from);
	if (CIFS_CACHE_READ(cinode)) {
		/*
		 * We have read level caching and we have just sent a write
		 * request to the server thus making data in the cache stale.
		 * Zap the cache and set oplock/lease level to NONE to avoid
		 * reading stale data from the cache. All subsequent read
		 * operations will read new data from the server.
		 */
		cifs_zap_mapping(inode);
		cifs_dbg(FYI, "Set Oplock/Lease to NONE for inode=%p after write\n",
			 inode);
		cinode->oplock = 0;
	}
out:
	cifs_put_writer(cinode);
	return written;
}

static struct cifs_readdata *
cifs_readdata_alloc(unsigned int nr_pages, work_func_t complete)
{
	struct cifs_readdata *rdata;

	rdata = kzalloc(sizeof(*rdata) + (sizeof(struct page *) * nr_pages),
			GFP_KERNEL);
	if (rdata != NULL) {
		kref_init(&rdata->refcount);
		INIT_LIST_HEAD(&rdata->list);
		init_completion(&rdata->done);
		INIT_WORK(&rdata->work, complete);
	}

	return rdata;
}

void
cifs_readdata_release(struct kref *refcount)
{
	struct cifs_readdata *rdata = container_of(refcount,
					struct cifs_readdata, refcount);

	if (rdata->cfile)
		cifsFileInfo_put(rdata->cfile);

	kfree(rdata);
}

static int
cifs_read_allocate_pages(struct cifs_readdata *rdata, unsigned int nr_pages)
{
	int rc = 0;
	struct page *page;
	unsigned int i;

	for (i = 0; i < nr_pages; i++) {
		page = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
		if (!page) {
			rc = -ENOMEM;
			break;
		}
		rdata->pages[i] = page;
	}

	if (rc) {
		unsigned int nr_page_failed = i;

		for (i = 0; i < nr_page_failed; i++) {
			put_page(rdata->pages[i]);
			rdata->pages[i] = NULL;
		}
	}
	return rc;
}

static void
cifs_uncached_readdata_release(struct kref *refcount)
{
	struct cifs_readdata *rdata = container_of(refcount,
					struct cifs_readdata, refcount);
	unsigned int i;

	for (i = 0; i < rdata->nr_pages; i++) {
		put_page(rdata->pages[i]);
		rdata->pages[i] = NULL;
	}
	cifs_readdata_release(refcount);
}

/**
 * cifs_readdata_to_iov - copy data from pages in response to an iovec
 * @rdata:	the readdata response with list of pages holding data
 * @iter:	destination for our data
 *
 * This function copies data from a list of pages in a readdata response into
 * an array of iovecs. It will first calculate where the data should go
 * based on the info in the readdata and then copy the data into that spot.
 */
static int
cifs_readdata_to_iov(struct cifs_readdata *rdata, struct iov_iter *iter)
{
	size_t remaining = rdata->got_bytes;
	unsigned int i;

	for (i = 0; i < rdata->nr_pages; i++) {
		struct page *page = rdata->pages[i];
		size_t copy = min_t(size_t, remaining, PAGE_SIZE);
		size_t written = copy_page_to_iter(page, 0, copy, iter);
		remaining -= written;
		if (written < copy && iov_iter_count(iter) > 0)
			break;
	}
	return remaining ? -EFAULT : 0;
}

static void
cifs_uncached_readv_complete(struct work_struct *work)
{
	struct cifs_readdata *rdata = container_of(work,
						struct cifs_readdata, work);

	complete(&rdata->done);
	kref_put(&rdata->refcount, cifs_uncached_readdata_release);
}

static int
cifs_uncached_read_into_pages(struct TCP_Server_Info *server,
			struct cifs_readdata *rdata, unsigned int len)
{
	int result = 0;
	unsigned int i;
	unsigned int nr_pages = rdata->nr_pages;
	struct kvec iov;

	rdata->got_bytes = 0;
	rdata->tailsz = PAGE_SIZE;
	for (i = 0; i < nr_pages; i++) {
		struct page *page = rdata->pages[i];

		if (len >= PAGE_SIZE) {
			/* enough data to fill the page */
			iov.iov_base = kmap(page);
			iov.iov_len = PAGE_SIZE;
			cifs_dbg(FYI, "%u: iov_base=%p iov_len=%zu\n",
				 i, iov.iov_base, iov.iov_len);
			len -= PAGE_SIZE;
		} else if (len > 0) {
			/* enough for partial page, fill and zero the rest */
			iov.iov_base = kmap(page);
			iov.iov_len = len;
			cifs_dbg(FYI, "%u: iov_base=%p iov_len=%zu\n",
				 i, iov.iov_base, iov.iov_len);
			memset(iov.iov_base + len, '\0', PAGE_SIZE - len);
			rdata->tailsz = len;
			len = 0;
		} else {
			/* no need to hold page hostage */
			rdata->pages[i] = NULL;
			rdata->nr_pages--;
			put_page(page);
			continue;
		}

		result = cifs_readv_from_socket(server, &iov, 1, iov.iov_len);
		kunmap(page);
		if (result < 0)
			break;

		rdata->got_bytes += result;
	}

	return rdata->got_bytes > 0 && result != -ECONNABORTED ?
						rdata->got_bytes : result;
}

static int
cifs_send_async_read(loff_t offset, size_t len, struct cifsFileInfo *open_file,
		     struct cifs_sb_info *cifs_sb, struct list_head *rdata_list)
{
	struct cifs_readdata *rdata;
	unsigned int npages, rsize, credits;
	size_t cur_len;
	int rc;
	pid_t pid;
	struct TCP_Server_Info *server;

	server = tlink_tcon(open_file->tlink)->ses->server;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	do {
		rc = server->ops->wait_mtu_credits(server, cifs_sb->rsize,
						   &rsize, &credits);
		if (rc)
			break;

		cur_len = min_t(const size_t, len, rsize);
		npages = DIV_ROUND_UP(cur_len, PAGE_SIZE);

		/* allocate a readdata struct */
		rdata = cifs_readdata_alloc(npages,
					    cifs_uncached_readv_complete);
		if (!rdata) {
			add_credits_and_wake_if(server, credits, 0);
			rc = -ENOMEM;
			break;
		}

		rc = cifs_read_allocate_pages(rdata, npages);
		if (rc)
			goto error;

		rdata->cfile = cifsFileInfo_get(open_file);
		rdata->nr_pages = npages;
		rdata->offset = offset;
		rdata->bytes = cur_len;
		rdata->pid = pid;
		rdata->pagesz = PAGE_SIZE;
		rdata->read_into_pages = cifs_uncached_read_into_pages;
		rdata->credits = credits;

		if (!rdata->cfile->invalidHandle ||
		    !(rc = cifs_reopen_file(rdata->cfile, true)))
			rc = server->ops->async_readv(rdata);
error:
		if (rc) {
			add_credits_and_wake_if(server, rdata->credits, 0);
			kref_put(&rdata->refcount,
				 cifs_uncached_readdata_release);
			if (rc == -EAGAIN)
				continue;
			break;
		}

		list_add_tail(&rdata->list, rdata_list);
		offset += cur_len;
		len -= cur_len;
	} while (len > 0);

	return rc;
}

ssize_t cifs_user_readv(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	ssize_t rc;
	size_t len;
	ssize_t total_read = 0;
	loff_t offset = iocb->ki_pos;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *open_file;
	struct cifs_readdata *rdata, *tmp;
	struct list_head rdata_list;

	len = iov_iter_count(to);
	if (!len)
		return 0;

	INIT_LIST_HEAD(&rdata_list);
	cifs_sb = CIFS_FILE_SB(file);
	open_file = file->private_data;
	tcon = tlink_tcon(open_file->tlink);

	if (!tcon->ses->server->ops->async_readv)
		return -ENOSYS;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cifs_dbg(FYI, "attempting read on write only file instance\n");

	rc = cifs_send_async_read(offset, len, open_file, cifs_sb, &rdata_list);

	/* if at least one read request send succeeded, then reset rc */
	if (!list_empty(&rdata_list))
		rc = 0;

	len = iov_iter_count(to);
	/* the loop below should proceed in the order of increasing offsets */
again:
	list_for_each_entry_safe(rdata, tmp, &rdata_list, list) {
		if (!rc) {
			/* FIXME: freezable sleep too? */
			rc = wait_for_completion_killable(&rdata->done);
			if (rc)
				rc = -EINTR;
			else if (rdata->result == -EAGAIN) {
				/* resend call if it's a retryable error */
				struct list_head tmp_list;
				unsigned int got_bytes = rdata->got_bytes;

				list_del_init(&rdata->list);
				INIT_LIST_HEAD(&tmp_list);

				/*
				 * Got a part of data and then reconnect has
				 * happened -- fill the buffer and continue
				 * reading.
				 */
				if (got_bytes && got_bytes < rdata->bytes) {
					rc = cifs_readdata_to_iov(rdata, to);
					if (rc) {
						kref_put(&rdata->refcount,
						cifs_uncached_readdata_release);
						continue;
					}
				}

				rc = cifs_send_async_read(
						rdata->offset + got_bytes,
						rdata->bytes - got_bytes,
						rdata->cfile, cifs_sb,
						&tmp_list);

				list_splice(&tmp_list, &rdata_list);

				kref_put(&rdata->refcount,
					 cifs_uncached_readdata_release);
				goto again;
			} else if (rdata->result)
				rc = rdata->result;
			else
				rc = cifs_readdata_to_iov(rdata, to);

			/* if there was a short read -- discard anything left */
			if (rdata->got_bytes && rdata->got_bytes < rdata->bytes)
				rc = -ENODATA;
		}
		list_del_init(&rdata->list);
		kref_put(&rdata->refcount, cifs_uncached_readdata_release);
	}

	total_read = len - iov_iter_count(to);

	cifs_stats_bytes_read(tcon, total_read);

	/* mask nodata case */
	if (rc == -ENODATA)
		rc = 0;

	if (total_read) {
		iocb->ki_pos += total_read;
		return total_read;
	}
	return rc;
}

ssize_t
cifs_strict_readv(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)
						iocb->ki_filp->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	int rc = -EACCES;

	/*
	 * In strict cache mode we need to read from the server all the time
	 * if we don't have level II oplock because the server can delay mtime
	 * change - so we can't make a decision about inode invalidating.
	 * And we can also fail with pagereading if there are mandatory locks
	 * on pages affected by this read but not on the region from pos to
	 * pos+len-1.
	 */
	if (!CIFS_CACHE_READ(cinode))
		return cifs_user_readv(iocb, to);

	if (cap_unix(tcon->ses) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		return generic_file_read_iter(iocb, to);

	/*
	 * We need to hold the sem to be sure nobody modifies lock list
	 * with a brlock that prevents reading.
	 */
	down_read(&cinode->lock_sem);
	if (!cifs_find_lock_conflict(cfile, iocb->ki_pos, iov_iter_count(to),
				     tcon->ses->server->vals->shared_lock_type,
				     NULL, CIFS_READ_OP))
		rc = generic_file_read_iter(iocb, to);
	up_read(&cinode->lock_sem);
	return rc;
}

static ssize_t
cifs_read(struct file *file, char *read_data, size_t read_size, loff_t *offset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read;
	unsigned int current_read_size;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	char *current_offset;
	struct cifsFileInfo *open_file;
	int buf_type = CIFS_NO_BUFFER;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cFYI(1, ("attempting read on write only file instance"));

	for (total_read = 0, current_offset = read_data;
	     read_size > total_read;
	     total_read += bytes_read, current_offset += bytes_read) {
		current_read_size = min_t(const int, read_size - total_read,
					  cifs_sb->rsize);
		/* For windows me and 9x we do not want to request more
		than it negotiated since it will refuse the read then */
		if ((pTcon->ses) &&
			!(pTcon->ses->capabilities & CAP_LARGE_FILES)) {
			current_read_size = min_t(const int, current_read_size,
					pTcon->ses->server->maxBuf - 128);
		}
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if ((open_file->invalidHandle) &&
			    (!open_file->closePend)) {
				rc = cifs_reopen_file(file, true);
				if (rc != 0)
					break;
			}
			rc = CIFSSMBRead(xid, pTcon,
					 open_file->netfid,
					 current_read_size, *poffset,
					 &bytes_read, &current_offset,
					 &buf_type);
		}
	unsigned int rsize;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;
	unsigned int xid;
	char *cur_offset;
	struct cifsFileInfo *open_file;
	struct cifs_io_parms io_parms;
	int buf_type = CIFS_NO_BUFFER;
	__u32 pid;

	xid = get_xid();
	cifs_sb = CIFS_FILE_SB(file);

	/* FIXME: set up handlers for larger reads and/or convert to async */
	rsize = min_t(unsigned int, cifs_sb->rsize, CIFSMaxBufSize);

	if (file->private_data == NULL) {
		rc = -EBADF;
		free_xid(xid);
		return rc;
	}
	open_file = file->private_data;
	tcon = tlink_tcon(open_file->tlink);
	server = tcon->ses->server;

	if (!server->ops->sync_read) {
		free_xid(xid);
		return -ENOSYS;
	}

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cifs_dbg(FYI, "attempting read on write only file instance\n");

	for (total_read = 0, cur_offset = read_data; read_size > total_read;
	     total_read += bytes_read, cur_offset += bytes_read) {
		do {
			current_read_size = min_t(uint, read_size - total_read,
						  rsize);
			/*
			 * For windows me and 9x we do not want to request more
			 * than it negotiated since it will refuse the read
			 * then.
			 */
			if (!(tcon->ses->capabilities &
				tcon->ses->server->vals->cap_large_files)) {
				current_read_size = min_t(uint,
					current_read_size, CIFSMaxBufSize);
			}
			if (open_file->invalidHandle) {
				rc = cifs_reopen_file(open_file, true);
				if (rc != 0)
					break;
			}
			io_parms.pid = pid;
			io_parms.tcon = tcon;
			io_parms.offset = *offset;
			io_parms.length = current_read_size;
			rc = server->ops->sync_read(xid, &open_file->fid, &io_parms,
						    &bytes_read, &cur_offset,
						    &buf_type);
		} while (rc == -EAGAIN);

		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else {
			cifs_stats_bytes_read(pTcon, total_read);
			*poffset += bytes_read;
		}
	}
	FreeXid(xid);
	return total_read;
}

int cifs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dentry *dentry = file->f_path.dentry;
	int rc, xid;

	xid = GetXid();
	rc = cifs_revalidate(dentry);
	if (rc) {
		cFYI(1, ("Validation prior to mmap failed, error=%d", rc));
		FreeXid(xid);
		return rc;
	}
	rc = generic_file_mmap(file, vma);
	FreeXid(xid);
	return rc;
}


static void cifs_copy_cache_pages(struct address_space *mapping,
	struct list_head *pages, int bytes_read, char *data,
	struct pagevec *plru_pvec)
{
	struct page *page;
	char *target;

	while (bytes_read > 0) {
		if (list_empty(pages))
			break;

		page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);

		if (add_to_page_cache(page, mapping, page->index,
				      GFP_KERNEL)) {
			page_cache_release(page);
			cFYI(1, ("Add page cache failed"));
			data += PAGE_CACHE_SIZE;
			bytes_read -= PAGE_CACHE_SIZE;
			continue;
		}

		target = kmap_atomic(page, KM_USER0);

		if (PAGE_CACHE_SIZE > bytes_read) {
			memcpy(target, data, bytes_read);
			/* zero the tail end of this partial page */
			memset(target + bytes_read, 0,
			       PAGE_CACHE_SIZE - bytes_read);
			bytes_read = 0;
		} else {
			memcpy(target, data, PAGE_CACHE_SIZE);
			bytes_read -= PAGE_CACHE_SIZE;
		}
		kunmap_atomic(target, KM_USER0);

		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);
		if (!pagevec_add(plru_pvec, page))
			__pagevec_lru_add(plru_pvec);
		data += PAGE_CACHE_SIZE;
	}
	return;
				free_xid(xid);
				return rc;
			}
		} else {
			cifs_stats_bytes_read(tcon, total_read);
			*offset += bytes_read;
		}
	}
	free_xid(xid);
	return total_read;
}

/*
 * If the page is mmap'ed into a process' page tables, then we need to make
 * sure that it doesn't change while being written back.
 */
static int
cifs_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;

	lock_page(page);
	return VM_FAULT_LOCKED;
}

static const struct vm_operations_struct cifs_file_vm_ops = {
	.fault = filemap_fault,
	.map_pages = filemap_map_pages,
	.page_mkwrite = cifs_page_mkwrite,
};

int cifs_file_strict_mmap(struct file *file, struct vm_area_struct *vma)
{
	int xid, rc = 0;
	struct inode *inode = file_inode(file);

	xid = get_xid();

	if (!CIFS_CACHE_READ(CIFS_I(inode)))
		rc = cifs_zap_mapping(inode);
	if (!rc)
		rc = generic_file_mmap(file, vma);
	if (!rc)
		vma->vm_ops = &cifs_file_vm_ops;

	free_xid(xid);
	return rc;
}

int cifs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc, xid;

	xid = get_xid();

	rc = cifs_revalidate_file(file);
	if (rc)
		cifs_dbg(FYI, "Validation prior to mmap failed, error=%d\n",
			 rc);
	if (!rc)
		rc = generic_file_mmap(file, vma);
	if (!rc)
		vma->vm_ops = &cifs_file_vm_ops;

	free_xid(xid);
	return rc;
}

static void
cifs_readv_complete(struct work_struct *work)
{
	unsigned int i, got_bytes;
	struct cifs_readdata *rdata = container_of(work,
						struct cifs_readdata, work);

	got_bytes = rdata->got_bytes;
	for (i = 0; i < rdata->nr_pages; i++) {
		struct page *page = rdata->pages[i];

		lru_cache_add_file(page);

		if (rdata->result == 0 ||
		    (rdata->result == -EAGAIN && got_bytes)) {
			flush_dcache_page(page);
			SetPageUptodate(page);
		}

		unlock_page(page);

		if (rdata->result == 0 ||
		    (rdata->result == -EAGAIN && got_bytes))
			cifs_readpage_to_fscache(rdata->mapping->host, page);

		got_bytes -= min_t(unsigned int, PAGE_CACHE_SIZE, got_bytes);

		page_cache_release(page);
		rdata->pages[i] = NULL;
	}
	kref_put(&rdata->refcount, cifs_readdata_release);
}

static int
cifs_readpages_read_into_pages(struct TCP_Server_Info *server,
			struct cifs_readdata *rdata, unsigned int len)
{
	int result = 0;
	unsigned int i;
	u64 eof;
	pgoff_t eof_index;
	unsigned int nr_pages = rdata->nr_pages;
	struct kvec iov;

	/* determine the eof that the server (probably) has */
	eof = CIFS_I(rdata->mapping->host)->server_eof;
	eof_index = eof ? (eof - 1) >> PAGE_CACHE_SHIFT : 0;
	cifs_dbg(FYI, "eof=%llu eof_index=%lu\n", eof, eof_index);

	rdata->got_bytes = 0;
	rdata->tailsz = PAGE_CACHE_SIZE;
	for (i = 0; i < nr_pages; i++) {
		struct page *page = rdata->pages[i];

		if (len >= PAGE_CACHE_SIZE) {
			/* enough data to fill the page */
			iov.iov_base = kmap(page);
			iov.iov_len = PAGE_CACHE_SIZE;
			cifs_dbg(FYI, "%u: idx=%lu iov_base=%p iov_len=%zu\n",
				 i, page->index, iov.iov_base, iov.iov_len);
			len -= PAGE_CACHE_SIZE;
		} else if (len > 0) {
			/* enough for partial page, fill and zero the rest */
			iov.iov_base = kmap(page);
			iov.iov_len = len;
			cifs_dbg(FYI, "%u: idx=%lu iov_base=%p iov_len=%zu\n",
				 i, page->index, iov.iov_base, iov.iov_len);
			memset(iov.iov_base + len,
				'\0', PAGE_CACHE_SIZE - len);
			rdata->tailsz = len;
			len = 0;
		} else if (page->index > eof_index) {
			/*
			 * The VFS will not try to do readahead past the
			 * i_size, but it's possible that we have outstanding
			 * writes with gaps in the middle and the i_size hasn't
			 * caught up yet. Populate those with zeroed out pages
			 * to prevent the VFS from repeatedly attempting to
			 * fill them until the writes are flushed.
			 */
			zero_user(page, 0, PAGE_CACHE_SIZE);
			lru_cache_add_file(page);
			flush_dcache_page(page);
			SetPageUptodate(page);
			unlock_page(page);
			page_cache_release(page);
			rdata->pages[i] = NULL;
			rdata->nr_pages--;
			continue;
		} else {
			/* no need to hold page hostage */
			lru_cache_add_file(page);
			unlock_page(page);
			page_cache_release(page);
			rdata->pages[i] = NULL;
			rdata->nr_pages--;
			continue;
		}

		result = cifs_readv_from_socket(server, &iov, 1, iov.iov_len);
		kunmap(page);
		if (result < 0)
			break;

		rdata->got_bytes += result;
	}

	return rdata->got_bytes > 0 && result != -ECONNABORTED ?
						rdata->got_bytes : result;
}

static int
readpages_get_pages(struct address_space *mapping, struct list_head *page_list,
		    unsigned int rsize, struct list_head *tmplist,
		    unsigned int *nr_pages, loff_t *offset, unsigned int *bytes)
{
	struct page *page, *tpage;
	unsigned int expected_index;
	int rc;
	gfp_t gfp = mapping_gfp_constraint(mapping, GFP_KERNEL);

	INIT_LIST_HEAD(tmplist);

	page = list_entry(page_list->prev, struct page, lru);

	/*
	 * Lock the page and put it in the cache. Since no one else
	 * should have access to this page, we're safe to simply set
	 * PG_locked without checking it first.
	 */
	__set_page_locked(page);
	rc = add_to_page_cache_locked(page, mapping,
				      page->index, gfp);

	/* give up if we can't stick it in the cache */
	if (rc) {
		__clear_page_locked(page);
		return rc;
	}

	/* move first page to the tmplist */
	*offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	*bytes = PAGE_CACHE_SIZE;
	*nr_pages = 1;
	list_move_tail(&page->lru, tmplist);

	/* now try and add more pages onto the request */
	expected_index = page->index + 1;
	list_for_each_entry_safe_reverse(page, tpage, page_list, lru) {
		/* discontinuity ? */
		if (page->index != expected_index)
			break;

		/* would this page push the read over the rsize? */
		if (*bytes + PAGE_CACHE_SIZE > rsize)
			break;

		__set_page_locked(page);
		if (add_to_page_cache_locked(page, mapping, page->index, gfp)) {
			__clear_page_locked(page);
			break;
		}
		list_move_tail(&page->lru, tmplist);
		(*bytes) += PAGE_CACHE_SIZE;
		expected_index++;
		(*nr_pages)++;
	}
	return rc;
}

static int cifs_readpages(struct file *file, struct address_space *mapping,
	struct list_head *page_list, unsigned num_pages)
{
	int rc = -EACCES;
	int xid;
	loff_t offset;
	struct page *page;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	unsigned int bytes_read = 0;
	unsigned int read_size, i;
	char *smb_read_data = NULL;
	struct smb_com_read_rsp *pSMBr;
	struct pagevec lru_pvec;
	struct cifsFileInfo *open_file;
	int buf_type = CIFS_NO_BUFFER;

	xid = GetXid();
	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	pagevec_init(&lru_pvec, 0);
		cFYI(DBG2, ("rpages: num pages %d", num_pages));
	for (i = 0; i < num_pages; ) {
		unsigned contig_pages;
		struct page *tmp_page;
		unsigned long expected_index;

		if (list_empty(page_list))
			break;

		page = list_entry(page_list->prev, struct page, lru);
		offset = (loff_t)page->index << PAGE_CACHE_SHIFT;

		/* count adjacent pages that we will read into */
		contig_pages = 0;
		expected_index =
			list_entry(page_list->prev, struct page, lru)->index;
		list_for_each_entry_reverse(tmp_page, page_list, lru) {
			if (tmp_page->index == expected_index) {
				contig_pages++;
				expected_index++;
			} else
				break;
		}
		if (contig_pages + i >  num_pages)
			contig_pages = num_pages - i;

		/* for reads over a certain size could initiate async
		   read ahead */

		read_size = contig_pages * PAGE_CACHE_SIZE;
		/* Read size needs to be in multiples of one page */
		read_size = min_t(const unsigned int, read_size,
				  cifs_sb->rsize & PAGE_CACHE_MASK);
		cFYI(DBG2, ("rpages: read size 0x%x  contiguous pages %d",
				read_size, contig_pages));
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if ((open_file->invalidHandle) &&
			    (!open_file->closePend)) {
				rc = cifs_reopen_file(file, true);
				if (rc != 0)
					break;
			}

			rc = CIFSSMBRead(xid, pTcon,
					 open_file->netfid,
					 read_size, offset,
					 &bytes_read, &smb_read_data,
					 &buf_type);
			/* BB more RC checks ? */
			if (rc == -EAGAIN) {
				if (smb_read_data) {
					if (buf_type == CIFS_SMALL_BUFFER)
						cifs_small_buf_release(smb_read_data);
					else if (buf_type == CIFS_LARGE_BUFFER)
						cifs_buf_release(smb_read_data);
					smb_read_data = NULL;
				}
			}
		}
		if ((rc < 0) || (smb_read_data == NULL)) {
			cFYI(1, ("Read error in readpages: %d", rc));
			break;
		} else if (bytes_read > 0) {
			task_io_account_read(bytes_read);
			pSMBr = (struct smb_com_read_rsp *)smb_read_data;
			cifs_copy_cache_pages(mapping, page_list, bytes_read,
				smb_read_data + 4 /* RFC1001 hdr */ +
				le16_to_cpu(pSMBr->DataOffset), &lru_pvec);

			i +=  bytes_read >> PAGE_CACHE_SHIFT;
			cifs_stats_bytes_read(pTcon, bytes_read);
			if ((bytes_read & PAGE_CACHE_MASK) != bytes_read) {
				i++; /* account for partial page */

				/* server copy of file can have smaller size
				   than client */
				/* BB do we need to verify this common case ?
				   this case is ok - if we are at server EOF
				   we will hit it on next read */

				/* break; */
			}
		} else {
			cFYI(1, ("No bytes read (%d) at offset %lld . "
				 "Cleaning remaining pages from readahead list",
				 bytes_read, offset));
			/* BB turn off caching and do new lookup on
			   file size at server? */
			break;
		}
		if (smb_read_data) {
			if (buf_type == CIFS_SMALL_BUFFER)
				cifs_small_buf_release(smb_read_data);
			else if (buf_type == CIFS_LARGE_BUFFER)
				cifs_buf_release(smb_read_data);
			smb_read_data = NULL;
		}
		bytes_read = 0;
	}

	pagevec_lru_add(&lru_pvec);

/* need to free smb_read_data buf before exit */
	if (smb_read_data) {
		if (buf_type == CIFS_SMALL_BUFFER)
			cifs_small_buf_release(smb_read_data);
		else if (buf_type == CIFS_LARGE_BUFFER)
			cifs_buf_release(smb_read_data);
		smb_read_data = NULL;
	}

	FreeXid(xid);
	return rc;
}

	int rc;
	struct list_head tmplist;
	struct cifsFileInfo *open_file = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct TCP_Server_Info *server;
	pid_t pid;

	/*
	 * Reads as many pages as possible from fscache. Returns -ENOBUFS
	 * immediately if the cookie is negative
	 *
	 * After this point, every page in the list might have PG_fscache set,
	 * so we will need to clean that up off of every page we don't use.
	 */
	rc = cifs_readpages_from_fscache(mapping->host, mapping, page_list,
					 &num_pages);
	if (rc == 0)
		return rc;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	rc = 0;
	server = tlink_tcon(open_file->tlink)->ses->server;

	cifs_dbg(FYI, "%s: file=%p mapping=%p num_pages=%u\n",
		 __func__, file, mapping, num_pages);

	/*
	 * Start with the page at end of list and move it to private
	 * list. Do the same with any following pages until we hit
	 * the rsize limit, hit an index discontinuity, or run out of
	 * pages. Issue the async read and then start the loop again
	 * until the list is empty.
	 *
	 * Note that list order is important. The page_list is in
	 * the order of declining indexes. When we put the pages in
	 * the rdata->pages, then we want them in increasing order.
	 */
	while (!list_empty(page_list)) {
		unsigned int i, nr_pages, bytes, rsize;
		loff_t offset;
		struct page *page, *tpage;
		struct cifs_readdata *rdata;
		unsigned credits;

		rc = server->ops->wait_mtu_credits(server, cifs_sb->rsize,
						   &rsize, &credits);
		if (rc)
			break;

		/*
		 * Give up immediately if rsize is too small to read an entire
		 * page. The VFS will fall back to readpage. We should never
		 * reach this point however since we set ra_pages to 0 when the
		 * rsize is smaller than a cache page.
		 */
		if (unlikely(rsize < PAGE_CACHE_SIZE)) {
			add_credits_and_wake_if(server, credits, 0);
			return 0;
		}

		rc = readpages_get_pages(mapping, page_list, rsize, &tmplist,
					 &nr_pages, &offset, &bytes);
		if (rc) {
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		rdata = cifs_readdata_alloc(nr_pages, cifs_readv_complete);
		if (!rdata) {
			/* best to give up if we're out of mem */
			list_for_each_entry_safe(page, tpage, &tmplist, lru) {
				list_del(&page->lru);
				lru_cache_add_file(page);
				unlock_page(page);
				page_cache_release(page);
			}
			rc = -ENOMEM;
			add_credits_and_wake_if(server, credits, 0);
			break;
		}

		rdata->cfile = cifsFileInfo_get(open_file);
		rdata->mapping = mapping;
		rdata->offset = offset;
		rdata->bytes = bytes;
		rdata->pid = pid;
		rdata->pagesz = PAGE_CACHE_SIZE;
		rdata->read_into_pages = cifs_readpages_read_into_pages;
		rdata->credits = credits;

		list_for_each_entry_safe(page, tpage, &tmplist, lru) {
			list_del(&page->lru);
			rdata->pages[rdata->nr_pages++] = page;
		}

		if (!rdata->cfile->invalidHandle ||
		    !(rc = cifs_reopen_file(rdata->cfile, true)))
			rc = server->ops->async_readv(rdata);
		if (rc) {
			add_credits_and_wake_if(server, rdata->credits, 0);
			for (i = 0; i < rdata->nr_pages; i++) {
				page = rdata->pages[i];
				lru_cache_add_file(page);
				unlock_page(page);
				page_cache_release(page);
			}
			/* Fallback to the readpage in error/reconnect cases */
			kref_put(&rdata->refcount, cifs_readdata_release);
			break;
		}

		kref_put(&rdata->refcount, cifs_readdata_release);
	}

	/* Any pages that have been shown to fscache but didn't get added to
	 * the pagecache must be uncached before they get returned to the
	 * allocator.
	 */
	cifs_fscache_readpages_cancel(mapping->host, page_list);
	return rc;
}

/*
 * cifs_readpage_worker must be called with the page pinned
 */
static int cifs_readpage_worker(struct file *file, struct page *page,
	loff_t *poffset)
{
	char *read_data;
	int rc;

	page_cache_get(page);
	/* Is the page cached? */
	rc = cifs_readpage_from_fscache(file_inode(file), page);
	if (rc == 0)
		goto read_complete;

	read_data = kmap(page);
	/* for reads over a certain size could initiate async read ahead */

	rc = cifs_read(file, read_data, PAGE_CACHE_SIZE, poffset);

	if (rc < 0)
		goto io_error;
	else
		cFYI(1, ("Bytes read %d", rc));

	file->f_path.dentry->d_inode->i_atime =
		current_fs_time(file->f_path.dentry->d_inode->i_sb);
		cifs_dbg(FYI, "Bytes read %d\n", rc);

	file_inode(file)->i_atime =
		current_fs_time(file_inode(file)->i_sb);

	if (PAGE_CACHE_SIZE > rc)
		memset(read_data + rc, 0, PAGE_CACHE_SIZE - rc);

	flush_dcache_page(page);
	SetPageUptodate(page);

	/* send this page to the cache */
	cifs_readpage_to_fscache(file_inode(file), page);

	rc = 0;

io_error:
	kunmap(page);
	page_cache_release(page);
	unlock_page(page);

read_complete:
	unlock_page(page);
	return rc;
}

static int cifs_readpage(struct file *file, struct page *page)
{
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	int rc = -EACCES;
	int xid;

	xid = GetXid();

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	cFYI(1, ("readpage %p at offset %d 0x%x\n",
		 page, (int)offset, (int)offset));

	rc = cifs_readpage_worker(file, page, &offset);

	unlock_page(page);

	FreeXid(xid);
	unsigned int xid;

	xid = get_xid();

	if (file->private_data == NULL) {
		rc = -EBADF;
		free_xid(xid);
		return rc;
	}

	cifs_dbg(FYI, "readpage %p at offset %d 0x%x\n",
		 page, (int)offset, (int)offset);

	rc = cifs_readpage_worker(file, page, &offset);

	free_xid(xid);
	return rc;
}

static int is_inode_writable(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file;
	struct cifs_tcon *tcon =
		cifs_sb_master_tcon(CIFS_SB(cifs_inode->vfs_inode.i_sb));

	read_lock(&GlobalSMBSeslock);
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (open_file->closePend)
			continue;
		if (open_file->pfile &&
		    ((open_file->pfile->f_flags & O_RDWR) ||
		     (open_file->pfile->f_flags & O_WRONLY))) {
			read_unlock(&GlobalSMBSeslock);
			return 1;
		}
	}
	read_unlock(&GlobalSMBSeslock);
	spin_lock(&cifs_file_list_lock);
	spin_lock(&tcon->open_file_lock);
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (OPEN_FMODE(open_file->f_flags) & FMODE_WRITE) {
			spin_unlock(&tcon->open_file_lock);
			return 1;
		}
	}
	spin_unlock(&tcon->open_file_lock);
	return 0;
}

/* We do not want to update the file size from server for inodes
   open for write - to avoid races with writepage extending
   the file - in the future we could consider allowing
   refreshing the inode only on increases in the file size
   but this is tricky to do without racing with writebehind
   page caching in the current Linux kernel design */
bool is_size_safe_to_change(struct cifsInodeInfo *cifsInode, __u64 end_of_file)
{
	if (!cifsInode)
		return true;

	if (is_inode_writable(cifsInode)) {
		/* This inode is open for write at least once */
		struct cifs_sb_info *cifs_sb;

		cifs_sb = CIFS_SB(cifsInode->vfs_inode.i_sb);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			/* since no page cache to corrupt on directio
			we can change size safely */
			return true;
		}

		if (i_size_read(&cifsInode->vfs_inode) < end_of_file)
			return true;

		return false;
	} else
		return true;
}

static int cifs_prepare_write(struct file *file, struct page *page,
	unsigned from, unsigned to)
{
	int rc = 0;
	loff_t i_size;
	loff_t offset;

	cFYI(1, ("prepare write for page %p from %d to %d", page, from, to));
	if (PageUptodate(page))
		return 0;

	/* If we are writing a full page it will be up to date,
	   no need to read from the server */
	if ((to == PAGE_CACHE_SIZE) && (from == 0)) {
		SetPageUptodate(page);
		return 0;
	}

	offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	i_size = i_size_read(page->mapping->host);

	if ((offset >= i_size) ||
	    ((from == 0) && (offset + to) >= i_size)) {
		/*
		 * We don't need to read data beyond the end of the file.
		 * zero it, and set the page uptodate
		 */
		simple_prepare_write(file, page, from, to);
		SetPageUptodate(page);
	} else if ((file->f_flags & O_ACCMODE) != O_WRONLY) {
		/* might as well read a page, it is fast enough */
		rc = cifs_readpage_worker(file, page, &offset);
static int cifs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int oncethru = 0;
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	loff_t offset = pos & (PAGE_CACHE_SIZE - 1);
	loff_t page_start = pos & PAGE_MASK;
	loff_t i_size;
	struct page *page;
	int rc = 0;

	cifs_dbg(FYI, "write_begin from %lld len %d\n", (long long)pos, len);

start:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		rc = -ENOMEM;
		goto out;
	}

	if (PageUptodate(page))
		goto out;

	/*
	 * If we write a full page it will be up to date, no need to read from
	 * the server. If the write is short, we'll end up doing a sync write
	 * instead.
	 */
	if (len == PAGE_CACHE_SIZE)
		goto out;

	/*
	 * optimize away the read when we have an oplock, and we're not
	 * expecting to use any of the data we'd be reading in. That
	 * is, when the page lies beyond the EOF, or straddles the EOF
	 * and the write will cover all of the existing data.
	 */
	if (CIFS_CACHE_READ(CIFS_I(mapping->host))) {
		i_size = i_size_read(mapping->host);
		if (page_start >= i_size ||
		    (offset == 0 && (pos + len) >= i_size)) {
			zero_user_segments(page, 0, offset,
					   offset + len,
					   PAGE_CACHE_SIZE);
			/*
			 * PageChecked means that the parts of the page
			 * to which we're not writing are considered up
			 * to date. Once the data is copied to the
			 * page, it can be set uptodate.
			 */
			SetPageChecked(page);
			goto out;
		}
	}

	if ((file->f_flags & O_ACCMODE) != O_WRONLY && !oncethru) {
		/*
		 * might as well read a page, it is fast enough. If we get
		 * an error, we don't need to return it. cifs_write_end will
		 * do a sync write instead since PG_uptodate isn't set.
		 */
		cifs_readpage_worker(file, page, &page_start);
		page_cache_release(page);
		oncethru = 1;
		goto start;
	} else {
		/* we could try using another file handle if there is one -
		   but how would we lock it to prevent close of that handle
		   racing with this read? In any case
		   this will be written out by commit_write so is fine */
	}

	/* we do not need to pass errors back
	   e.g. if we do not have read access to the file
	   because cifs_commit_write will do the right thing.  -- shaggy */

	return 0;
}

		   this will be written out by write_end so is fine */
	}
out:
	*pagep = page;
	return rc;
}

static int cifs_release_page(struct page *page, gfp_t gfp)
{
	if (PagePrivate(page))
		return 0;

	return cifs_fscache_release_page(page, gfp);
}

static void cifs_invalidate_page(struct page *page, unsigned int offset,
				 unsigned int length)
{
	struct cifsInodeInfo *cifsi = CIFS_I(page->mapping->host);

	if (offset == 0 && length == PAGE_CACHE_SIZE)
		cifs_fscache_invalidate_page(page, &cifsi->vfs_inode);
}

static int cifs_launder_page(struct page *page)
{
	int rc = 0;
	loff_t range_start = page_offset(page);
	loff_t range_end = range_start + (loff_t)(PAGE_CACHE_SIZE - 1);
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,
		.range_start = range_start,
		.range_end = range_end,
	};

	cifs_dbg(FYI, "Launder page: %p\n", page);

	if (clear_page_dirty_for_io(page))
		rc = cifs_writepage_locked(page, &wbc);

	cifs_fscache_invalidate_page(page, page->mapping->host);
	return rc;
}

void cifs_oplock_break(struct work_struct *work)
{
	struct cifsFileInfo *cfile = container_of(work, struct cifsFileInfo,
						  oplock_break);
	struct inode *inode = d_inode(cfile->dentry);
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	int rc = 0;

	wait_on_bit(&cinode->flags, CIFS_INODE_PENDING_WRITERS,
			TASK_UNINTERRUPTIBLE);

	server->ops->downgrade_oplock(server, cinode,
		test_bit(CIFS_INODE_DOWNGRADE_OPLOCK_TO_L2, &cinode->flags));

	if (!CIFS_CACHE_WRITE(cinode) && CIFS_CACHE_READ(cinode) &&
						cifs_has_mand_locks(cinode)) {
		cifs_dbg(FYI, "Reset oplock to None for inode=%p due to mand locks\n",
			 inode);
		cinode->oplock = 0;
	}

	if (inode && S_ISREG(inode->i_mode)) {
		if (CIFS_CACHE_READ(cinode))
			break_lease(inode, O_RDONLY);
		else
			break_lease(inode, O_WRONLY);
		rc = filemap_fdatawrite(inode->i_mapping);
		if (!CIFS_CACHE_READ(cinode)) {
			rc = filemap_fdatawait(inode->i_mapping);
			mapping_set_error(inode->i_mapping, rc);
			cifs_zap_mapping(inode);
		}
		cifs_dbg(FYI, "Oplock flush inode %p rc %d\n", inode, rc);
	}

	rc = cifs_push_locks(cfile);
	if (rc)
		cifs_dbg(VFS, "Push locks rc = %d\n", rc);

	/*
	 * releasing stale oplock after recent reconnect of smb session using
	 * a now incorrect file handle is not a data integrity issue but do
	 * not bother sending an oplock release if session to server still is
	 * disconnected since oplock already released by the server
	 */
	if (!cfile->oplock_break_cancelled) {
		rc = tcon->ses->server->ops->oplock_response(tcon, &cfile->fid,
							     cinode);
		cifs_dbg(FYI, "Oplock release rc = %d\n", rc);
	}
	cifs_done_oplock_break(cinode);
}

/*
 * The presence of cifs_direct_io() in the address space ops vector
 * allowes open() O_DIRECT flags which would have failed otherwise.
 *
 * In the non-cached mode (mount with cache=none), we shunt off direct read and write requests
 * so this method should never be called.
 *
 * Direct IO is not yet supported in the cached mode. 
 */
static ssize_t
cifs_direct_io(struct kiocb *iocb, struct iov_iter *iter, loff_t pos)
{
        /*
         * FIXME
         * Eventually need to support direct IO for non forcedirectio mounts
         */
        return -EINVAL;
}


const struct address_space_operations cifs_addr_ops = {
	.readpage = cifs_readpage,
	.readpages = cifs_readpages,
	.writepage = cifs_writepage,
	.writepages = cifs_writepages,
	.prepare_write = cifs_prepare_write,
	.commit_write = cifs_commit_write,
	.set_page_dirty = __set_page_dirty_nobuffers,
	/* .sync_page = cifs_sync_page, */
	/* .direct_IO = */
	.write_begin = cifs_write_begin,
	.write_end = cifs_write_end,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.releasepage = cifs_release_page,
	.direct_IO = cifs_direct_io,
	.invalidatepage = cifs_invalidate_page,
	.launder_page = cifs_launder_page,
};

/*
 * cifs_readpages requires the server to support a buffer large enough to
 * contain the header plus one complete page of data.  Otherwise, we need
 * to leave cifs_readpages out of the address space operations.
 */
const struct address_space_operations cifs_addr_ops_smallbuf = {
	.readpage = cifs_readpage,
	.writepage = cifs_writepage,
	.writepages = cifs_writepages,
	.prepare_write = cifs_prepare_write,
	.commit_write = cifs_commit_write,
	.set_page_dirty = __set_page_dirty_nobuffers,
	/* .sync_page = cifs_sync_page, */
	/* .direct_IO = */
	.write_begin = cifs_write_begin,
	.write_end = cifs_write_end,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.releasepage = cifs_release_page,
	.invalidatepage = cifs_invalidate_page,
	.launder_page = cifs_launder_page,
};
