/*
 *   fs/cifs/readdir.c
 *
 *   Directory search handling
 *
 *   Copyright (C) International Business Machines  Corp., 2004, 2008
 *   Copyright (C) Red Hat, Inc., 2011
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
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifsfs.h"

/*
 * To be safe - for UCS to UTF-8 with strings loaded with the rare long
 * characters alloc more to account for such multibyte target UTF-8
 * characters.
 */
#define UNICODE_NAME_MAX ((4 * NAME_MAX) + 2)

#ifdef CONFIG_CIFS_DEBUG2
static void dump_cifs_file_struct(struct file *file, char *label)
{
	struct cifsFileInfo *cf;

	if (file) {
		cf = file->private_data;
		if (cf == NULL) {
			cFYI(1, ("empty cifs private file data"));
			return;
		}
		if (cf->invalidHandle)
			cFYI(1, ("invalid handle"));
		if (cf->srch_inf.endOfSearch)
			cFYI(1, ("end of search"));
		if (cf->srch_inf.emptyDir)
			cFYI(1, ("empty dir"));
			cifs_dbg(FYI, "empty cifs private file data\n");
			return;
		}
		if (cf->invalidHandle)
			cifs_dbg(FYI, "invalid handle\n");
		if (cf->srch_inf.endOfSearch)
			cifs_dbg(FYI, "end of search\n");
		if (cf->srch_inf.emptyDir)
			cifs_dbg(FYI, "empty dir\n");
	}
}
#else
static inline void dump_cifs_file_struct(struct file *file, char *label)
{
}
#endif /* DEBUG2 */

/* Returns one if new inode created (which therefore needs to be hashed) */
/* Might check in the future if inode number changed so we can rehash inode */
static int construct_dentry(struct qstr *qstring, struct file *file,
	struct inode **ptmp_inode, struct dentry **pnew_dentry)
{
	struct dentry *tmp_dentry;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int rc = 0;

	cFYI(1, ("For %s", qstring->name));
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	qstring->hash = full_name_hash(qstring->name, qstring->len);
	tmp_dentry = d_lookup(file->f_path.dentry, qstring);
	if (tmp_dentry) {
		cFYI(0, ("existing dentry with inode 0x%p",
			 tmp_dentry->d_inode));
		*ptmp_inode = tmp_dentry->d_inode;
/* BB overwrite old name? i.e. tmp_dentry->d_name and tmp_dentry->d_name.len??*/
		if (*ptmp_inode == NULL) {
			*ptmp_inode = new_inode(file->f_path.dentry->d_sb);
			if (*ptmp_inode == NULL)
				return rc;
			rc = 1;
		}
		if (file->f_path.dentry->d_sb->s_flags & MS_NOATIME)
			(*ptmp_inode)->i_flags |= S_NOATIME | S_NOCMTIME;
	} else {
		tmp_dentry = d_alloc(file->f_path.dentry, qstring);
		if (tmp_dentry == NULL) {
			cERROR(1, ("Failed allocating dentry"));
			*ptmp_inode = NULL;
			return rc;
		}

		*ptmp_inode = new_inode(file->f_path.dentry->d_sb);
		if (pTcon->nocase)
			tmp_dentry->d_op = &cifs_ci_dentry_ops;
		else
			tmp_dentry->d_op = &cifs_dentry_ops;
		if (*ptmp_inode == NULL)
			return rc;
		if (file->f_path.dentry->d_sb->s_flags & MS_NOATIME)
			(*ptmp_inode)->i_flags |= S_NOATIME | S_NOCMTIME;
		rc = 2;
	}

	tmp_dentry->d_time = jiffies;
	*pnew_dentry = tmp_dentry;
	return rc;
}

static void AdjustForTZ(struct cifsTconInfo *tcon, struct inode *inode)
{
	if ((tcon) && (tcon->ses) && (tcon->ses->server)) {
		inode->i_ctime.tv_sec += tcon->ses->server->timeAdj;
		inode->i_mtime.tv_sec += tcon->ses->server->timeAdj;
		inode->i_atime.tv_sec += tcon->ses->server->timeAdj;
	}
	return;
}


static void fill_in_inode(struct inode *tmp_inode, int new_buf_type,
			  char *buf, unsigned int *pobject_type, int isNewInode)
{
	loff_t local_size;
	struct timespec local_mtime;

	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(tmp_inode->i_sb);
	__u32 attr;
	__u64 allocation_size;
	__u64 end_of_file;
	umode_t default_mode;

	/* save mtime and size */
	local_mtime = tmp_inode->i_mtime;
	local_size  = tmp_inode->i_size;

	if (new_buf_type) {
		FILE_DIRECTORY_INFO *pfindData = (FILE_DIRECTORY_INFO *)buf;

		attr = le32_to_cpu(pfindData->ExtFileAttributes);
		allocation_size = le64_to_cpu(pfindData->AllocationSize);
		end_of_file = le64_to_cpu(pfindData->EndOfFile);
		tmp_inode->i_atime =
		      cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
		tmp_inode->i_mtime =
		      cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastWriteTime));
		tmp_inode->i_ctime =
		      cifs_NTtimeToUnix(le64_to_cpu(pfindData->ChangeTime));
	} else { /* legacy, OS2 and DOS style */
/*		struct timespec ts;*/
		FIND_FILE_STANDARD_INFO *pfindData =
			(FIND_FILE_STANDARD_INFO *)buf;

		tmp_inode->i_mtime = cnvrtDosUnixTm(
				le16_to_cpu(pfindData->LastWriteDate),
				le16_to_cpu(pfindData->LastWriteTime));
		tmp_inode->i_atime = cnvrtDosUnixTm(
				le16_to_cpu(pfindData->LastAccessDate),
				le16_to_cpu(pfindData->LastAccessTime));
		tmp_inode->i_ctime = cnvrtDosUnixTm(
				le16_to_cpu(pfindData->LastWriteDate),
				le16_to_cpu(pfindData->LastWriteTime));
		AdjustForTZ(cifs_sb->tcon, tmp_inode);
		attr = le16_to_cpu(pfindData->Attributes);
		allocation_size = le32_to_cpu(pfindData->AllocationSize);
		end_of_file = le32_to_cpu(pfindData->DataSize);
	}

	/* Linux can not store file creation time unfortunately so ignore it */

	cifsInfo->cifsAttrs = attr;
#ifdef CONFIG_CIFS_EXPERIMENTAL
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) {
		/* get more accurate mode via ACL - so force inode refresh */
		cifsInfo->time = 0;
	} else
