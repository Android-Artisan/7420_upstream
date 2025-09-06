#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/reiserfs_fs.h>
#include <linux/errno.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/posix_acl_xattr.h>
#include <linux/reiserfs_xattr.h>
#include <linux/reiserfs_acl.h>
#include <asm/uaccess.h>

static int reiserfs_set_acl(struct inode *inode, int type,
			    struct posix_acl *acl);

static int
xattr_set_acl(struct inode *inode, int type, const void *value, size_t size)
{
	struct posix_acl *acl;
	int error;

	if (!reiserfs_posixacl(inode->i_sb))
		return -EOPNOTSUPP;
	if (!is_owner_or_cap(inode))
		return -EPERM;

	if (value) {
		acl = posix_acl_from_xattr(value, size);
		if (IS_ERR(acl)) {
			return PTR_ERR(acl);
		} else if (acl) {
			error = posix_acl_valid(acl);
			if (error)
				goto release_and_out;
		}
	} else
		acl = NULL;

	error = reiserfs_set_acl(inode, type, acl);

      release_and_out:
	posix_acl_release(acl);
	return error;
}

static int
xattr_get_acl(struct inode *inode, int type, void *buffer, size_t size)
{
	struct posix_acl *acl;
	int error;

	if (!reiserfs_posixacl(inode->i_sb))
		return -EOPNOTSUPP;

	acl = reiserfs_get_acl(inode, type);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;
	error = posix_acl_to_xattr(acl, buffer, size);
	posix_acl_release(acl);
#include "reiserfs.h"
#include <linux/errno.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/slab.h>
#include <linux/posix_acl_xattr.h>
#include "xattr.h"
#include "acl.h"
#include <linux/uaccess.h>

static int __reiserfs_set_acl(struct reiserfs_transaction_handle *th,
			    struct inode *inode, int type,
			    struct posix_acl *acl);


int
reiserfs_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int error, error2;
	struct reiserfs_transaction_handle th;
	size_t jcreate_blocks;
	int size = acl ? posix_acl_xattr_size(acl->a_count) : 0;


	/*
	 * Pessimism: We can't assume that anything from the xattr root up
	 * has been created.
	 */

	jcreate_blocks = reiserfs_xattr_jcreate_nblocks(inode) +
			 reiserfs_xattr_nblocks(inode, size) * 2;

	reiserfs_write_lock(inode->i_sb);
	error = journal_begin(&th, inode->i_sb, jcreate_blocks);
	reiserfs_write_unlock(inode->i_sb);
	if (error == 0) {
		if (type == ACL_TYPE_ACCESS && acl) {
			error = posix_acl_update_mode(inode, &inode->i_mode,
						      &acl);
			if (error)
				goto unlock;
		}
		error = __reiserfs_set_acl(&th, inode, type, acl);
unlock:
		reiserfs_write_lock(inode->i_sb);
		error2 = journal_end(&th);
		reiserfs_write_unlock(inode->i_sb);
		if (error2)
			error = error2;
	}

	return error;
}

/*
 * Convert from filesystem to in-memory representation.
 */
static struct posix_acl *posix_acl_from_disk(const void *value, size_t size)
static struct posix_acl *reiserfs_posix_acl_from_disk(const void *value, size_t size)
{
	const char *end = (char *)value + size;
	int n, count;
	struct posix_acl *acl;

