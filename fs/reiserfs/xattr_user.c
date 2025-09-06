#include <linux/reiserfs_fs.h>
#include "reiserfs.h"
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include <linux/reiserfs_xattr.h>
#include <asm/uaccess.h>

#ifdef CONFIG_REISERFS_FS_POSIX_ACL
# include <linux/reiserfs_acl.h>
#endif

static int
user_get(struct inode *inode, const char *name, void *buffer, size_t size)
#include "xattr.h"
#include <linux/uaccess.h>

static int
user_get(const struct xattr_handler *handler, struct dentry *dentry,
	 const char *name, void *buffer, size_t size)
{

	if (strlen(name) < sizeof(XATTR_USER_PREFIX))
		return -EINVAL;
	if (!reiserfs_xattrs_user(inode->i_sb))
		return -EOPNOTSUPP;
	return reiserfs_xattr_get(inode, name, buffer, size);
}

static int
user_set(struct inode *inode, const char *name, const void *buffer,
	 size_t size, int flags)
{

	if (strlen(name) < sizeof(XATTR_USER_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs_user(inode->i_sb))
		return -EOPNOTSUPP;
	return reiserfs_xattr_set(inode, name, buffer, size, flags);
}

static int user_del(struct inode *inode, const char *name)
	if (!reiserfs_xattrs_user(dentry->d_sb))
		return -EOPNOTSUPP;
	return reiserfs_xattr_get(d_inode(dentry),
				  xattr_full_name(handler, name),
				  buffer, size);
}

static int
user_set(const struct xattr_handler *handler, struct dentry *dentry,
	 const char *name, const void *buffer, size_t size, int flags)
{
	if (strlen(name) < sizeof(XATTR_USER_PREFIX))
		return -EINVAL;

	if (!reiserfs_xattrs_user(inode->i_sb))
		return -EOPNOTSUPP;
	return 0;
}

static int
user_list(struct inode *inode, const char *name, int namelen, char *out)
{
	int len = namelen;
	if (!reiserfs_xattrs_user(inode->i_sb))
		return 0;

	if (out)
		memcpy(out, name, len);

	return len;
}

struct reiserfs_xattr_handler user_handler = {
	.prefix = XATTR_USER_PREFIX,
	.get = user_get,
	.set = user_set,
	.del = user_del,
	if (!reiserfs_xattrs_user(dentry->d_sb))
		return -EOPNOTSUPP;
	return reiserfs_xattr_set(d_inode(dentry),
				  xattr_full_name(handler, name),
				  buffer, size, flags);
}

static size_t user_list(const struct xattr_handler *handler,
			struct dentry *dentry, char *list, size_t list_size,
			const char *name, size_t name_len)
{
	const size_t len = name_len + 1;

	if (!reiserfs_xattrs_user(dentry->d_sb))
		return 0;
	if (list && len <= list_size) {
		memcpy(list, name, name_len);
		list[name_len] = '\0';
	}
	return len;
}

const struct xattr_handler reiserfs_xattr_user_handler = {
	.prefix = XATTR_USER_PREFIX,
	.get = user_get,
	.set = user_set,
	.list = user_list,
};