#endif /* CONFIG_CIFS_EXPERIMENTAL */
		cifsInfo->time = jiffies;

	/* treat dos attribute of read-only as read-only mode bit e.g. 555? */
	/* 2767 perms - indicate mandatory locking */
		/* BB fill in uid and gid here? with help from winbind?
		   or retrieve from NTFS stream extended attribute */
	if (atomic_read(&cifsInfo->inUse) == 0) {
		tmp_inode->i_uid = cifs_sb->mnt_uid;
		tmp_inode->i_gid = cifs_sb->mnt_gid;
	}

	if (attr & ATTR_DIRECTORY)
		default_mode = cifs_sb->mnt_dir_mode;
	else
		default_mode = cifs_sb->mnt_file_mode;

	/* set initial permissions */
	if ((atomic_read(&cifsInfo->inUse) == 0) ||
	    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM) == 0)
		tmp_inode->i_mode = default_mode;
	else {
		/* just reenable write bits if !ATTR_READONLY */
		if ((tmp_inode->i_mode & S_IWUGO) == 0 &&
		    (attr & ATTR_READONLY) == 0)
			tmp_inode->i_mode |= (S_IWUGO & default_mode);

		tmp_inode->i_mode &= ~S_IFMT;
	}

	/* clear write bits if ATTR_READONLY is set */
	if (attr & ATTR_READONLY)
		tmp_inode->i_mode &= ~S_IWUGO;

	/* set inode type */
	if ((attr & ATTR_SYSTEM) &&
	    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL)) {
		if (end_of_file == 0)  {
			tmp_inode->i_mode |= S_IFIFO;
			*pobject_type = DT_FIFO;
		} else {
			/*
			 * trying to get the type can be slow, so just call
			 * this a regular file for now, and mark for reval
			 */
			tmp_inode->i_mode |= S_IFREG;
			*pobject_type = DT_REG;
			cifsInfo->time = 0;
		}
	} else {
		if (attr & ATTR_DIRECTORY) {
			tmp_inode->i_mode |= S_IFDIR;
			*pobject_type = DT_DIR;
		} else {
			tmp_inode->i_mode |= S_IFREG;
			*pobject_type = DT_REG;
		}
	}

	/* can not fill in nlink here as in qpathinfo version and Unx search */
	if (atomic_read(&cifsInfo->inUse) == 0)
		atomic_set(&cifsInfo->inUse, 1);

	spin_lock(&tmp_inode->i_lock);
	if (is_size_safe_to_change(cifsInfo, end_of_file)) {
		/* can not safely change the file size here if the
		client is writing to it due to potential races */
		i_size_write(tmp_inode, end_of_file);

	/* 512 bytes (2**9) is the fake blocksize that must be used */
	/* for this calculation, even though the reported blocksize is larger */
		tmp_inode->i_blocks = (512 - 1 + allocation_size) >> 9;
	}
	spin_unlock(&tmp_inode->i_lock);

	if (allocation_size < end_of_file)
		cFYI(1, ("May be sparse file, allocation less than file size"));
	cFYI(1, ("File Size %ld and blocks %llu",
		(unsigned long)tmp_inode->i_size,
		(unsigned long long)tmp_inode->i_blocks));
	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, ("File inode"));
		tmp_inode->i_op = &cifs_file_inode_ops;
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				tmp_inode->i_fop = &cifs_file_direct_nobrl_ops;
			else
				tmp_inode->i_fop = &cifs_file_direct_ops;
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			tmp_inode->i_fop = &cifs_file_nobrl_ops;
		else
			tmp_inode->i_fop = &cifs_file_ops;

		if ((cifs_sb->tcon) && (cifs_sb->tcon->ses) &&
		   (cifs_sb->tcon->ses->server->maxBuf <
			PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE))
			tmp_inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
		else
			tmp_inode->i_data.a_ops = &cifs_addr_ops;

		if (isNewInode)
			return; /* No sense invalidating pages for new inode
				   since have not started caching readahead file
				   data yet */

		if (timespec_equal(&tmp_inode->i_mtime, &local_mtime) &&
			(local_size == tmp_inode->i_size)) {
			cFYI(1, ("inode exists but unchanged"));
		} else {
			/* file may have changed on server */
			cFYI(1, ("invalidate inode, readdir detected change"));
			invalidate_remote_inode(tmp_inode);
		}
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, ("Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, ("Symbolic Link inode"));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
	} else {
		cFYI(1, ("Init special inode"));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
	}
}

static void unix_fill_in_inode(struct inode *tmp_inode,
	FILE_UNIX_INFO *pfindData, unsigned int *pobject_type, int isNewInode)
{
	loff_t local_size;
	struct timespec local_mtime;

	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(tmp_inode->i_sb);

	__u32 type = le32_to_cpu(pfindData->Type);
	__u64 num_of_bytes = le64_to_cpu(pfindData->NumOfBytes);
	__u64 end_of_file = le64_to_cpu(pfindData->EndOfFile);
	cifsInfo->time = jiffies;
	atomic_inc(&cifsInfo->inUse);

	/* save mtime and size */
	local_mtime = tmp_inode->i_mtime;
	local_size  = tmp_inode->i_size;

	tmp_inode->i_atime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastModificationTime));
	tmp_inode->i_ctime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastStatusChange));

	tmp_inode->i_mode = le64_to_cpu(pfindData->Permissions);
	/* since we set the inode type below we need to mask off type
	   to avoid strange results if bits above were corrupt */
	tmp_inode->i_mode &= ~S_IFMT;
	if (type == UNIX_FILE) {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
	} else if (type == UNIX_SYMLINK) {
		*pobject_type = DT_LNK;
		tmp_inode->i_mode |= S_IFLNK;
	} else if (type == UNIX_DIR) {
		*pobject_type = DT_DIR;
		tmp_inode->i_mode |= S_IFDIR;
	} else if (type == UNIX_CHARDEV) {
		*pobject_type = DT_CHR;
		tmp_inode->i_mode |= S_IFCHR;
		tmp_inode->i_rdev = MKDEV(le64_to_cpu(pfindData->DevMajor),
				le64_to_cpu(pfindData->DevMinor) & MINORMASK);
	} else if (type == UNIX_BLOCKDEV) {
		*pobject_type = DT_BLK;
		tmp_inode->i_mode |= S_IFBLK;
		tmp_inode->i_rdev = MKDEV(le64_to_cpu(pfindData->DevMajor),
				le64_to_cpu(pfindData->DevMinor) & MINORMASK);
	} else if (type == UNIX_FIFO) {
		*pobject_type = DT_FIFO;
		tmp_inode->i_mode |= S_IFIFO;
	} else if (type == UNIX_SOCKET) {
		*pobject_type = DT_SOCK;
		tmp_inode->i_mode |= S_IFSOCK;
	} else {
		/* safest to just call it a file */
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
		cFYI(1, ("unknown inode type %d", type));
	}

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_UID)
		tmp_inode->i_uid = cifs_sb->mnt_uid;
	else
		tmp_inode->i_uid = le64_to_cpu(pfindData->Uid);
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_OVERR_GID)
		tmp_inode->i_gid = cifs_sb->mnt_gid;
	else
		tmp_inode->i_gid = le64_to_cpu(pfindData->Gid);
	tmp_inode->i_nlink = le64_to_cpu(pfindData->Nlinks);

	spin_lock(&tmp_inode->i_lock);
	if (is_size_safe_to_change(cifsInfo, end_of_file)) {
		/* can not safely change the file size here if the
		client is writing to it due to potential races */
		i_size_write(tmp_inode, end_of_file);

	/* 512 bytes (2**9) is the fake blocksize that must be used */
	/* for this calculation, not the real blocksize */
		tmp_inode->i_blocks = (512 - 1 + num_of_bytes) >> 9;
	}
	spin_unlock(&tmp_inode->i_lock);

	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, ("File inode"));
		tmp_inode->i_op = &cifs_file_inode_ops;

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
				tmp_inode->i_fop = &cifs_file_direct_nobrl_ops;
			else
				tmp_inode->i_fop = &cifs_file_direct_ops;
		} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			tmp_inode->i_fop = &cifs_file_nobrl_ops;
		else
			tmp_inode->i_fop = &cifs_file_ops;

		if ((cifs_sb->tcon) && (cifs_sb->tcon->ses) &&
		   (cifs_sb->tcon->ses->server->maxBuf <
			PAGE_CACHE_SIZE + MAX_CIFS_HDR_SIZE))
			tmp_inode->i_data.a_ops = &cifs_addr_ops_smallbuf;
		else
			tmp_inode->i_data.a_ops = &cifs_addr_ops;

		if (isNewInode)
			return; /* No sense invalidating pages for new inode
				   since we have not started caching readahead
				   file data for it yet */

		if (timespec_equal(&tmp_inode->i_mtime, &local_mtime) &&
			(local_size == tmp_inode->i_size)) {
			cFYI(1, ("inode exists but unchanged"));
		} else {
			/* file may have changed on server */
			cFYI(1, ("invalidate inode, readdir detected change"));
			invalidate_remote_inode(tmp_inode);
		}
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, ("Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, ("Symbolic Link inode"));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
	} else {
		cFYI(1, ("Special inode"));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
	}
}