	if (!value)
		return NULL;
	if (size < sizeof(reiserfs_acl_header))
		return ERR_PTR(-EINVAL);
	if (((reiserfs_acl_header *) value)->a_version !=
	    cpu_to_le32(REISERFS_ACL_VERSION))
		return ERR_PTR(-EINVAL);
	value = (char *)value + sizeof(reiserfs_acl_header);
	count = reiserfs_acl_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
	if (count == 0)
		return NULL;
	acl = posix_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);
	for (n = 0; n < count; n++) {
		reiserfs_acl_entry *entry = (reiserfs_acl_entry *) value;
		if ((char *)value + sizeof(reiserfs_acl_entry_short) > end)
			goto fail;
		acl->a_entries[n].e_tag = le16_to_cpu(entry->e_tag);
		acl->a_entries[n].e_perm = le16_to_cpu(entry->e_perm);
		switch (acl->a_entries[n].e_tag) {
		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			value = (char *)value +
			    sizeof(reiserfs_acl_entry_short);
			acl->a_entries[n].e_id = ACL_UNDEFINED_ID;
			break;

		case ACL_USER:
			break;

		case ACL_USER:
			value = (char *)value + sizeof(reiserfs_acl_entry);
			if ((char *)value > end)
				goto fail;
			acl->a_entries[n].e_uid = 
				make_kuid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;
		case ACL_GROUP:
			value = (char *)value + sizeof(reiserfs_acl_entry);
			if ((char *)value > end)
				goto fail;
			acl->a_entries[n].e_id = le32_to_cpu(entry->e_id);
			acl->a_entries[n].e_gid =
				make_kgid(&init_user_ns,
					  le32_to_cpu(entry->e_id));
			break;

		default:
			goto fail;
		}
	}
	if (value != end)
		goto fail;
	return acl;

      fail:
fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Convert from in-memory to filesystem representation.
 */
static void *posix_acl_to_disk(const struct posix_acl *acl, size_t * size)
static void *reiserfs_posix_acl_to_disk(const struct posix_acl *acl, size_t * size)
{
	reiserfs_acl_header *ext_acl;
	char *e;
	int n;

	*size = reiserfs_acl_size(acl->a_count);
	ext_acl = kmalloc(sizeof(reiserfs_acl_header) +
						  acl->a_count *
						  sizeof(reiserfs_acl_entry),
						  GFP_NOFS);
	if (!ext_acl)
		return ERR_PTR(-ENOMEM);
	ext_acl->a_version = cpu_to_le32(REISERFS_ACL_VERSION);
	e = (char *)ext_acl + sizeof(reiserfs_acl_header);
	for (n = 0; n < acl->a_count; n++) {
		const struct posix_acl_entry *acl_e = &acl->a_entries[n];
		reiserfs_acl_entry *entry = (reiserfs_acl_entry *) e;
		entry->e_tag = cpu_to_le16(acl->a_entries[n].e_tag);
		entry->e_perm = cpu_to_le16(acl->a_entries[n].e_perm);
		switch (acl->a_entries[n].e_tag) {
		case ACL_USER:
		case ACL_GROUP:
			entry->e_id = cpu_to_le32(acl->a_entries[n].e_id);
			entry->e_id = cpu_to_le32(
				from_kuid(&init_user_ns, acl_e->e_uid));
			e += sizeof(reiserfs_acl_entry);
			break;
		case ACL_GROUP:
			entry->e_id = cpu_to_le32(
				from_kgid(&init_user_ns, acl_e->e_gid));
			e += sizeof(reiserfs_acl_entry);
			break;

		case ACL_USER_OBJ:
		case ACL_GROUP_OBJ:
		case ACL_MASK:
		case ACL_OTHER:
			e += sizeof(reiserfs_acl_entry_short);
			break;

		default:
			goto fail;
		}
	}
	return (char *)ext_acl;

      fail:
fail:
	kfree(ext_acl);
	return ERR_PTR(-EINVAL);
}

/*
 * Inode operation get_posix_acl().
 *
 * inode->i_mutex: down
 * BKL held [before 2.5.x]
 */
struct posix_acl *reiserfs_get_acl(struct inode *inode, int type)
{
	char *name, *value;
	struct posix_acl *acl, **p_acl;
	int size;
	int retval;
	struct reiserfs_inode_info *reiserfs_i = REISERFS_I(inode);
	struct posix_acl *acl;
	int size;
	int retval;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		p_acl = &reiserfs_i->i_acl_access;
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		p_acl = &reiserfs_i->i_acl_default;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	if (IS_ERR(*p_acl)) {
		if (PTR_ERR(*p_acl) == -ENODATA)
			return NULL;
	} else if (*p_acl != NULL)
		return posix_acl_dup(*p_acl);

