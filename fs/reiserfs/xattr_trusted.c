#include <linux/reiserfs_fs.h>
#include "reiserfs.h"
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <asm/uaccess.h>

static int
trusted_get(struct inode *inode, const char *name, void *buffer, size_t size)
#include "xattr.h"
#include <linux/uaccess.h>

static int
trusted_get(const struct xattr_handler *handler, struct dentry *dentry,
	    const char *name, void *buffer, size_t size)
{
	if (strlen(name) < sizeof(XATTR_TRUSTED_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs(inode->i_sb))
		return -EOPNOTSUPP;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return -EPERM;

	return reiserfs_xattr_get(inode, name, buffer, size);
}

static int
trusted_set(struct inode *inode, const char *name, const void *buffer,
	    size_t size, int flags)
	if (!capable(CAP_SYS_ADMIN) || IS_PRIVATE(d_inode(dentry)))
		return -EPERM;

	return reiserfs_xattr_get(d_inode(dentry),
				  xattr_full_name(handler, name),
				  buffer, size);
}

static int
trusted_set(const struct xattr_handler *handler, struct dentry *dentry,
	    const char *name, const void *buffer, size_t size, int flags)
{
	if (strlen(name) < sizeof(XATTR_TRUSTED_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs(inode->i_sb))
		return -EOPNOTSUPP;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return -EPERM;

	return reiserfs_xattr_set(inode, name, buffer, size, flags);
}

static int trusted_del(struct inode *inode, const char *name)
{
	if (strlen(name) < sizeof(XATTR_TRUSTED_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs(inode->i_sb))
		return -EOPNOTSUPP;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return -EPERM;

	return 0;
}

static int
trusted_list(struct inode *inode, const char *name, int namelen, char *out)
{
	int len = namelen;

	if (!reiserfs_xattrs(inode->i_sb))
		return 0;

	if (!(capable(CAP_SYS_ADMIN) || is_reiserfs_priv_object(inode)))
		return 0;

	if (out)
		memcpy(out, name, len);

	return len;
}

struct reiserfs_xattr_handler trusted_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = trusted_get,
	.set = trusted_set,
	.del = trusted_del,
	if (!capable(CAP_SYS_ADMIN) || IS_PRIVATE(d_inode(dentry)))
		return -EPERM;

	return reiserfs_xattr_set(d_inode(dentry),
				  xattr_full_name(handler, name),
				  buffer, size, flags);
}

static size_t trusted_list(const struct xattr_handler *handler,
			   struct dentry *dentry, char *list, size_t list_size,
			   const char *name, size_t name_len)
{
	const size_t len = name_len + 1;

	if (!capable(CAP_SYS_ADMIN) || IS_PRIVATE(d_inode(dentry)))
		return 0;

	if (list && len <= list_size) {
		memcpy(list, name, name_len);
		list[name_len] = '\0';
	}
	return len;
}

const struct xattr_handler reiserfs_xattr_trusted_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = trusted_get,
	.set = trusted_set,
	.list = trusted_list,
};