static int initiate_cifs_search(const int xid, struct file *file)
{
	int rc = 0;
	char *full_path;
	struct cifsFileInfo *cifsFile;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	if (file->private_data == NULL) {
		file->private_data =
			kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	}

	if (file->private_data == NULL)
		return -ENOMEM;
	cifsFile = file->private_data;
	cifsFile->invalidHandle = true;
	cifsFile->srch_inf.endOfSearch = false;

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	if (cifs_sb == NULL)
		return -EINVAL;

	pTcon = cifs_sb->tcon;
	if (pTcon == NULL)
		return -EINVAL;

	full_path = build_path_from_dentry(file->f_path.dentry);

	if (full_path == NULL)
		return -ENOMEM;

	cFYI(1, ("Full path: %s start at: %lld", full_path, file->f_pos));
/*
 * Attempt to preload the dcache with the results from the FIND_FIRST/NEXT
 *
 * Find the dentry that matches "name". If there isn't one, create one. If it's
 * a negative dentry or the uniqueid or filetype(mode) changed,
 * then drop it and recreate it.
 */
static void
cifs_prime_dcache(struct dentry *parent, struct qstr *name,
		    struct cifs_fattr *fattr)
{
	struct dentry *dentry, *alias;
	struct inode *inode;
	struct super_block *sb = d_inode(parent)->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);

	cifs_dbg(FYI, "%s: for %s\n", __func__, name->name);

	dentry = d_hash_and_lookup(parent, name);
	if (IS_ERR(dentry))
		return;

	if (dentry) {
		inode = d_inode(dentry);
		if (inode) {
			if (d_mountpoint(dentry))
				goto out;
			/*
			 * If we're generating inode numbers, then we don't
			 * want to clobber the existing one with the one that
			 * the readdir code created.
			 */
			if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM))
				fattr->cf_uniqueid = CIFS_I(inode)->uniqueid;

			/* update inode in place
			 * if both i_ino and i_mode didn't change */
			if (CIFS_I(inode)->uniqueid == fattr->cf_uniqueid &&
			    (inode->i_mode & S_IFMT) ==
			    (fattr->cf_mode & S_IFMT)) {
				cifs_fattr_to_inode(inode, fattr);
				goto out;
			}
		}
		d_invalidate(dentry);
		dput(dentry);
	}

	/*
	 * If we know that the inode will need to be revalidated immediately,
	 * then don't create a new dentry for it. We'll end up doing an on
	 * the wire call either way and this spares us an invalidation.
	 */
	if (fattr->cf_flags & CIFS_FATTR_NEED_REVAL)
		return;

	dentry = d_alloc(parent, name);
	if (!dentry)
		return;

	inode = cifs_iget(sb, fattr);
	if (!inode)
		goto out;

	alias = d_splice_alias(inode, dentry);
	if (alias && !IS_ERR(alias))
		dput(alias);
out:
	dput(dentry);
}

static void
cifs_fill_common_info(struct cifs_fattr *fattr, struct cifs_sb_info *cifs_sb)
{
	fattr->cf_uid = cifs_sb->mnt_uid;
	fattr->cf_gid = cifs_sb->mnt_gid;

	if (fattr->cf_cifsattrs & ATTR_DIRECTORY) {
		fattr->cf_mode = S_IFDIR | cifs_sb->mnt_dir_mode;
		fattr->cf_dtype = DT_DIR;
	} else {
		fattr->cf_mode = S_IFREG | cifs_sb->mnt_file_mode;
		fattr->cf_dtype = DT_REG;
	}

	/*
	 * We need to revalidate it further to make a decision about whether it
	 * is a symbolic link, DFS referral or a reparse point with a direct
	 * access like junctions, deduplicated files, NFS symlinks.
	 */
	if (fattr->cf_cifsattrs & ATTR_REPARSE)
		fattr->cf_flags |= CIFS_FATTR_NEED_REVAL;

	/* non-unix readdir doesn't provide nlink */
	fattr->cf_flags |= CIFS_FATTR_UNKNOWN_NLINK;

	if (fattr->cf_cifsattrs & ATTR_READONLY)
		fattr->cf_mode &= ~S_IWUGO;

	/*
	 * We of course don't get ACL info in FIND_FIRST/NEXT results, so
	 * mark it for revalidation so that "ls -l" will look right. It might
	 * be super-slow, but if we don't do this then the ownership of files
	 * may look wrong since the inodes may not have timed out by the time
	 * "ls" does a stat() call on them.
	 */
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL)
		fattr->cf_flags |= CIFS_FATTR_NEED_REVAL;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL &&
	    fattr->cf_cifsattrs & ATTR_SYSTEM) {
		if (fattr->cf_eof == 0)  {
			fattr->cf_mode &= ~S_IFMT;
			fattr->cf_mode |= S_IFIFO;
			fattr->cf_dtype = DT_FIFO;
		} else {
			/*
			 * trying to get the type and mode via SFU can be slow,
			 * so just call those regular files for now, and mark
			 * for reval
			 */
			fattr->cf_flags |= CIFS_FATTR_NEED_REVAL;
		}
	}
}

void
cifs_dir_info_to_fattr(struct cifs_fattr *fattr, FILE_DIRECTORY_INFO *info,
		       struct cifs_sb_info *cifs_sb)
{
	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_cifsattrs = le32_to_cpu(info->ExtFileAttributes);
	fattr->cf_eof = le64_to_cpu(info->EndOfFile);
	fattr->cf_bytes = le64_to_cpu(info->AllocationSize);
	fattr->cf_createtime = le64_to_cpu(info->CreationTime);
	fattr->cf_atime = cifs_NTtimeToUnix(info->LastAccessTime);
	fattr->cf_ctime = cifs_NTtimeToUnix(info->ChangeTime);
	fattr->cf_mtime = cifs_NTtimeToUnix(info->LastWriteTime);

	cifs_fill_common_info(fattr, cifs_sb);
}

static void
cifs_std_info_to_fattr(struct cifs_fattr *fattr, FIND_FILE_STANDARD_INFO *info,
		       struct cifs_sb_info *cifs_sb)
{
	int offset = cifs_sb_master_tcon(cifs_sb)->ses->server->timeAdj;

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_atime = cnvrtDosUnixTm(info->LastAccessDate,
					    info->LastAccessTime, offset);
	fattr->cf_ctime = cnvrtDosUnixTm(info->LastWriteDate,
					    info->LastWriteTime, offset);
	fattr->cf_mtime = cnvrtDosUnixTm(info->LastWriteDate,
					    info->LastWriteTime, offset);

	fattr->cf_cifsattrs = le16_to_cpu(info->Attributes);
	fattr->cf_bytes = le32_to_cpu(info->AllocationSize);
	fattr->cf_eof = le32_to_cpu(info->DataSize);

	cifs_fill_common_info(fattr, cifs_sb);
}

/* BB eventually need to add the following helper function to
      resolve NT_STATUS_STOPPED_ON_SYMLINK return code when
      we try to do FindFirst on (NTFS) directory symlinks */
/*
int get_symlink_reparse_path(char *full_path, struct cifs_sb_info *cifs_sb,
			     unsigned int xid)
{
	__u16 fid;
	int len;
	int oplock = 0;
	int rc;
	struct cifs_tcon *ptcon = cifs_sb_tcon(cifs_sb);
	char *tmpbuffer;

	rc = CIFSSMBOpen(xid, ptcon, full_path, FILE_OPEN, GENERIC_READ,
			OPEN_REPARSE_POINT, &fid, &oplock, NULL,
			cifs_sb->local_nls,
			cifs_remap(cifs_sb);
	if (!rc) {
		tmpbuffer = kmalloc(maxpath);
		rc = CIFSSMBQueryReparseLinkInfo(xid, ptcon, full_path,
				tmpbuffer,
				maxpath -1,
				fid,
				cifs_sb->local_nls);
		if (CIFSSMBClose(xid, ptcon, fid)) {
			cifs_dbg(FYI, "Error closing temporary reparsepoint open\n");
		}
	}
}
 */