	size = reiserfs_xattr_get(inode, name, NULL, 0);
	if (size < 0) {
		if (size == -ENODATA || size == -ENOSYS) {
			*p_acl = ERR_PTR(-ENODATA);
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		BUG();
	}

	size = reiserfs_xattr_get(inode, name, NULL, 0);
	if (size < 0) {
		if (size == -ENODATA || size == -ENOSYS) {
			set_cached_acl(inode, type, NULL);
			return NULL;
		}
		return ERR_PTR(size);
	}

	value = kmalloc(size, GFP_NOFS);
	if (!value)
		return ERR_PTR(-ENOMEM);

	retval = reiserfs_xattr_get(inode, name, value, size);
	if (retval == -ENODATA || retval == -ENOSYS) {
		/* This shouldn't actually happen as it should have
		   been caught above.. but just in case */
		acl = NULL;
		*p_acl = ERR_PTR(-ENODATA);
	} else if (retval < 0) {
		acl = ERR_PTR(retval);
	} else {
		acl = posix_acl_from_disk(value, retval);
		if (!IS_ERR(acl))
			*p_acl = posix_acl_dup(acl);
	}
		/*
		 * This shouldn't actually happen as it should have
		 * been caught above.. but just in case
		 */
		acl = NULL;
	} else if (retval < 0) {
		acl = ERR_PTR(retval);
	} else {
		acl = reiserfs_posix_acl_from_disk(value, retval);
	}
	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);

	kfree(value);
	return acl;
}

/*
 * Inode operation set_posix_acl().
 *
 * inode->i_mutex: down
 * BKL held [before 2.5.x]
 */