static int
initiate_cifs_search(const unsigned int xid, struct file *file)
{
	__u16 search_flags;
	int rc = 0;
	char *full_path = NULL;
	struct cifsFileInfo *cifsFile;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;

	if (file->private_data == NULL) {
		tlink = cifs_sb_tlink(cifs_sb);
		if (IS_ERR(tlink))
			return PTR_ERR(tlink);

		cifsFile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
		if (cifsFile == NULL) {
			rc = -ENOMEM;
			goto error_exit;
		}
		spin_lock_init(&cifsFile->file_info_lock);
		file->private_data = cifsFile;
		cifsFile->tlink = cifs_get_tlink(tlink);
		tcon = tlink_tcon(tlink);
	} else {
		cifsFile = file->private_data;
		tcon = tlink_tcon(cifsFile->tlink);
	}

	server = tcon->ses->server;

	if (!server->ops->query_dir_first) {
		rc = -ENOSYS;
		goto error_exit;
	}

	cifsFile->invalidHandle = true;
	cifsFile->srch_inf.endOfSearch = false;

	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto error_exit;
	}

	cifs_dbg(FYI, "Full path: %s start at: %lld\n", full_path, file->f_pos);

ffirst_retry:
	/* test for Unix extensions */
	/* but now check for them on the share/mount not on the SMB session */