static int
reiserfs_set_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	char *name;
	void *value = NULL;
	struct posix_acl **p_acl;
	size_t size;
	int error;
	struct reiserfs_inode_info *reiserfs_i = REISERFS_I(inode);

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;
__reiserfs_set_acl(struct reiserfs_transaction_handle *th, struct inode *inode,
		 int type, struct posix_acl *acl)
{
	char *name;
	void *value = NULL;
	size_t size = 0;
	int error;

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		p_acl = &reiserfs_i->i_acl_access;
		if (acl) {
			mode_t mode = inode->i_mode;
			error = posix_acl_equiv_mode(acl, &mode);
			if (error < 0)
				return error;
			else {
				inode->i_mode = mode;
		if (acl) {
			error = posix_acl_update_mode(inode, &inode->i_mode, &acl);
			if (error)
				return error;
		}
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		p_acl = &reiserfs_i->i_acl_default;
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		break;
	default:
		return -EINVAL;
	}

	if (acl) {
		value = posix_acl_to_disk(acl, &size);
		if (IS_ERR(value))
			return (int)PTR_ERR(value);
		error = reiserfs_xattr_set(inode, name, value, size, 0);
	} else {
		error = reiserfs_xattr_del(inode, name);
		if (error == -ENODATA) {
			/* This may seem odd here, but it means that the ACL was set
			 * with a value representable with mode bits. If there was
			 * an ACL before, reiserfs_xattr_del already dirtied the inode.
			 */
			mark_inode_dirty(inode);
			error = 0;
		value = reiserfs_posix_acl_to_disk(acl, &size);
		if (IS_ERR(value))
			return (int)PTR_ERR(value);
	}

	error = reiserfs_xattr_set_handle(th, inode, name, value, size, 0);

	/*
	 * Ensure that the inode gets dirtied if we're only using
	 * the mode bits and an old ACL didn't exist. We don't need
	 * to check if the inode is hashed here since we won't get
	 * called by reiserfs_inherit_default_acl().
	 */
	if (error == -ENODATA) {
		error = 0;
		if (type == ACL_TYPE_ACCESS) {
			inode->i_ctime = CURRENT_TIME_SEC;
			mark_inode_dirty(inode);
		}
	}

	kfree(value);

	if (!error) {
		/* Release the old one */
		if (!IS_ERR(*p_acl) && *p_acl)
			posix_acl_release(*p_acl);

		if (acl == NULL)
			*p_acl = ERR_PTR(-ENODATA);
		else
			*p_acl = posix_acl_dup(acl);
	}
	if (!error)
		set_cached_acl(inode, type, acl);

	return error;
}

/* dir->i_mutex: locked,
 * inode is new and not released into the wild yet */
int
reiserfs_inherit_default_acl(struct inode *dir, struct dentry *dentry,
			     struct inode *inode)
{
	struct posix_acl *acl;
/*
 * dir->i_mutex: locked,
 * inode is new and not released into the wild yet
 */
int
reiserfs_inherit_default_acl(struct reiserfs_transaction_handle *th,
			     struct inode *dir, struct dentry *dentry,
			     struct inode *inode)
{
	struct posix_acl *default_acl, *acl;
	int err = 0;

	/* ACLs only get applied to files and directories */
	if (S_ISLNK(inode->i_mode))
		return 0;

	/* ACLs can only be used on "new" objects, so if it's an old object
	 * there is nothing to inherit from */
	if (get_inode_sd_version(dir) == STAT_DATA_V1)
		goto apply_umask;

	/* Don't apply ACLs to objects in the .reiserfs_priv tree.. This
	 * would be useless since permissions are ignored, and a pain because
	 * it introduces locking cycles */
	if (is_reiserfs_priv_object(dir)) {
		reiserfs_mark_inode_private(inode);
		goto apply_umask;
	}

	acl = reiserfs_get_acl(dir, ACL_TYPE_DEFAULT);
	if (IS_ERR(acl)) {
		if (PTR_ERR(acl) == -ENODATA)
			goto apply_umask;
		return PTR_ERR(acl);
	}

	if (acl) {
		struct posix_acl *acl_copy;
		mode_t mode = inode->i_mode;
		int need_acl;

		/* Copy the default ACL to the default ACL of a new directory */
		if (S_ISDIR(inode->i_mode)) {
			err = reiserfs_set_acl(inode, ACL_TYPE_DEFAULT, acl);
			if (err)
				goto cleanup;
		}

		/* Now we reconcile the new ACL and the mode,
		   potentially modifying both */
		acl_copy = posix_acl_clone(acl, GFP_NOFS);
		if (!acl_copy) {
			err = -ENOMEM;
			goto cleanup;
		}

		need_acl = posix_acl_create_masq(acl_copy, &mode);
		if (need_acl >= 0) {
			if (mode != inode->i_mode) {
				inode->i_mode = mode;
			}

			/* If we need an ACL.. */
			if (need_acl > 0) {
				err =
				    reiserfs_set_acl(inode, ACL_TYPE_ACCESS,
						     acl_copy);
				if (err)
					goto cleanup_copy;
			}
		}
	      cleanup_copy:
		posix_acl_release(acl_copy);
	      cleanup:
		posix_acl_release(acl);
	} else {
	      apply_umask:
		/* no ACL, apply umask */
		inode->i_mode &= ~current->fs->umask;
	}

	return err;
}

/* Looks up and caches the result of the default ACL.
 * We do this so that we don't need to carry the xattr_sem into
 * reiserfs_new_inode if we don't need to */
int reiserfs_cache_default_acl(struct inode *inode)
{
	int ret = 0;
	if (reiserfs_posixacl(inode->i_sb) && !is_reiserfs_priv_object(inode)) {
		struct posix_acl *acl;
		reiserfs_read_lock_xattr_i(inode);
		reiserfs_read_lock_xattrs(inode->i_sb);
		acl = reiserfs_get_acl(inode, ACL_TYPE_DEFAULT);
		reiserfs_read_unlock_xattrs(inode->i_sb);
		reiserfs_read_unlock_xattr_i(inode);
		ret = (acl && !IS_ERR(acl));
		if (ret)
			posix_acl_release(acl);
	}

	return ret;
}

int reiserfs_acl_chmod(struct inode *inode)
{
	struct posix_acl *acl, *clone;
	int error;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	if (get_inode_sd_version(inode) == STAT_DATA_V1 ||
	    !reiserfs_posixacl(inode->i_sb)) {
		return 0;
	}

	reiserfs_read_lock_xattrs(inode->i_sb);
	acl = reiserfs_get_acl(inode, ACL_TYPE_ACCESS);
	reiserfs_read_unlock_xattrs(inode->i_sb);
	if (!acl)
		return 0;
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	clone = posix_acl_clone(acl, GFP_NOFS);
	posix_acl_release(acl);
	if (!clone)
		return -ENOMEM;
	error = posix_acl_chmod_masq(clone, inode->i_mode);
	if (!error) {
		int lock = !has_xattr_dir(inode);
		reiserfs_write_lock_xattr_i(inode);
		if (lock)
			reiserfs_write_lock_xattrs(inode->i_sb);
		else
			reiserfs_read_lock_xattrs(inode->i_sb);
		error = reiserfs_set_acl(inode, ACL_TYPE_ACCESS, clone);
		if (lock)
			reiserfs_write_unlock_xattrs(inode->i_sb);
		else
			reiserfs_read_unlock_xattrs(inode->i_sb);
		reiserfs_write_unlock_xattr_i(inode);
	}
	posix_acl_release(clone);
	return error;
}

static int
posix_acl_access_get(struct inode *inode, const char *name,
		     void *buffer, size_t size)
{
	if (strlen(name) != sizeof(POSIX_ACL_XATTR_ACCESS) - 1)
		return -EINVAL;
	return xattr_get_acl(inode, ACL_TYPE_ACCESS, buffer, size);
}

static int
posix_acl_access_set(struct inode *inode, const char *name,
		     const void *value, size_t size, int flags)
{
	if (strlen(name) != sizeof(POSIX_ACL_XATTR_ACCESS) - 1)
		return -EINVAL;
	return xattr_set_acl(inode, ACL_TYPE_ACCESS, value, size);
}

static int posix_acl_access_del(struct inode *inode, const char *name)
{
	struct reiserfs_inode_info *reiserfs_i = REISERFS_I(inode);
	struct posix_acl **acl = &reiserfs_i->i_acl_access;
	if (strlen(name) != sizeof(POSIX_ACL_XATTR_ACCESS) - 1)
		return -EINVAL;
	if (!IS_ERR(*acl) && *acl) {
		posix_acl_release(*acl);
		*acl = ERR_PTR(-ENODATA);
	}

	return 0;
}

static int
posix_acl_access_list(struct inode *inode, const char *name, int namelen,
		      char *out)
{
	int len = namelen;
	if (!reiserfs_posixacl(inode->i_sb))
		return 0;
	if (out)
		memcpy(out, name, len);

	return len;
}

struct reiserfs_xattr_handler posix_acl_access_handler = {
	.prefix = POSIX_ACL_XATTR_ACCESS,
	.get = posix_acl_access_get,
	.set = posix_acl_access_set,
	.del = posix_acl_access_del,
	.list = posix_acl_access_list,
};

static int
posix_acl_default_get(struct inode *inode, const char *name,
		      void *buffer, size_t size)
{
	if (strlen(name) != sizeof(POSIX_ACL_XATTR_DEFAULT) - 1)
		return -EINVAL;
	return xattr_get_acl(inode, ACL_TYPE_DEFAULT, buffer, size);
}

static int
posix_acl_default_set(struct inode *inode, const char *name,
		      const void *value, size_t size, int flags)
{
	if (strlen(name) != sizeof(POSIX_ACL_XATTR_DEFAULT) - 1)
		return -EINVAL;
	return xattr_set_acl(inode, ACL_TYPE_DEFAULT, value, size);
}

static int posix_acl_default_del(struct inode *inode, const char *name)
{
	struct reiserfs_inode_info *reiserfs_i = REISERFS_I(inode);
	struct posix_acl **acl = &reiserfs_i->i_acl_default;
	if (strlen(name) != sizeof(POSIX_ACL_XATTR_DEFAULT) - 1)
		return -EINVAL;
	if (!IS_ERR(*acl) && *acl) {
		posix_acl_release(*acl);
		*acl = ERR_PTR(-ENODATA);
	}

	return 0;
}

static int
posix_acl_default_list(struct inode *inode, const char *name, int namelen,
		       char *out)
{
	int len = namelen;
	if (!reiserfs_posixacl(inode->i_sb))
		return 0;
	if (out)
		memcpy(out, name, len);

	return len;
}

struct reiserfs_xattr_handler posix_acl_default_handler = {
	.prefix = POSIX_ACL_XATTR_DEFAULT,
	.get = posix_acl_default_get,
	.set = posix_acl_default_set,
	.del = posix_acl_default_del,
	.list = posix_acl_default_list,
};
	/*
	 * ACLs can only be used on "new" objects, so if it's an old object
	 * there is nothing to inherit from
	 */
	if (get_inode_sd_version(dir) == STAT_DATA_V1)
		goto apply_umask;

	/*
	 * Don't apply ACLs to objects in the .reiserfs_priv tree.. This
	 * would be useless since permissions are ignored, and a pain because
	 * it introduces locking cycles
	 */
	if (IS_PRIVATE(dir)) {
		inode->i_flags |= S_PRIVATE;
		goto apply_umask;
	}

	err = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
	if (err)
		return err;

	if (default_acl) {
		err = __reiserfs_set_acl(th, inode, ACL_TYPE_DEFAULT,
					 default_acl);
		posix_acl_release(default_acl);
	}
	if (acl) {
		if (!err)
			err = __reiserfs_set_acl(th, inode, ACL_TYPE_ACCESS,
						 acl);
		posix_acl_release(acl);
	}

	return err;

apply_umask:
	/* no ACL, apply umask */
	inode->i_mode &= ~current_umask();
	return err;
}

/* This is used to cache the default acl before a new object is created.
 * The biggest reason for this is to get an idea of how many blocks will
 * actually be required for the create operation if we must inherit an ACL.
 * An ACL write can add up to 3 object creations and an additional file write
 * so we'd prefer not to reserve that many blocks in the journal if we can.
 * It also has the advantage of not loading the ACL with a transaction open,
 * this may seem silly, but if the owner of the directory is doing the
 * creation, the ACL may not be loaded since the permissions wouldn't require
 * it.
 * We return the number of blocks required for the transaction.
 */
int reiserfs_cache_default_acl(struct inode *inode)
{
	struct posix_acl *acl;
	int nblocks = 0;

	if (IS_PRIVATE(inode))
		return 0;

	acl = reiserfs_get_acl(inode, ACL_TYPE_DEFAULT);

	if (acl && !IS_ERR(acl)) {
		int size = reiserfs_acl_size(acl->a_count);

		/* Other xattrs can be created during inode creation. We don't
		 * want to claim too many blocks, so we check to see if we
		 * we need to create the tree to the xattrs, and then we
		 * just want two files. */
		nblocks = reiserfs_xattr_jcreate_nblocks(inode);
		nblocks += JOURNAL_BLOCKS_PER_OBJECT(inode->i_sb);

		REISERFS_I(inode)->i_flags |= i_has_xattr_dir;

		/* We need to account for writes + bitmaps for two files */
		nblocks += reiserfs_xattr_nblocks(inode, size) * 4;
		posix_acl_release(acl);
	}

	return nblocks;
}

/*
 * Called under i_mutex
 */
int reiserfs_acl_chmod(struct inode *inode)
{
	if (IS_PRIVATE(inode))
		return 0;
	if (get_inode_sd_version(inode) == STAT_DATA_V1 ||
	    !reiserfs_posixacl(inode->i_sb))
		return 0;

	return posix_acl_chmod(inode, inode->i_mode);
}