/*	if (pTcon->ses->capabilities & CAP_UNIX) { */
	if (pTcon->unix_ext)
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_UNIX;
	else if ((pTcon->ses->capabilities &
			(CAP_NT_SMBS | CAP_NT_FIND)) == 0) {
	/* if (cap_unix(tcon->ses) { */
	if (tcon->unix_ext)
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_UNIX;
	else if ((tcon->ses->capabilities &
		  tcon->ses->server->vals->cap_nt_find) == 0) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_INFO_STANDARD;
	} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_ID_FULL_DIR_INFO;
	} else /* not srvinos - BB fixme add check for backlevel? */ {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_DIRECTORY_INFO;
	}

	rc = CIFSFindFirst(xid, pTcon, full_path, cifs_sb->local_nls,
		&cifsFile->netfid, &cifsFile->srch_inf,
		cifs_sb->mnt_cifs_flags &
			CIFS_MOUNT_MAP_SPECIAL_CHR, CIFS_DIR_SEP(cifs_sb));
	if (rc == 0)
		cifsFile->invalidHandle = false;
	if ((rc == -EOPNOTSUPP) &&
	search_flags = CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME;
	if (backup_cred(cifs_sb))
		search_flags |= CIFS_SEARCH_BACKUP_SEARCH;

	rc = server->ops->query_dir_first(xid, tcon, full_path, cifs_sb,
					  &cifsFile->fid, search_flags,
					  &cifsFile->srch_inf);

	if (rc == 0)
		cifsFile->invalidHandle = false;
	/* BB add following call to handle readdir on new NTFS symlink errors
	else if STATUS_STOPPED_ON_SYMLINK
		call get_symlink_reparse_path and retry with new path */
	else if ((rc == -EOPNOTSUPP) &&
		(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		goto ffirst_retry;
	}
	kfree(full_path);
error_exit:
	kfree(full_path);
	cifs_put_tlink(tlink);
	return rc;
}

/* return length of unicode string in bytes */
static int cifs_unicode_bytelen(char *str)
{
	int len;
	__le16 *ustr = (__le16 *)str;
static int cifs_unicode_bytelen(const char *str)
{
	int len;
	const __le16 *ustr = (const __le16 *)str;

	for (len = 0; len <= PATH_MAX; len++) {
		if (ustr[len] == 0)
			return len << 1;
	}
	cFYI(1, ("Unicode string longer than PATH_MAX found"));
	cifs_dbg(FYI, "Unicode string longer than PATH_MAX found\n");
	return len << 1;
}

static char *nxt_dir_entry(char *old_entry, char *end_of_smb, int level)
{
	char *new_entry;
	FILE_DIRECTORY_INFO *pDirInfo = (FILE_DIRECTORY_INFO *)old_entry;

	if (level == SMB_FIND_FILE_INFO_STANDARD) {
		FIND_FILE_STANDARD_INFO *pfData;
		pfData = (FIND_FILE_STANDARD_INFO *)pDirInfo;

		new_entry = old_entry + sizeof(FIND_FILE_STANDARD_INFO) +
				pfData->FileNameLength;
	} else
		new_entry = old_entry + le32_to_cpu(pDirInfo->NextEntryOffset);
	cFYI(1, ("new entry %p old entry %p", new_entry, old_entry));
	/* validate that new_entry is not past end of SMB */
	if (new_entry >= end_of_smb) {
		cERROR(1,
		      ("search entry %p began after end of SMB %p old entry %p",
			new_entry, end_of_smb, old_entry));
	} else {
		u32 next_offset = le32_to_cpu(pDirInfo->NextEntryOffset);

		if (old_entry + next_offset < old_entry) {
			cifs_dbg(VFS, "invalid offset %u\n", next_offset);
			return NULL;
		}
		new_entry = old_entry + next_offset;
	}
	cifs_dbg(FYI, "new entry %p old entry %p\n", new_entry, old_entry);
	/* validate that new_entry is not past end of SMB */
	if (new_entry >= end_of_smb) {
		cifs_dbg(VFS, "search entry %p began after end of SMB %p old entry %p\n",
			 new_entry, end_of_smb, old_entry);
		return NULL;
	} else if (((level == SMB_FIND_FILE_INFO_STANDARD) &&
		    (new_entry + sizeof(FIND_FILE_STANDARD_INFO) > end_of_smb))
		  || ((level != SMB_FIND_FILE_INFO_STANDARD) &&
		   (new_entry + sizeof(FILE_DIRECTORY_INFO) > end_of_smb)))  {
		cERROR(1, ("search entry %p extends after end of SMB %p",
			new_entry, end_of_smb));
		cifs_dbg(VFS, "search entry %p extends after end of SMB %p\n",
			 new_entry, end_of_smb);
		return NULL;
	} else
		return new_entry;

}

#define UNICODE_DOT cpu_to_le16(0x2e)

/* return 0 if no match and 1 for . (current directory) and 2 for .. (parent) */
static int cifs_entry_is_dot(char *current_entry, struct cifsFileInfo *cfile)
{
	int rc = 0;
	char *filename = NULL;
	int len = 0;

	if (cfile->srch_inf.info_level == SMB_FIND_FILE_UNIX) {
		FILE_UNIX_INFO *pFindData = (FILE_UNIX_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		if (cfile->srch_inf.unicode) {
			len = cifs_unicode_bytelen(filename);
		} else {
			/* BB should we make this strnlen of PATH_MAX? */
			len = strnlen(filename, 5);
		}
	} else if (cfile->srch_inf.info_level == SMB_FIND_FILE_DIRECTORY_INFO) {
		FILE_DIRECTORY_INFO *pFindData =
			(FILE_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (cfile->srch_inf.info_level ==
			SMB_FIND_FILE_FULL_DIRECTORY_INFO) {
		FILE_FULL_DIRECTORY_INFO *pFindData =
			(FILE_FULL_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (cfile->srch_inf.info_level ==
			SMB_FIND_FILE_ID_FULL_DIR_INFO) {
		SEARCH_ID_FULL_DIR_INFO *pFindData =
			(SEARCH_ID_FULL_DIR_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (cfile->srch_inf.info_level ==
			SMB_FIND_FILE_BOTH_DIRECTORY_INFO) {
		FILE_BOTH_DIRECTORY_INFO *pFindData =
			(FILE_BOTH_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (cfile->srch_inf.info_level == SMB_FIND_FILE_INFO_STANDARD) {
		FIND_FILE_STANDARD_INFO *pFindData =
			(FIND_FILE_STANDARD_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = pFindData->FileNameLength;
	} else {
		cFYI(1, ("Unknown findfirst level %d",
			 cfile->srch_inf.info_level));
	}

	if (filename) {
		if (cfile->srch_inf.unicode) {
			__le16 *ufilename = (__le16 *)filename;
			if (len == 2) {
				/* check for . */
				if (ufilename[0] == UNICODE_DOT)
					rc = 1;
			} else if (len == 4) {
				/* check for .. */
				if ((ufilename[0] == UNICODE_DOT)
				   && (ufilename[1] == UNICODE_DOT))
					rc = 2;
			}
		} else /* ASCII */ {
			if (len == 1) {
				if (filename[0] == '.')
					rc = 1;
			} else if (len == 2) {
				if ((filename[0] == '.') && (filename[1] == '.'))
					rc = 2;
			}
struct cifs_dirent {
	const char	*name;
	size_t		namelen;
	u32		resume_key;
	u64		ino;
};

static void cifs_fill_dirent_unix(struct cifs_dirent *de,
		const FILE_UNIX_INFO *info, bool is_unicode)
{
	de->name = &info->FileName[0];
	if (is_unicode)
		de->namelen = cifs_unicode_bytelen(de->name);
	else
		de->namelen = strnlen(de->name, PATH_MAX);
	de->resume_key = info->ResumeKey;
	de->ino = le64_to_cpu(info->basic.UniqueId);
}

static void cifs_fill_dirent_dir(struct cifs_dirent *de,
		const FILE_DIRECTORY_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
}

static void cifs_fill_dirent_full(struct cifs_dirent *de,
		const FILE_FULL_DIRECTORY_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
}

static void cifs_fill_dirent_search(struct cifs_dirent *de,
		const SEARCH_ID_FULL_DIR_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
	de->ino = le64_to_cpu(info->UniqueId);
}

static void cifs_fill_dirent_both(struct cifs_dirent *de,
		const FILE_BOTH_DIRECTORY_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
}

static void cifs_fill_dirent_std(struct cifs_dirent *de,
		const FIND_FILE_STANDARD_INFO *info)
{
	de->name = &info->FileName[0];
	/* one byte length, no endianess conversion */
	de->namelen = info->FileNameLength;
	de->resume_key = info->ResumeKey;
}

static int cifs_fill_dirent(struct cifs_dirent *de, const void *info,
		u16 level, bool is_unicode)
{
	memset(de, 0, sizeof(*de));

	switch (level) {
	case SMB_FIND_FILE_UNIX:
		cifs_fill_dirent_unix(de, info, is_unicode);
		break;
	case SMB_FIND_FILE_DIRECTORY_INFO:
		cifs_fill_dirent_dir(de, info);
		break;
	case SMB_FIND_FILE_FULL_DIRECTORY_INFO:
		cifs_fill_dirent_full(de, info);
		break;
	case SMB_FIND_FILE_ID_FULL_DIR_INFO:
		cifs_fill_dirent_search(de, info);
		break;
	case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
		cifs_fill_dirent_both(de, info);
		break;
	case SMB_FIND_FILE_INFO_STANDARD:
		cifs_fill_dirent_std(de, info);
		break;
	default:
		cifs_dbg(FYI, "Unknown findfirst level %d\n", level);
		return -EINVAL;
	}

	return 0;
}

#define UNICODE_DOT cpu_to_le16(0x2e)

/* return 0 if no match and 1 for . (current directory) and 2 for .. (parent) */
static int cifs_entry_is_dot(struct cifs_dirent *de, bool is_unicode)
{
	int rc = 0;

	if (!de->name)
		return 0;

	if (is_unicode) {
		__le16 *ufilename = (__le16 *)de->name;
		if (de->namelen == 2) {
			/* check for . */
			if (ufilename[0] == UNICODE_DOT)
				rc = 1;
		} else if (de->namelen == 4) {
			/* check for .. */
			if (ufilename[0] == UNICODE_DOT &&
			    ufilename[1] == UNICODE_DOT)
				rc = 2;
		}
	} else /* ASCII */ {
		if (de->namelen == 1) {
			if (de->name[0] == '.')
				rc = 1;
		} else if (de->namelen == 2) {
			if (de->name[0] == '.' && de->name[1] == '.')
				rc = 2;
		}
	}

	return rc;
}

/* Check if directory that we are searching has changed so we can decide
   whether we can use the cached search results from the previous search */
static int is_dir_changed(struct file *file)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct inode *inode = file_inode(file);
	struct cifsInodeInfo *cifsInfo = CIFS_I(inode);

	if (cifsInfo->time == 0)
		return 1; /* directory was changed, perhaps due to unlink */
	else
		return 0;

}

/* find the corresponding entry in the search */
/* Note that the SMB server returns search entries for . and .. which
   complicates logic here if we choose to parse for them and we do not
   assume that they are located in the findfirst return buffer.*/
/* We start counting in the buffer with entry 2 and increment for every
   entry (do not increment for . or .. entry) */
static int find_cifs_entry(const int xid, struct cifsTconInfo *pTcon,
	struct file *file, char **ppCurrentEntry, int *num_to_ret)
{
	int rc = 0;
	int pos_in_buf = 0;
	loff_t first_entry_in_buffer;
	loff_t index_to_find = file->f_pos;
	struct cifsFileInfo *cifsFile = file->private_data;
	/* check if index in the buffer */

	if ((cifsFile == NULL) || (ppCurrentEntry == NULL) ||
	   (num_to_ret == NULL))
		return -ENOENT;

	*ppCurrentEntry = NULL;
	first_entry_in_buffer =
		cifsFile->srch_inf.index_of_last_entry -
			cifsFile->srch_inf.entries_in_buffer;

	/* if first entry in buf is zero then is first buffer
	in search response data which means it is likely . and ..
	will be in this buffer, although some servers do not return
	. and .. for the root of a drive and for those we need
	to start two entries earlier */

	dump_cifs_file_struct(file, "In fce ");
	if (((index_to_find < cifsFile->srch_inf.index_of_last_entry) &&
	     is_dir_changed(file)) ||
	   (index_to_find < first_entry_in_buffer)) {
		/* close and restart search */
		cFYI(1, ("search backing up - close and restart search"));
		if (!cifsFile->srch_inf.endOfSearch &&
		    !cifsFile->invalidHandle) {
			cifsFile->invalidHandle = true;
			CIFSFindClose(xid, pTcon, cifsFile->netfid);
		}
		if (cifsFile->srch_inf.ntwrk_buf_start) {
			cFYI(1, ("freeing SMB ff cache buf on search rewind"));
			if (cifsFile->srch_inf.smallBuf)
				cifs_small_buf_release(cifsFile->srch_inf.
						ntwrk_buf_start);
			else
				cifs_buf_release(cifsFile->srch_inf.
						ntwrk_buf_start);
			cifsFile->srch_inf.ntwrk_buf_start = NULL;
		}
		rc = initiate_cifs_search(xid, file);
		if (rc) {
			cFYI(1, ("error %d reinitiating a search on rewind",
				 rc));
			return rc;
		}
	}

	while ((index_to_find >= cifsFile->srch_inf.index_of_last_entry) &&
	      (rc == 0) && !cifsFile->srch_inf.endOfSearch) {
		cFYI(1, ("calling findnext2"));
		rc = CIFSFindNext(xid, pTcon, cifsFile->netfid,
				  &cifsFile->srch_inf);
		if (rc)
			return -ENOENT;
	}
	if (index_to_find < cifsFile->srch_inf.index_of_last_entry) {
		/* we found the buffer that contains the entry */
		/* scan and find it */
		int i;
		char *current_entry;
		char *end_of_smb = cifsFile->srch_inf.ntwrk_buf_start +
			smbCalcSize((struct smb_hdr *)
				cifsFile->srch_inf.ntwrk_buf_start);

		current_entry = cifsFile->srch_inf.srch_entries_start;
		first_entry_in_buffer = cifsFile->srch_inf.index_of_last_entry
					- cifsFile->srch_inf.entries_in_buffer;
		pos_in_buf = index_to_find - first_entry_in_buffer;
		cFYI(1, ("found entry - pos_in_buf %d", pos_in_buf));

		for (i = 0; (i < (pos_in_buf)) && (current_entry != NULL); i++) {
			/* go entry by entry figuring out which is first */
			current_entry = nxt_dir_entry(current_entry, end_of_smb,
						cifsFile->srch_inf.info_level);
		}
		if ((current_entry == NULL) && (i < pos_in_buf)) {
			/* BB fixme - check if we should flag this error */
			cERROR(1, ("reached end of buf searching for pos in buf"
			  " %d index to find %lld rc %d",
			  pos_in_buf, index_to_find, rc));
		}
		rc = 0;
		*ppCurrentEntry = current_entry;
	} else {
		cFYI(1, ("index not in buffer - could not findnext into it"));
		return 0;
	}

	if (pos_in_buf >= cifsFile->srch_inf.entries_in_buffer) {
		cFYI(1, ("can not return entries pos_in_buf beyond last"));
		*num_to_ret = 0;
	} else
		*num_to_ret = cifsFile->srch_inf.entries_in_buffer - pos_in_buf;
static int cifs_save_resume_key(const char *current_entry,
	struct cifsFileInfo *file_info)
{
	struct cifs_dirent de;
	int rc;

	rc = cifs_fill_dirent(&de, current_entry, file_info->srch_inf.info_level,
			      file_info->srch_inf.unicode);
	if (!rc) {
		file_info->srch_inf.presume_name = de.name;
		file_info->srch_inf.resume_name_len = de.namelen;
		file_info->srch_inf.resume_key = de.resume_key;
	}
	return rc;
}

/*
 * Find the corresponding entry in the search. Note that the SMB server returns
 * search entries for . and .. which complicates logic here if we choose to
 * parse for them and we do not assume that they are located in the findfirst
 * return buffer. We start counting in the buffer with entry 2 and increment for
 * every entry (do not increment for . or .. entry).
 */
static int
find_cifs_entry(const unsigned int xid, struct cifs_tcon *tcon, loff_t pos,
		struct file *file, char **current_entry, int *num_to_ret)
{
	__u16 search_flags;
	int rc = 0;
	int pos_in_buf = 0;
	loff_t first_entry_in_buffer;
	loff_t index_to_find = pos;
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct TCP_Server_Info *server = tcon->ses->server;
	/* check if index in the buffer */

	if (!server->ops->query_dir_first || !server->ops->query_dir_next)
		return -ENOSYS;

	if ((cfile == NULL) || (current_entry == NULL) || (num_to_ret == NULL))
		return -ENOENT;

	*current_entry = NULL;
	first_entry_in_buffer = cfile->srch_inf.index_of_last_entry -
					cfile->srch_inf.entries_in_buffer;

	/*
	 * If first entry in buf is zero then is first buffer
	 * in search response data which means it is likely . and ..
	 * will be in this buffer, although some servers do not return
	 * . and .. for the root of a drive and for those we need
	 * to start two entries earlier.
	 */

	dump_cifs_file_struct(file, "In fce ");
	if (((index_to_find < cfile->srch_inf.index_of_last_entry) &&
	     is_dir_changed(file)) || (index_to_find < first_entry_in_buffer)) {
		/* close and restart search */
		cifs_dbg(FYI, "search backing up - close and restart search\n");
		spin_lock(&cfile->file_info_lock);
		if (server->ops->dir_needs_close(cfile)) {
			cfile->invalidHandle = true;
			spin_unlock(&cfile->file_info_lock);
			if (server->ops->close_dir)
				server->ops->close_dir(xid, tcon, &cfile->fid);
		} else
			spin_unlock(&cfile->file_info_lock);
		if (cfile->srch_inf.ntwrk_buf_start) {
			cifs_dbg(FYI, "freeing SMB ff cache buf on search rewind\n");
			if (cfile->srch_inf.smallBuf)
				cifs_small_buf_release(cfile->srch_inf.
						ntwrk_buf_start);
			else
				cifs_buf_release(cfile->srch_inf.
						ntwrk_buf_start);
			cfile->srch_inf.ntwrk_buf_start = NULL;
		}
		rc = initiate_cifs_search(xid, file);
		if (rc) {
			cifs_dbg(FYI, "error %d reinitiating a search on rewind\n",
				 rc);
			return rc;
		}
		/* FindFirst/Next set last_entry to NULL on malformed reply */
		if (cfile->srch_inf.last_entry)
			cifs_save_resume_key(cfile->srch_inf.last_entry, cfile);
	}

	search_flags = CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME;
	if (backup_cred(cifs_sb))
		search_flags |= CIFS_SEARCH_BACKUP_SEARCH;

	while ((index_to_find >= cfile->srch_inf.index_of_last_entry) &&
	       (rc == 0) && !cfile->srch_inf.endOfSearch) {
		cifs_dbg(FYI, "calling findnext2\n");
		rc = server->ops->query_dir_next(xid, tcon, &cfile->fid,
						 search_flags,
						 &cfile->srch_inf);
		/* FindFirst/Next set last_entry to NULL on malformed reply */
		if (cfile->srch_inf.last_entry)
			cifs_save_resume_key(cfile->srch_inf.last_entry, cfile);
		if (rc)
			return -ENOENT;
	}
	if (index_to_find < cfile->srch_inf.index_of_last_entry) {
		/* we found the buffer that contains the entry */
		/* scan and find it */
		int i;
		char *cur_ent;
		char *end_of_smb;

		if (cfile->srch_inf.ntwrk_buf_start == NULL) {
			cifs_dbg(VFS, "ntwrk_buf_start is NULL during readdir\n");
			return -EIO;
		}

		end_of_smb = cfile->srch_inf.ntwrk_buf_start +
			server->ops->calc_smb_size(
					cfile->srch_inf.ntwrk_buf_start);

		cur_ent = cfile->srch_inf.srch_entries_start;
		first_entry_in_buffer = cfile->srch_inf.index_of_last_entry
					- cfile->srch_inf.entries_in_buffer;
		pos_in_buf = index_to_find - first_entry_in_buffer;
		cifs_dbg(FYI, "found entry - pos_in_buf %d\n", pos_in_buf);

		for (i = 0; (i < (pos_in_buf)) && (cur_ent != NULL); i++) {
			/* go entry by entry figuring out which is first */
			cur_ent = nxt_dir_entry(cur_ent, end_of_smb,
						cfile->srch_inf.info_level);
		}
		if ((cur_ent == NULL) && (i < pos_in_buf)) {
			/* BB fixme - check if we should flag this error */
			cifs_dbg(VFS, "reached end of buf searching for pos in buf %d index to find %lld rc %d\n",
				 pos_in_buf, index_to_find, rc);
		}
		rc = 0;
		*current_entry = cur_ent;
	} else {
		cifs_dbg(FYI, "index not in buffer - could not findnext into it\n");
		return 0;
	}

	if (pos_in_buf >= cfile->srch_inf.entries_in_buffer) {
		cifs_dbg(FYI, "can not return entries pos_in_buf beyond last\n");
		*num_to_ret = 0;
	} else
		*num_to_ret = cfile->srch_inf.entries_in_buffer - pos_in_buf;

	return rc;
}

/* inode num, inode type and filename returned */
static int cifs_get_name_from_search_buf(struct qstr *pqst,
	char *current_entry, __u16 level, unsigned int unicode,
	struct cifs_sb_info *cifs_sb, int max_len, ino_t *pinum)
{
	int rc = 0;
	unsigned int len = 0;
	char *filename;
	struct nls_table *nlt = cifs_sb->local_nls;

	*pinum = 0;

	if (level == SMB_FIND_FILE_UNIX) {
		FILE_UNIX_INFO *pFindData = (FILE_UNIX_INFO *)current_entry;

		filename = &pFindData->FileName[0];
		if (unicode) {
			len = cifs_unicode_bytelen(filename);
		} else {
			/* BB should we make this strnlen of PATH_MAX? */
			len = strnlen(filename, PATH_MAX);
		}

		/* BB fixme - hash low and high 32 bits if not 64 bit arch BB */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)
			*pinum = pFindData->UniqueId;
	} else if (level == SMB_FIND_FILE_DIRECTORY_INFO) {
		FILE_DIRECTORY_INFO *pFindData =
			(FILE_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (level == SMB_FIND_FILE_FULL_DIRECTORY_INFO) {
		FILE_FULL_DIRECTORY_INFO *pFindData =
			(FILE_FULL_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (level == SMB_FIND_FILE_ID_FULL_DIR_INFO) {
		SEARCH_ID_FULL_DIR_INFO *pFindData =
			(SEARCH_ID_FULL_DIR_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		*pinum = pFindData->UniqueId;
	} else if (level == SMB_FIND_FILE_BOTH_DIRECTORY_INFO) {
		FILE_BOTH_DIRECTORY_INFO *pFindData =
			(FILE_BOTH_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if (level == SMB_FIND_FILE_INFO_STANDARD) {
		FIND_FILE_STANDARD_INFO *pFindData =
			(FIND_FILE_STANDARD_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		/* one byte length, no name conversion */
		len = (unsigned int)pFindData->FileNameLength;
	} else {
		cFYI(1, ("Unknown findfirst level %d", level));
		return -EINVAL;
	}

	if (len > max_len) {
		cERROR(1, ("bad search response length %d past smb end", len));
		return -EINVAL;
	}

	if (unicode) {
		/* BB fixme - test with long names */
		/* Note converted filename can be longer than in unicode */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR)
			pqst->len = cifs_convertUCSpath((char *)pqst->name,
					(__le16 *)filename, len/2, nlt);
		else
			pqst->len = cifs_strfromUCS_le((char *)pqst->name,
					(__le16 *)filename, len/2, nlt);
	} else {
		pqst->name = filename;
		pqst->len = len;
	}
	pqst->hash = full_name_hash(pqst->name, pqst->len);
/*	cFYI(1, ("filldir on %s",pqst->name));  */
	return rc;
}

static int cifs_filldir(char *pfindEntry, struct file *file,
	filldir_t filldir, void *direntry, char *scratch_buf, int max_len)
{
	int rc = 0;
	struct qstr qstring;
	struct cifsFileInfo *pCifsF;
	unsigned int obj_type;
	ino_t  inum;
	struct cifs_sb_info *cifs_sb;
	struct inode *tmp_inode;
	struct dentry *tmp_dentry;

	/* get filename and len into qstring */
	/* get dentry */
	/* decide whether to create and populate ionde */
	if ((direntry == NULL) || (file == NULL))
		return -EINVAL;

	pCifsF = file->private_data;

	if ((scratch_buf == NULL) || (pfindEntry == NULL) || (pCifsF == NULL))
		return -ENOENT;

	rc = cifs_entry_is_dot(pfindEntry, pCifsF);
	/* skip . and .. since we added them first */
	if (rc != 0)
		return 0;

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	qstring.name = scratch_buf;
	rc = cifs_get_name_from_search_buf(&qstring, pfindEntry,
			pCifsF->srch_inf.info_level,
			pCifsF->srch_inf.unicode, cifs_sb,
			max_len,
			&inum /* returned */);

	if (rc)
		return rc;

	rc = construct_dentry(&qstring, file, &tmp_inode, &tmp_dentry);
	if ((tmp_inode == NULL) || (tmp_dentry == NULL))
		return -ENOMEM;

	if (rc) {
		/* inode created, we need to hash it with right inode number */
		if (inum != 0) {
			/* BB fixme - hash the 2 32 quantities bits together if
			 *  necessary BB */
			tmp_inode->i_ino = inum;
		}
		insert_inode_hash(tmp_inode);
	}

	/* we pass in rc below, indicating whether it is a new inode,
	   so we can figure out whether to invalidate the inode cached
	   data if the file has changed */
	if (pCifsF->srch_inf.info_level == SMB_FIND_FILE_UNIX)
		unix_fill_in_inode(tmp_inode,
				   (FILE_UNIX_INFO *)pfindEntry,
				   &obj_type, rc);
	else if (pCifsF->srch_inf.info_level == SMB_FIND_FILE_INFO_STANDARD)
		fill_in_inode(tmp_inode, 0 /* old level 1 buffer type */,
				pfindEntry, &obj_type, rc);
	else
		fill_in_inode(tmp_inode, 1 /* NT */, pfindEntry, &obj_type, rc);

	if (rc) /* new inode - needs to be tied to dentry */ {
		d_instantiate(tmp_dentry, tmp_inode);
		if (rc == 2)
			d_rehash(tmp_dentry);
	}


	rc = filldir(direntry, qstring.name, qstring.len, file->f_pos,
		     tmp_inode->i_ino, obj_type);
	if (rc) {
		cFYI(1, ("filldir rc = %d", rc));
		/* we can not return filldir errors to the caller
		since they are "normal" when the stat blocksize
		is too small - we return remapped error instead */
		rc = -EOVERFLOW;
	}

	dput(tmp_dentry);
	return rc;
}

static int cifs_save_resume_key(const char *current_entry,
	struct cifsFileInfo *cifsFile)
{
	int rc = 0;
	unsigned int len = 0;
	__u16 level;
	char *filename;

	if ((cifsFile == NULL) || (current_entry == NULL))
		return -EINVAL;

	level = cifsFile->srch_inf.info_level;

	if (level == SMB_FIND_FILE_UNIX) {
		FILE_UNIX_INFO *pFindData = (FILE_UNIX_INFO *)current_entry;

		filename = &pFindData->FileName[0];
		if (cifsFile->srch_inf.unicode) {
			len = cifs_unicode_bytelen(filename);
		} else {
			/* BB should we make this strnlen of PATH_MAX? */
			len = strnlen(filename, PATH_MAX);
		}
		cifsFile->srch_inf.resume_key = pFindData->ResumeKey;
	} else if (level == SMB_FIND_FILE_DIRECTORY_INFO) {
		FILE_DIRECTORY_INFO *pFindData =
			(FILE_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if (level == SMB_FIND_FILE_FULL_DIRECTORY_INFO) {
		FILE_FULL_DIRECTORY_INFO *pFindData =
			(FILE_FULL_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if (level == SMB_FIND_FILE_ID_FULL_DIR_INFO) {
		SEARCH_ID_FULL_DIR_INFO *pFindData =
			(SEARCH_ID_FULL_DIR_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if (level == SMB_FIND_FILE_BOTH_DIRECTORY_INFO) {
		FILE_BOTH_DIRECTORY_INFO *pFindData =
			(FILE_BOTH_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if (level == SMB_FIND_FILE_INFO_STANDARD) {
		FIND_FILE_STANDARD_INFO *pFindData =
			(FIND_FILE_STANDARD_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		/* one byte length, no name conversion */
		len = (unsigned int)pFindData->FileNameLength;
		cifsFile->srch_inf.resume_key = pFindData->ResumeKey;
	} else {
		cFYI(1, ("Unknown findfirst level %d", level));
		return -EINVAL;
	}
	cifsFile->srch_inf.resume_name_len = len;
	cifsFile->srch_inf.presume_name = filename;
	return rc;
}

int cifs_readdir(struct file *file, void *direntry, filldir_t filldir)
{
	int rc = 0;
	int xid, i;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
static int cifs_filldir(char *find_entry, struct file *file,
		struct dir_context *ctx,
		char *scratch_buf, unsigned int max_len)
{
	struct cifsFileInfo *file_info = file->private_data;
	struct super_block *sb = file_inode(file)->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_dirent de = { NULL, };
	struct cifs_fattr fattr;
	struct qstr name;
	int rc = 0;
	ino_t ino;

	rc = cifs_fill_dirent(&de, find_entry, file_info->srch_inf.info_level,
			      file_info->srch_inf.unicode);
	if (rc)
		return rc;

	if (de.namelen > max_len) {
		cifs_dbg(VFS, "bad search response length %zd past smb end\n",
			 de.namelen);
		return -EINVAL;
	}

	/* skip . and .. since we added them first */
	if (cifs_entry_is_dot(&de, file_info->srch_inf.unicode))
		return 0;

	if (file_info->srch_inf.unicode) {
		struct nls_table *nlt = cifs_sb->local_nls;
		int map_type;

		map_type = cifs_remap(cifs_sb);
		name.name = scratch_buf;
		name.len =
			cifs_from_utf16((char *)name.name, (__le16 *)de.name,
					UNICODE_NAME_MAX,
					min_t(size_t, de.namelen,
					      (size_t)max_len), nlt, map_type);
		name.len -= nls_nullsize(nlt);
	} else {
		name.name = de.name;
		name.len = de.namelen;
	}

	switch (file_info->srch_inf.info_level) {
	case SMB_FIND_FILE_UNIX:
		cifs_unix_basic_to_fattr(&fattr,
					 &((FILE_UNIX_INFO *)find_entry)->basic,
					 cifs_sb);
		break;
	case SMB_FIND_FILE_INFO_STANDARD:
		cifs_std_info_to_fattr(&fattr,
				       (FIND_FILE_STANDARD_INFO *)find_entry,
				       cifs_sb);
		break;
	default:
		cifs_dir_info_to_fattr(&fattr,
				       (FILE_DIRECTORY_INFO *)find_entry,
				       cifs_sb);
		break;
	}

	if (de.ino && (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		fattr.cf_uniqueid = de.ino;
	} else {
		fattr.cf_uniqueid = iunique(sb, ROOT_I);
		cifs_autodisable_serverino(cifs_sb);
	}

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS) &&
	    couldbe_mf_symlink(&fattr))
		/*
		 * trying to get the type and mode can be slow,
		 * so just call those regular files for now, and mark
		 * for reval
		 */
		fattr.cf_flags |= CIFS_FATTR_NEED_REVAL;

	cifs_prime_dcache(file->f_path.dentry, &name, &fattr);

	ino = cifs_uniqueid_to_ino_t(fattr.cf_uniqueid);
	return !dir_emit(ctx, name.name, name.len, ino, fattr.cf_dtype);
}


int cifs_readdir(struct file *file, struct dir_context *ctx)
{
	int rc = 0;
	unsigned int xid;
	int i;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *cifsFile = NULL;
	char *current_entry;
	int num_to_fill = 0;
	char *tmp_buf = NULL;
	char *end_of_smb;
	int max_len;

	xid = GetXid();

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;
	if (pTcon == NULL)
		return -EINVAL;

	switch ((int) file->f_pos) {
	case 0:
		if (filldir(direntry, ".", 1, file->f_pos,
		     file->f_path.dentry->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for current dir failed"));
			rc = -ENOMEM;
			break;
		}
		file->f_pos++;
	case 1:
		if (filldir(direntry, "..", 2, file->f_pos,
		     file->f_path.dentry->d_parent->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for parent dir failed"));
			rc = -ENOMEM;
			break;
		}
		file->f_pos++;
	default:
		/* 1) If search is active,
			is in current search buffer?
			if it before then restart search
			if after then keep searching till find it */

		if (file->private_data == NULL) {
			rc = initiate_cifs_search(xid, file);
			cFYI(1, ("initiate cifs search rc %d", rc));
			if (rc) {
				FreeXid(xid);
				return rc;
			}
		}
		if (file->private_data == NULL) {
			rc = -EINVAL;
			FreeXid(xid);
			return rc;
		}
		cifsFile = file->private_data;
		if (cifsFile->srch_inf.endOfSearch) {
			if (cifsFile->srch_inf.emptyDir) {
				cFYI(1, ("End of search, empty dir"));
				rc = 0;
				break;
			}
		} /* else {
			cifsFile->invalidHandle = true;
			CIFSFindClose(xid, pTcon, cifsFile->netfid);
		} */

		rc = find_cifs_entry(xid, pTcon, file,
				&current_entry, &num_to_fill);
		if (rc) {
			cFYI(1, ("fce error %d", rc));
			goto rddir2_exit;
		} else if (current_entry != NULL) {
			cFYI(1, ("entry %lld found", file->f_pos));
		} else {
			cFYI(1, ("could not find entry"));
			goto rddir2_exit;
		}
		cFYI(1, ("loop through %d times filling dir for net buf %p",
			num_to_fill, cifsFile->srch_inf.ntwrk_buf_start));
		max_len = smbCalcSize((struct smb_hdr *)
				cifsFile->srch_inf.ntwrk_buf_start);
		end_of_smb = cifsFile->srch_inf.ntwrk_buf_start + max_len;

		/* To be safe - for UCS to UTF-8 with strings loaded
		with the rare long characters alloc more to account for
		such multibyte target UTF-8 characters. cifs_unicode.c,
		which actually does the conversion, has the same limit */
		tmp_buf = kmalloc((2 * NAME_MAX) + 4, GFP_KERNEL);
		for (i = 0; (i < num_to_fill) && (rc == 0); i++) {
			if (current_entry == NULL) {
				/* evaluate whether this case is an error */
				cERROR(1, ("past SMB end,  num to fill %d i %d",
					  num_to_fill, i));
				break;
			}
			/* if buggy server returns . and .. late do
			we want to check for that here? */
			rc = cifs_filldir(current_entry, file,
					filldir, direntry, tmp_buf, max_len);
			if (rc == -EOVERFLOW) {
				rc = 0;
				break;
			}

			file->f_pos++;
			if (file->f_pos ==
				cifsFile->srch_inf.index_of_last_entry) {
				cFYI(1, ("last entry in buf at pos %lld %s",
					file->f_pos, tmp_buf));
				cifs_save_resume_key(current_entry, cifsFile);
				break;
			} else
				current_entry =
					nxt_dir_entry(current_entry, end_of_smb,
						cifsFile->srch_inf.info_level);
		}
		kfree(tmp_buf);
		break;
	} /* end switch */

rddir2_exit:
	FreeXid(xid);
	unsigned int max_len;

	xid = get_xid();

	/*
	 * Ensure FindFirst doesn't fail before doing filldir() for '.' and
	 * '..'. Otherwise we won't be able to notify VFS in case of failure.
	 */
	if (file->private_data == NULL) {
		rc = initiate_cifs_search(xid, file);
		cifs_dbg(FYI, "initiate cifs search rc %d\n", rc);
		if (rc)
			goto rddir2_exit;
	}

	if (!dir_emit_dots(file, ctx))
		goto rddir2_exit;

	/* 1) If search is active,
		is in current search buffer?
		if it before then restart search
		if after then keep searching till find it */

	cifsFile = file->private_data;
	if (cifsFile->srch_inf.endOfSearch) {
		if (cifsFile->srch_inf.emptyDir) {
			cifs_dbg(FYI, "End of search, empty dir\n");
			rc = 0;
			goto rddir2_exit;
		}
	} /* else {
		cifsFile->invalidHandle = true;
		tcon->ses->server->close(xid, tcon, &cifsFile->fid);
	} */

	tcon = tlink_tcon(cifsFile->tlink);
	rc = find_cifs_entry(xid, tcon, ctx->pos, file, &current_entry,
			     &num_to_fill);
	if (rc) {
		cifs_dbg(FYI, "fce error %d\n", rc);
		goto rddir2_exit;
	} else if (current_entry != NULL) {
		cifs_dbg(FYI, "entry %lld found\n", ctx->pos);
	} else {
		cifs_dbg(FYI, "could not find entry\n");
		goto rddir2_exit;
	}
	cifs_dbg(FYI, "loop through %d times filling dir for net buf %p\n",
		 num_to_fill, cifsFile->srch_inf.ntwrk_buf_start);
	max_len = tcon->ses->server->ops->calc_smb_size(
			cifsFile->srch_inf.ntwrk_buf_start);
	end_of_smb = cifsFile->srch_inf.ntwrk_buf_start + max_len;

	tmp_buf = kmalloc(UNICODE_NAME_MAX, GFP_KERNEL);
	if (tmp_buf == NULL) {
		rc = -ENOMEM;
		goto rddir2_exit;
	}

	for (i = 0; i < num_to_fill; i++) {
		if (current_entry == NULL) {
			/* evaluate whether this case is an error */
			cifs_dbg(VFS, "past SMB end,  num to fill %d i %d\n",
				 num_to_fill, i);
			break;
		}
		/*
		 * if buggy server returns . and .. late do we want to
		 * check for that here?
		 */
		*tmp_buf = 0;
		rc = cifs_filldir(current_entry, file, ctx,
				  tmp_buf, max_len);
		if (rc) {
			if (rc > 0)
				rc = 0;
			break;
		}

		ctx->pos++;
		if (ctx->pos ==
			cifsFile->srch_inf.index_of_last_entry) {
			cifs_dbg(FYI, "last entry in buf at pos %lld %s\n",
				 ctx->pos, tmp_buf);
			cifs_save_resume_key(current_entry, cifsFile);
			break;
		} else
			current_entry =
				nxt_dir_entry(current_entry, end_of_smb,
					cifsFile->srch_inf.info_level);
	}
	kfree(tmp_buf);

rddir2_exit:
	free_xid(xid);
	return rc;
}
