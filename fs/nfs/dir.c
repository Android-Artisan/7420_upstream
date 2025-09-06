/*
 *  linux/fs/nfs/dir.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs directory handling functions
 *
 * 10 Apr 1996	Added silly rename for unlink	--okir
 * 28 Sep 1996	Improved directory cache --okir
 * 23 Aug 1997  Claus Heine claus@momo.math.rwth-aachen.de 
 *              Re-implemented silly rename for unlink, newly implemented
 *              silly rename for nfs_rename() following the suggestions
 *              of Olaf Kirch (okir) found in this file.
 *              Following Linus comments on my original hack, this version
 *              depends only on the dcache stuff and doesn't touch the inode
 *              layer (iput() and friends).
 *  6 Jun 1999	Cache readdir lookups in the page cache. -DaveM
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/pagevec.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/sched.h>

#include "nfs4_fs.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include <linux/pagevec.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/swap.h>
#include <linux/sched.h>
#include <linux/kmemleak.h>
#include <linux/xattr.h>

#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"

#include "nfstrace.h"

/* #define NFS_DEBUG_VERBOSE 1 */

static int nfs_opendir(struct inode *, struct file *);
static int nfs_readdir(struct file *, void *, filldir_t);
static struct dentry *nfs_lookup(struct inode *, struct dentry *, struct nameidata *);
static int nfs_create(struct inode *, struct dentry *, int, struct nameidata *);
static int nfs_mkdir(struct inode *, struct dentry *, int);
static int nfs_rmdir(struct inode *, struct dentry *);
static int nfs_unlink(struct inode *, struct dentry *);
static int nfs_symlink(struct inode *, struct dentry *, const char *);
static int nfs_link(struct dentry *, struct inode *, struct dentry *);
static int nfs_mknod(struct inode *, struct dentry *, int, dev_t);
static int nfs_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *);
static int nfs_fsync_dir(struct file *, struct dentry *, int);
static loff_t nfs_llseek_dir(struct file *, loff_t, int);
static int nfs_closedir(struct inode *, struct file *);
static int nfs_readdir(struct file *, struct dir_context *);
static int nfs_fsync_dir(struct file *, loff_t, loff_t, int);
static loff_t nfs_llseek_dir(struct file *, loff_t, int);
static void nfs_readdir_clear_array(struct page*);

const struct file_operations nfs_dir_operations = {
	.llseek		= nfs_llseek_dir,
	.read		= generic_read_dir,
	.readdir	= nfs_readdir,
	.open		= nfs_opendir,
	.release	= nfs_release,
	.fsync		= nfs_fsync_dir,
};

const struct inode_operations nfs_dir_inode_operations = {
	.create		= nfs_create,
	.lookup		= nfs_lookup,
	.link		= nfs_link,
	.unlink		= nfs_unlink,
	.symlink	= nfs_symlink,
	.mkdir		= nfs_mkdir,
	.rmdir		= nfs_rmdir,
	.mknod		= nfs_mknod,
	.rename		= nfs_rename,
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
};

#ifdef CONFIG_NFS_V3
const struct inode_operations nfs3_dir_inode_operations = {
	.create		= nfs_create,
	.lookup		= nfs_lookup,
	.link		= nfs_link,
	.unlink		= nfs_unlink,
	.symlink	= nfs_symlink,
	.mkdir		= nfs_mkdir,
	.rmdir		= nfs_rmdir,
	.mknod		= nfs_mknod,
	.rename		= nfs_rename,
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.listxattr	= nfs3_listxattr,
	.getxattr	= nfs3_getxattr,
	.setxattr	= nfs3_setxattr,
	.removexattr	= nfs3_removexattr,
};
#endif  /* CONFIG_NFS_V3 */

#ifdef CONFIG_NFS_V4

static struct dentry *nfs_atomic_lookup(struct inode *, struct dentry *, struct nameidata *);
const struct inode_operations nfs4_dir_inode_operations = {
	.create		= nfs_create,
	.lookup		= nfs_atomic_lookup,
	.link		= nfs_link,
	.unlink		= nfs_unlink,
	.symlink	= nfs_symlink,
	.mkdir		= nfs_mkdir,
	.rmdir		= nfs_rmdir,
	.mknod		= nfs_mknod,
	.rename		= nfs_rename,
	.permission	= nfs_permission,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
	.getxattr       = nfs4_getxattr,
	.setxattr       = nfs4_setxattr,
	.listxattr      = nfs4_listxattr,
};

#endif /* CONFIG_NFS_V4 */
	.iterate	= nfs_readdir,
	.open		= nfs_opendir,
	.release	= nfs_closedir,
	.fsync		= nfs_fsync_dir,
};

const struct address_space_operations nfs_dir_aops = {
	.freepage = nfs_readdir_clear_array,
};

static struct nfs_open_dir_context *alloc_nfs_open_dir_context(struct inode *dir, struct rpc_cred *cred)
{
	struct nfs_inode *nfsi = NFS_I(dir);
	struct nfs_open_dir_context *ctx;
	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx != NULL) {
		ctx->duped = 0;
		ctx->attr_gencount = nfsi->attr_gencount;
		ctx->dir_cookie = 0;
		ctx->dup_cookie = 0;
		ctx->cred = get_rpccred(cred);
		spin_lock(&dir->i_lock);
		list_add(&ctx->list, &nfsi->open_files);
		spin_unlock(&dir->i_lock);
		return ctx;
	}
	return  ERR_PTR(-ENOMEM);
}

static void put_nfs_open_dir_context(struct inode *dir, struct nfs_open_dir_context *ctx)
{
	spin_lock(&dir->i_lock);
	list_del(&ctx->list);
	spin_unlock(&dir->i_lock);
	put_rpccred(ctx->cred);
	kfree(ctx);
}

/*
 * Open file
 */
static int
nfs_opendir(struct inode *inode, struct file *filp)
{
	int res;

	dfprintk(FILE, "NFS: open dir(%s/%s)\n",
			filp->f_path.dentry->d_parent->d_name.name,
			filp->f_path.dentry->d_name.name);

	nfs_inc_stats(inode, NFSIOS_VFSOPEN);

	/* Call generic open code in order to cache credentials */
	res = nfs_open(inode, filp);
	return res;
}

typedef __be32 * (*decode_dirent_t)(__be32 *, struct nfs_entry *, int);
typedef struct {
	struct file	*file;
	struct page	*page;
	unsigned long	page_index;
	__be32		*ptr;
	u64		*dir_cookie;
	loff_t		current_index;
	struct nfs_entry *entry;
	decode_dirent_t	decode;
	int		plus;
	unsigned long	timestamp;
	int		timestamp_valid;
} nfs_readdir_descriptor_t;

/* Now we cache directories properly, by stuffing the dirent
 * data directly in the page cache.
 *
 * Inode invalidation due to refresh etc. takes care of
 * _everything_, no sloppy entry flushing logic, no extraneous
 * copying, network direct to page cache, the way it was meant
 * to be.
 *
 * NOTE: Dirent information verification is done always by the
 *	 page-in of the RPC reply, nowhere else, this simplies
 *	 things substantially.
 */
static
int nfs_readdir_filler(nfs_readdir_descriptor_t *desc, struct page *page)
{
	struct file	*file = desc->file;
	struct inode	*inode = file->f_path.dentry->d_inode;
	struct rpc_cred	*cred = nfs_file_cred(file);
	unsigned long	timestamp;
	int		error;

	dfprintk(DIRCACHE, "NFS: %s: reading cookie %Lu into page %lu\n",
			__func__, (long long)desc->entry->cookie,
			page->index);

 again:
	timestamp = jiffies;
	error = NFS_PROTO(inode)->readdir(file->f_path.dentry, cred, desc->entry->cookie, page,
	int res = 0;
	struct nfs_open_dir_context *ctx;
	struct rpc_cred *cred;

	dfprintk(FILE, "NFS: open dir(%pD2)\n", filp);

	nfs_inc_stats(inode, NFSIOS_VFSOPEN);

	cred = rpc_lookup_cred();
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	ctx = alloc_nfs_open_dir_context(inode, cred);
	if (IS_ERR(ctx)) {
		res = PTR_ERR(ctx);
		goto out;
	}
	filp->private_data = ctx;
	if (filp->f_path.dentry == filp->f_path.mnt->mnt_root) {
		/* This is a mountpoint, so d_revalidate will never
		 * have been called, so we need to refresh the
		 * inode (for close-open consistency) ourselves.
		 */
		__nfs_revalidate_inode(NFS_SERVER(inode), inode);
	}
out:
	put_rpccred(cred);
	return res;
}

static int
nfs_closedir(struct inode *inode, struct file *filp)
{
	put_nfs_open_dir_context(file_inode(filp), filp->private_data);
	return 0;
}

struct nfs_cache_array_entry {
	u64 cookie;
	u64 ino;
	struct qstr string;
	unsigned char d_type;
};

struct nfs_cache_array {
	int size;
	int eof_index;
	u64 last_cookie;
	struct nfs_cache_array_entry array[0];
};

typedef int (*decode_dirent_t)(struct xdr_stream *, struct nfs_entry *, int);
typedef struct {
	struct file	*file;
	struct page	*page;
	struct dir_context *ctx;
	unsigned long	page_index;
	u64		*dir_cookie;
	u64		last_cookie;
	loff_t		current_index;
	decode_dirent_t	decode;

	unsigned long	timestamp;
	unsigned long	gencount;
	unsigned int	cache_entry_index;
	unsigned int	plus:1;
	unsigned int	eof:1;
} nfs_readdir_descriptor_t;

static
void nfs_readdir_init_array(struct page *page)
{
	struct nfs_cache_array *array;

	array = kmap_atomic(page);
	memset(array, 0, sizeof(struct nfs_cache_array));
	array->eof_index = -1;
	kunmap_atomic(array);
}

/*
 * The caller is responsible for calling nfs_readdir_release_array(page)
 */
static
struct nfs_cache_array *nfs_readdir_get_array(struct page *page)
{
	void *ptr;
	if (page == NULL)
		return ERR_PTR(-EIO);
	ptr = kmap(page);
	if (ptr == NULL)
		return ERR_PTR(-ENOMEM);
	return ptr;
}

static
void nfs_readdir_release_array(struct page *page)
{
	kunmap(page);
}

/*
 * we are freeing strings created by nfs_add_to_readdir_array()
 */
static
void nfs_readdir_clear_array(struct page *page)
{
	struct nfs_cache_array *array;
	int i;

	array = kmap_atomic(page);
	for (i = 0; i < array->size; i++)
		kfree(array->array[i].string.name);
	array->size = 0;
	kunmap_atomic(array);
}

/*
 * the caller is responsible for freeing qstr.name
 * when called by nfs_readdir_add_to_array, the strings will be freed in
 * nfs_clear_readdir_array()
 */
static
int nfs_readdir_make_qstr(struct qstr *string, const char *name, unsigned int len)
{
	string->len = len;
	string->name = kmemdup(name, len, GFP_KERNEL);
	if (string->name == NULL)
		return -ENOMEM;
	/*
	 * Avoid a kmemleak false positive. The pointer to the name is stored
	 * in a page cache page which kmemleak does not scan.
	 */
	kmemleak_not_leak(string->name);
	string->hash = full_name_hash(name, len);
	return 0;
}

static
int nfs_readdir_add_to_array(struct nfs_entry *entry, struct page *page)
{
	struct nfs_cache_array *array = nfs_readdir_get_array(page);
	struct nfs_cache_array_entry *cache_entry;
	int ret;

	if (IS_ERR(array))
		return PTR_ERR(array);

	cache_entry = &array->array[array->size];

	/* Check that this entry lies within the page bounds */
	ret = -ENOSPC;
	if ((char *)&cache_entry[1] - (char *)page_address(page) > PAGE_SIZE)
		goto out;

	cache_entry->cookie = entry->prev_cookie;
	cache_entry->ino = entry->ino;
	cache_entry->d_type = entry->d_type;
	ret = nfs_readdir_make_qstr(&cache_entry->string, entry->name, entry->len);
	if (ret)
		goto out;
	array->last_cookie = entry->cookie;
	array->size++;
	if (entry->eof != 0)
		array->eof_index = array->size;
out:
	nfs_readdir_release_array(page);
	return ret;
}

static
int nfs_readdir_search_for_pos(struct nfs_cache_array *array, nfs_readdir_descriptor_t *desc)
{
	loff_t diff = desc->ctx->pos - desc->current_index;
	unsigned int index;

	if (diff < 0)
		goto out_eof;
	if (diff >= array->size) {
		if (array->eof_index >= 0)
			goto out_eof;
		return -EAGAIN;
	}

	index = (unsigned int)diff;
	*desc->dir_cookie = array->array[index].cookie;
	desc->cache_entry_index = index;
	return 0;
out_eof:
	desc->eof = true;
	return -EBADCOOKIE;
}

static bool
nfs_readdir_inode_mapping_valid(struct nfs_inode *nfsi)
{
	if (nfsi->cache_validity & (NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA))
		return false;
	smp_rmb();
	return !test_bit(NFS_INO_INVALIDATING, &nfsi->flags);
}

static
int nfs_readdir_search_for_cookie(struct nfs_cache_array *array, nfs_readdir_descriptor_t *desc)
{
	int i;
	loff_t new_pos;
	int status = -EAGAIN;

	for (i = 0; i < array->size; i++) {
		if (array->array[i].cookie == *desc->dir_cookie) {
			struct nfs_inode *nfsi = NFS_I(file_inode(desc->file));
			struct nfs_open_dir_context *ctx = desc->file->private_data;

			new_pos = desc->current_index + i;
			if (ctx->attr_gencount != nfsi->attr_gencount ||
			    !nfs_readdir_inode_mapping_valid(nfsi)) {
				ctx->duped = 0;
				ctx->attr_gencount = nfsi->attr_gencount;
			} else if (new_pos < desc->ctx->pos) {
				if (ctx->duped > 0
				    && ctx->dup_cookie == *desc->dir_cookie) {
					if (printk_ratelimit()) {
						pr_notice("NFS: directory %pD2 contains a readdir loop."
								"Please contact your server vendor.  "
								"The file: %.*s has duplicate cookie %llu\n",
								desc->file, array->array[i].string.len,
								array->array[i].string.name, *desc->dir_cookie);
					}
					status = -ELOOP;
					goto out;
				}
				ctx->dup_cookie = *desc->dir_cookie;
				ctx->duped = -1;
			}
			desc->ctx->pos = new_pos;
			desc->cache_entry_index = i;
			return 0;
		}
	}
	if (array->eof_index >= 0) {
		status = -EBADCOOKIE;
		if (*desc->dir_cookie == array->last_cookie)
			desc->eof = true;
	}
out:
	return status;
}

static
int nfs_readdir_search_array(nfs_readdir_descriptor_t *desc)
{
	struct nfs_cache_array *array;
	int status;

	array = nfs_readdir_get_array(desc->page);
	if (IS_ERR(array)) {
		status = PTR_ERR(array);
		goto out;
	}

	if (*desc->dir_cookie == 0)
		status = nfs_readdir_search_for_pos(array, desc);
	else
		status = nfs_readdir_search_for_cookie(array, desc);

	if (status == -EAGAIN) {
		desc->last_cookie = array->last_cookie;
		desc->current_index += array->size;
		desc->page_index++;
	}
	nfs_readdir_release_array(desc->page);
out:
	return status;
}

/* Fill a page with xdr information before transferring to the cache page */
static
int nfs_readdir_xdr_filler(struct page **pages, nfs_readdir_descriptor_t *desc,
			struct nfs_entry *entry, struct file *file, struct inode *inode)
{
	struct nfs_open_dir_context *ctx = file->private_data;
	struct rpc_cred	*cred = ctx->cred;
	unsigned long	timestamp, gencount;
	int		error;

 again:
	timestamp = jiffies;
	gencount = nfs_inc_attr_generation_counter();
	error = NFS_PROTO(inode)->readdir(file_dentry(file), cred, entry->cookie, pages,
					  NFS_SERVER(inode)->dtsize, desc->plus);
	if (error < 0) {
		/* We requested READDIRPLUS, but the server doesn't grok it */
		if (error == -ENOTSUPP && desc->plus) {
			NFS_SERVER(inode)->caps &= ~NFS_CAP_READDIRPLUS;
			clear_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(inode)->flags);
			desc->plus = 0;
			goto again;
		}
		goto error;
	}
	desc->timestamp = timestamp;
	desc->timestamp_valid = 1;
	SetPageUptodate(page);
	/* Ensure consistent page alignment of the data.
	 * Note: assumes we have exclusive access to this mapping either
	 *	 through inode->i_mutex or some other mechanism.
	 */
	desc->gencount = gencount;
error:
	return error;
}

static int xdr_decode(nfs_readdir_descriptor_t *desc,
		      struct nfs_entry *entry, struct xdr_stream *xdr)
{
	int error;

	error = desc->decode(xdr, entry, desc->plus);
	if (error)
		return error;
	entry->fattr->time_start = desc->timestamp;
	entry->fattr->gencount = desc->gencount;
	return 0;
}

/* Match file and dirent using either filehandle or fileid
 * Note: caller is responsible for checking the fsid
 */
static
int nfs_same_file(struct dentry *dentry, struct nfs_entry *entry)
{
	struct nfs_inode *nfsi;

	if (d_really_is_negative(dentry))
		return 0;

	nfsi = NFS_I(d_inode(dentry));
	if (entry->fattr->fileid == nfsi->fileid)
		return 1;
	if (nfs_compare_fh(entry->fh, &nfsi->fh) == 0)
		return 1;
	return 0;
}

static
bool nfs_use_readdirplus(struct inode *dir, struct dir_context *ctx)
{
	if (!nfs_server_capable(dir, NFS_CAP_READDIRPLUS))
		return false;
	if (test_and_clear_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(dir)->flags))
		return true;
	if (ctx->pos == 0)
		return true;
	return false;
}

/*
 * This function is called by the lookup code to request the use of
 * readdirplus to accelerate any future lookups in the same
 * directory.
 */
static
void nfs_advise_use_readdirplus(struct inode *dir)
{
	set_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(dir)->flags);
}

/*
 * This function is mainly for use by nfs_getattr().
 *
 * If this is an 'ls -l', we want to force use of readdirplus.
 * Do this by checking if there is an active file descriptor
 * and calling nfs_advise_use_readdirplus, then forcing a
 * cache flush.
 */
void nfs_force_use_readdirplus(struct inode *dir)
{
	if (!list_empty(&NFS_I(dir)->open_files)) {
		nfs_advise_use_readdirplus(dir);
		invalidate_mapping_pages(dir->i_mapping, 0, -1);
	}
}

static
void nfs_prime_dcache(struct dentry *parent, struct nfs_entry *entry)
{
	struct qstr filename = QSTR_INIT(entry->name, entry->len);
	struct dentry *dentry;
	struct dentry *alias;
	struct inode *dir = d_inode(parent);
	struct inode *inode;
	int status;

	if (!(entry->fattr->valid & NFS_ATTR_FATTR_FILEID))
		return;
	if (!(entry->fattr->valid & NFS_ATTR_FATTR_FSID))
		return;
	if (filename.name[0] == '.') {
		if (filename.len == 1)
			return;
		if (filename.len == 2 && filename.name[1] == '.')
			return;
	}
	filename.hash = full_name_hash(filename.name, filename.len);

	dentry = d_lookup(parent, &filename);
	if (dentry != NULL) {
		/* Is there a mountpoint here? If so, just exit */
		if (!nfs_fsid_equal(&NFS_SB(dentry->d_sb)->fsid,
					&entry->fattr->fsid))
			goto out;
		if (nfs_same_file(dentry, entry)) {
			nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
			status = nfs_refresh_inode(d_inode(dentry), entry->fattr);
			if (!status)
				nfs_setsecurity(d_inode(dentry), entry->fattr, entry->label);
			goto out;
		} else {
			d_invalidate(dentry);
			dput(dentry);
		}
	}

	dentry = d_alloc(parent, &filename);
	if (dentry == NULL)
		return;

	inode = nfs_fhget(dentry->d_sb, entry->fh, entry->fattr, entry->label);
	if (IS_ERR(inode))
		goto out;

	alias = d_splice_alias(inode, dentry);
	if (IS_ERR(alias))
		goto out;
	else if (alias) {
		nfs_set_verifier(alias, nfs_save_change_attribute(dir));
		dput(alias);
	} else
		nfs_set_verifier(dentry, nfs_save_change_attribute(dir));

out:
	dput(dentry);
}

/* Perform conversion from xdr to cache array */
static
int nfs_readdir_page_filler(nfs_readdir_descriptor_t *desc, struct nfs_entry *entry,
				struct page **xdr_pages, struct page *page, unsigned int buflen)
{
	struct xdr_stream stream;
	struct xdr_buf buf;
	struct page *scratch;
	struct nfs_cache_array *array;
	unsigned int count = 0;
	int status;

	scratch = alloc_page(GFP_KERNEL);
	if (scratch == NULL)
		return -ENOMEM;

	if (buflen == 0)
		goto out_nopages;

	xdr_init_decode_pages(&stream, &buf, xdr_pages, buflen);
	xdr_set_scratch_buffer(&stream, page_address(scratch), PAGE_SIZE);

	do {
		if (entry->label)
			entry->label->len = NFS4_MAXLABELLEN;

		status = xdr_decode(desc, entry, &stream);
		if (status != 0) {
			if (status == -EAGAIN)
				status = 0;
			break;
		}

		count++;

		if (desc->plus != 0)
			nfs_prime_dcache(file_dentry(desc->file), entry);

		status = nfs_readdir_add_to_array(entry, page);
		if (status != 0)
			break;
	} while (!entry->eof);

out_nopages:
	if (count == 0 || (status == -EBADCOOKIE && entry->eof != 0)) {
		array = nfs_readdir_get_array(page);
		if (!IS_ERR(array)) {
			array->eof_index = array->size;
			status = 0;
			nfs_readdir_release_array(page);
		} else
			status = PTR_ERR(array);
	}

	put_page(scratch);
	return status;
}

static
void nfs_readdir_free_pages(struct page **pages, unsigned int npages)
{
	unsigned int i;
	for (i = 0; i < npages; i++)
		put_page(pages[i]);
}

/*
 * nfs_readdir_large_page will allocate pages that must be freed with a call
 * to nfs_readdir_free_pagearray
 */
static
int nfs_readdir_alloc_pages(struct page **pages, unsigned int npages)
{
	unsigned int i;

	for (i = 0; i < npages; i++) {
		struct page *page = alloc_page(GFP_KERNEL);
		if (page == NULL)
			goto out_freepages;
		pages[i] = page;
	}
	return 0;

out_freepages:
	nfs_readdir_free_pages(pages, i);
	return -ENOMEM;
}

static
int nfs_readdir_xdr_to_array(nfs_readdir_descriptor_t *desc, struct page *page, struct inode *inode)
{
	struct page *pages[NFS_MAX_READDIR_PAGES];
	struct nfs_entry entry;
	struct file	*file = desc->file;
	struct nfs_cache_array *array;
	int status = -ENOMEM;
	unsigned int array_size = ARRAY_SIZE(pages);

	nfs_readdir_init_array(page);

	entry.prev_cookie = 0;
	entry.cookie = desc->last_cookie;
	entry.eof = 0;
	entry.fh = nfs_alloc_fhandle();
	entry.fattr = nfs_alloc_fattr();
	entry.server = NFS_SERVER(inode);
	if (entry.fh == NULL || entry.fattr == NULL)
		goto out;

	entry.label = nfs4_label_alloc(NFS_SERVER(inode), GFP_NOWAIT);
	if (IS_ERR(entry.label)) {
		status = PTR_ERR(entry.label);
		goto out;
	}

	array = nfs_readdir_get_array(page);
	if (IS_ERR(array)) {
		status = PTR_ERR(array);
		goto out_label_free;
	}

	status = nfs_readdir_alloc_pages(pages, array_size);
	if (status < 0)
		goto out_release_array;
	do {
		unsigned int pglen;
		status = nfs_readdir_xdr_filler(pages, desc, &entry, file, inode);

		if (status < 0)
			break;
		pglen = status;
		status = nfs_readdir_page_filler(desc, &entry, pages, page, pglen);
		if (status < 0) {
			if (status == -ENOSPC)
				status = 0;
			break;
		}
	} while (array->eof_index < 0);

	nfs_readdir_free_pages(pages, array_size);
out_release_array:
	nfs_readdir_release_array(page);
out_label_free:
	nfs4_label_free(entry.label);
out:
	nfs_free_fattr(entry.fattr);
	nfs_free_fhandle(entry.fh);
	return status;
}

/*
 * Now we cache directories properly, by converting xdr information
 * to an array that can be used for lookups later.  This results in
 * fewer cache pages, since we can store more information on each page.
 * We only need to convert from xdr once so future lookups are much simpler
 */
static
int nfs_readdir_filler(nfs_readdir_descriptor_t *desc, struct page* page)
{
	struct inode	*inode = file_inode(desc->file);
	int ret;

	ret = nfs_readdir_xdr_to_array(desc, page, inode);
	if (ret < 0)
		goto error;
	SetPageUptodate(page);

	if (invalidate_inode_pages2_range(inode->i_mapping, page->index + 1, -1) < 0) {
		/* Should never happen */
		nfs_zap_mapping(inode, inode->i_mapping);
	}
	unlock_page(page);
	return 0;
 error:
	nfs_readdir_clear_array(page);
	unlock_page(page);
	return -EIO;
}

static inline
int dir_decode(nfs_readdir_descriptor_t *desc)
{
	__be32	*p = desc->ptr;
	p = desc->decode(p, desc->entry, desc->plus);
	if (IS_ERR(p))
		return PTR_ERR(p);
	desc->ptr = p;
	if (desc->timestamp_valid)
		desc->entry->fattr->time_start = desc->timestamp;
	else
		desc->entry->fattr->valid &= ~NFS_ATTR_FATTR;
	return 0;
}

static inline
void dir_page_release(nfs_readdir_descriptor_t *desc)
{
	kunmap(desc->page);
	page_cache_release(desc->page);
	desc->page = NULL;
	desc->ptr = NULL;
}

/*
 * Given a pointer to a buffer that has already been filled by a call
 * to readdir, find the next entry with cookie '*desc->dir_cookie'.
 *
 * If the end of the buffer has been reached, return -EAGAIN, if not,
 * return the offset within the buffer of the next entry to be
 * read.
 */
static inline
int find_dirent(nfs_readdir_descriptor_t *desc)
{
	struct nfs_entry *entry = desc->entry;
	int		loop_count = 0,
			status;

	while((status = dir_decode(desc)) == 0) {
		dfprintk(DIRCACHE, "NFS: %s: examining cookie %Lu\n",
				__func__, (unsigned long long)entry->cookie);
		if (entry->prev_cookie == *desc->dir_cookie)
			break;
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}
	return status;
}

/*
 * Given a pointer to a buffer that has already been filled by a call
 * to readdir, find the entry at offset 'desc->file->f_pos'.
 *
 * If the end of the buffer has been reached, return -EAGAIN, if not,
 * return the offset within the buffer of the next entry to be
 * read.
 */
static inline
int find_dirent_index(nfs_readdir_descriptor_t *desc)
{
	struct nfs_entry *entry = desc->entry;
	int		loop_count = 0,
			status;

	for(;;) {
		status = dir_decode(desc);
		if (status)
			break;

		dfprintk(DIRCACHE, "NFS: found cookie %Lu at index %Ld\n",
				(unsigned long long)entry->cookie, desc->current_index);

		if (desc->file->f_pos == desc->current_index) {
			*desc->dir_cookie = entry->cookie;
			break;
		}
		desc->current_index++;
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}
	return status;
}

/*
 * Find the given page, and call find_dirent() or find_dirent_index in
 * order to try to return the next entry.
 */
static inline
int find_dirent_page(nfs_readdir_descriptor_t *desc)
{
	struct inode	*inode = desc->file->f_path.dentry->d_inode;
	struct page	*page;
	int		status;

	dfprintk(DIRCACHE, "NFS: %s: searching page %ld for target %Lu\n",
			__func__, desc->page_index,
			(long long) *desc->dir_cookie);

	/* If we find the page in the page_cache, we cannot be sure
	 * how fresh the data is, so we will ignore readdir_plus attributes.
	 */
	desc->timestamp_valid = 0;
	page = read_cache_page(inode->i_mapping, desc->page_index,
			       (filler_t *)nfs_readdir_filler, desc);
	if (IS_ERR(page)) {
		status = PTR_ERR(page);
		goto out;
	}

	/* NOTE: Someone else may have changed the READDIRPLUS flag */
	desc->page = page;
	desc->ptr = kmap(page);		/* matching kunmap in nfs_do_filldir */
	if (*desc->dir_cookie != 0)
		status = find_dirent(desc);
	else
		status = find_dirent_index(desc);
	if (status < 0)
		dir_page_release(desc);
 out:
	dfprintk(DIRCACHE, "NFS: %s: returns %d\n", __func__, status);
	return status;
}

/*
 * Recurse through the page cache pages, and return a
 * filled nfs_entry structure of the next directory entry if possible.
 *
 * The target for the search is '*desc->dir_cookie' if non-0,
 * 'desc->file->f_pos' otherwise
 */
static inline
int readdir_search_pagecache(nfs_readdir_descriptor_t *desc)
{
	int		loop_count = 0;
	int		res;

	/* Always search-by-index from the beginning of the cache */
	if (*desc->dir_cookie == 0) {
		dfprintk(DIRCACHE, "NFS: readdir_search_pagecache() searching for offset %Ld\n",
				(long long)desc->file->f_pos);
		desc->page_index = 0;
		desc->entry->cookie = desc->entry->prev_cookie = 0;
		desc->entry->eof = 0;
		desc->current_index = 0;
	} else
		dfprintk(DIRCACHE, "NFS: readdir_search_pagecache() searching for cookie %Lu\n",
				(unsigned long long)*desc->dir_cookie);

	for (;;) {
		res = find_dirent_page(desc);
		if (res != -EAGAIN)
			break;
		/* Align to beginning of next page */
		desc->page_index ++;
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}

	dfprintk(DIRCACHE, "NFS: %s: returns %d\n", __func__, res);
	return res;
}

static inline unsigned int dt_type(struct inode *inode)
{
	return (inode->i_mode >> 12) & 15;
}

static struct dentry *nfs_readdir_lookup(nfs_readdir_descriptor_t *desc);
	return ret;
}

static
void cache_page_release(nfs_readdir_descriptor_t *desc)
{
	page_cache_release(desc->page);
	desc->page = NULL;
}

static
struct page *get_cache_page(nfs_readdir_descriptor_t *desc)
{
	return read_cache_page(file_inode(desc->file)->i_mapping,
			desc->page_index, (filler_t *)nfs_readdir_filler, desc);
}

/*
 * Returns 0 if desc->dir_cookie was found on page desc->page_index
 * and locks the page to prevent removal from the page cache.
 */
static
int find_and_lock_cache_page(nfs_readdir_descriptor_t *desc)
{
	int res;

	desc->page = get_cache_page(desc);
	if (IS_ERR(desc->page))
		return PTR_ERR(desc->page);
	res = lock_page_killable(desc->page);
	if (res != 0)
		goto error;
	res = -EAGAIN;
	if (desc->page->mapping != NULL) {
		res = nfs_readdir_search_array(desc);
		if (res == 0)
			return 0;
	}
	unlock_page(desc->page);
error:
	cache_page_release(desc);
	return res;
}

/* Search for desc->dir_cookie from the beginning of the page cache */
static inline
int readdir_search_pagecache(nfs_readdir_descriptor_t *desc)
{
	int res;

	if (desc->page_index == 0) {
		desc->current_index = 0;
		desc->last_cookie = 0;
	}
	do {
		res = find_and_lock_cache_page(desc);
	} while (res == -EAGAIN);
	return res;
}

/*
 * Once we've found the start of the dirent within a page: fill 'er up...
 */
static 
int nfs_do_filldir(nfs_readdir_descriptor_t *desc, void *dirent,
		   filldir_t filldir)
{
	struct file	*file = desc->file;
	struct nfs_entry *entry = desc->entry;
	struct dentry	*dentry = NULL;
	u64		fileid;
	int		loop_count = 0,
			res;

	dfprintk(DIRCACHE, "NFS: nfs_do_filldir() filling starting @ cookie %Lu\n",
			(unsigned long long)entry->cookie);

	for(;;) {
		unsigned d_type = DT_UNKNOWN;
		/* Note: entry->prev_cookie contains the cookie for
		 *	 retrieving the current dirent on the server */
		fileid = entry->ino;

		/* Get a dentry if we have one */
		if (dentry != NULL)
			dput(dentry);
		dentry = nfs_readdir_lookup(desc);

		/* Use readdirplus info */
		if (dentry != NULL && dentry->d_inode != NULL) {
			d_type = dt_type(dentry->d_inode);
			fileid = NFS_FILEID(dentry->d_inode);
		}

		res = filldir(dirent, entry->name, entry->len, 
			      file->f_pos, nfs_compat_user_ino64(fileid),
			      d_type);
		if (res < 0)
			break;
		file->f_pos++;
		*desc->dir_cookie = entry->cookie;
		if (dir_decode(desc) != 0) {
			desc->page_index ++;
			break;
		}
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}
	dir_page_release(desc);
	if (dentry != NULL)
		dput(dentry);
int nfs_do_filldir(nfs_readdir_descriptor_t *desc)
{
	struct file	*file = desc->file;
	int i = 0;
	int res = 0;
	struct nfs_cache_array *array = NULL;
	struct nfs_open_dir_context *ctx = file->private_data;

	array = nfs_readdir_get_array(desc->page);
	if (IS_ERR(array)) {
		res = PTR_ERR(array);
		goto out;
	}

	for (i = desc->cache_entry_index; i < array->size; i++) {
		struct nfs_cache_array_entry *ent;

		ent = &array->array[i];
		if (!dir_emit(desc->ctx, ent->string.name, ent->string.len,
		    nfs_compat_user_ino64(ent->ino), ent->d_type)) {
			desc->eof = true;
			break;
		}
		desc->ctx->pos++;
		if (i < (array->size-1))
			*desc->dir_cookie = array->array[i+1].cookie;
		else
			*desc->dir_cookie = array->last_cookie;
		if (ctx->duped != 0)
			ctx->duped = 1;
	}
	if (array->eof_index >= 0)
		desc->eof = true;

	nfs_readdir_release_array(desc->page);
out:
	dfprintk(DIRCACHE, "NFS: nfs_do_filldir() filling ended @ cookie %Lu; returning = %d\n",
			(unsigned long long)*desc->dir_cookie, res);
	return res;
}

/*
 * If we cannot find a cookie in our cache, we suspect that this is
 * because it points to a deleted file, so we ask the server to return
 * whatever it thinks is the next entry. We then feed this to filldir.
 * If all goes well, we should then be able to find our way round the
 * cache on the next call to readdir_search_pagecache();
 *
 * NOTE: we cannot add the anonymous page to the pagecache because
 *	 the data it contains might not be page aligned. Besides,
 *	 we should already have a complete representation of the
 *	 directory in the page cache by the time we get here.
 */
static inline
int uncached_readdir(nfs_readdir_descriptor_t *desc, void *dirent,
		     filldir_t filldir)
{
	struct file	*file = desc->file;
	struct inode	*inode = file->f_path.dentry->d_inode;
	struct rpc_cred	*cred = nfs_file_cred(file);
	struct page	*page = NULL;
	int		status;
	unsigned long	timestamp;
int uncached_readdir(nfs_readdir_descriptor_t *desc)
{
	struct page	*page = NULL;
	int		status;
	struct inode *inode = file_inode(desc->file);
	struct nfs_open_dir_context *ctx = desc->file->private_data;

	dfprintk(DIRCACHE, "NFS: uncached_readdir() searching for cookie %Lu\n",
			(unsigned long long)*desc->dir_cookie);

	page = alloc_page(GFP_HIGHUSER);
	if (!page) {
		status = -ENOMEM;
		goto out;
	}
	timestamp = jiffies;
	status = NFS_PROTO(inode)->readdir(file->f_path.dentry, cred,
						*desc->dir_cookie, page,
						NFS_SERVER(inode)->dtsize,
						desc->plus);
	desc->page = page;
	desc->ptr = kmap(page);		/* matching kunmap in nfs_do_filldir */
	if (status >= 0) {
		desc->timestamp = timestamp;
		desc->timestamp_valid = 1;
		if ((status = dir_decode(desc)) == 0)
			desc->entry->prev_cookie = *desc->dir_cookie;
	} else
		status = -EIO;
	if (status < 0)
		goto out_release;

	status = nfs_do_filldir(desc, dirent, filldir);

	/* Reset read descriptor so it searches the page cache from
	 * the start upon the next call to readdir_search_pagecache() */
	desc->page_index = 0;
	desc->entry->cookie = desc->entry->prev_cookie = 0;
	desc->entry->eof = 0;

	desc->page_index = 0;
	desc->last_cookie = *desc->dir_cookie;
	desc->page = page;
	ctx->duped = 0;

	status = nfs_readdir_xdr_to_array(desc, page, inode);
	if (status < 0)
		goto out_release;

	status = nfs_do_filldir(desc);

 out_release:
	nfs_readdir_clear_array(desc->page);
	cache_page_release(desc);
 out:
	dfprintk(DIRCACHE, "NFS: %s: returns %d\n",
			__func__, status);
	return status;
 out_release:
	dir_page_release(desc);
	goto out;
}

	cache_page_release(desc);
	goto out;
}

/* The file offset position represents the dirent entry number.  A
   last cookie cache takes care of the common case of reading the
   whole directory.
 */
static int nfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry	*dentry = filp->f_path.dentry;
	struct inode	*inode = dentry->d_inode;
	nfs_readdir_descriptor_t my_desc,
			*desc = &my_desc;
	struct nfs_entry my_entry;
	struct nfs_fh	 fh;
	struct nfs_fattr fattr;
	long		res;

	dfprintk(FILE, "NFS: readdir(%s/%s) starting at cookie %llu\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,
			(long long)filp->f_pos);
	nfs_inc_stats(inode, NFSIOS_VFSGETDENTS);

	/*
	 * filp->f_pos points to the dirent entry number.
static int nfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct dentry	*dentry = file_dentry(file);
	struct inode	*inode = d_inode(dentry);
	nfs_readdir_descriptor_t my_desc,
			*desc = &my_desc;
	struct nfs_open_dir_context *dir_ctx = file->private_data;
	int res = 0;

	dfprintk(FILE, "NFS: readdir(%pD2) starting at cookie %llu\n",
			file, (long long)ctx->pos);
	nfs_inc_stats(inode, NFSIOS_VFSGETDENTS);

	/*
	 * ctx->pos points to the dirent entry number.
	 * *desc->dir_cookie has the cookie for the next entry. We have
	 * to either find the entry with the appropriate number or
	 * revalidate the cookie.
	 */
	memset(desc, 0, sizeof(*desc));

	desc->file = filp;
	desc->dir_cookie = &nfs_file_open_context(filp)->dir_cookie;
	desc->decode = NFS_PROTO(inode)->decode_dirent;
	desc->plus = NFS_USE_READDIRPLUS(inode);

	my_entry.cookie = my_entry.prev_cookie = 0;
	my_entry.eof = 0;
	my_entry.fh = &fh;
	my_entry.fattr = &fattr;
	nfs_fattr_init(&fattr);
	desc->entry = &my_entry;

	nfs_block_sillyrename(dentry);
	res = nfs_revalidate_mapping_nolock(inode, filp->f_mapping);
	if (res < 0)
		goto out;

	while(!desc->entry->eof) {
		res = readdir_search_pagecache(desc);

		if (res == -EBADCOOKIE) {
			/* This means either end of directory */
			if (*desc->dir_cookie && desc->entry->cookie != *desc->dir_cookie) {
				/* Or that the server has 'lost' a cookie */
				res = uncached_readdir(desc, dirent, filldir);
				if (res >= 0)
					continue;
			}
			res = 0;
	desc->file = file;
	desc->ctx = ctx;
	desc->dir_cookie = &dir_ctx->dir_cookie;
	desc->decode = NFS_PROTO(inode)->decode_dirent;
	desc->plus = nfs_use_readdirplus(inode, ctx) ? 1 : 0;

	nfs_block_sillyrename(dentry);
	if (ctx->pos == 0 || nfs_attribute_cache_expired(inode))
		res = nfs_revalidate_mapping(inode, file->f_mapping);
	if (res < 0)
		goto out;

	do {
		res = readdir_search_pagecache(desc);

		if (res == -EBADCOOKIE) {
			res = 0;
			/* This means either end of directory */
			if (*desc->dir_cookie && !desc->eof) {
				/* Or that the server has 'lost' a cookie */
				res = uncached_readdir(desc);
				if (res == 0)
					continue;
			}
			break;
		}
		if (res == -ETOOSMALL && desc->plus) {
			clear_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(inode)->flags);
			nfs_zap_caches(inode);
			desc->plus = 0;
			desc->entry->eof = 0;
			desc->page_index = 0;
			desc->plus = 0;
			desc->eof = 0;
			continue;
		}
		if (res < 0)
			break;

		res = nfs_do_filldir(desc, dirent, filldir);
		if (res < 0) {
			res = 0;
			break;
		}
	}
		res = nfs_do_filldir(desc);
		unlock_page(desc->page);
		cache_page_release(desc);
		if (res < 0)
			break;
	} while (!desc->eof);
out:
	nfs_unblock_sillyrename(dentry);
	if (res > 0)
		res = 0;
	dfprintk(FILE, "NFS: readdir(%s/%s) returns %ld\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,
			res);
	return res;
}

static loff_t nfs_llseek_dir(struct file *filp, loff_t offset, int origin)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;

	dfprintk(FILE, "NFS: llseek dir(%s/%s, %lld, %d)\n",
			dentry->d_parent->d_name.name,
			dentry->d_name.name,
			offset, origin);

	mutex_lock(&inode->i_mutex);
	switch (origin) {
	dfprintk(FILE, "NFS: readdir(%pD2) returns %d\n", file, res);
	return res;
}

static loff_t nfs_llseek_dir(struct file *filp, loff_t offset, int whence)
{
	struct inode *inode = file_inode(filp);
	struct nfs_open_dir_context *dir_ctx = filp->private_data;

	dfprintk(FILE, "NFS: llseek dir(%pD2, %lld, %d)\n",
			filp, offset, whence);

	mutex_lock(&inode->i_mutex);
	switch (whence) {
		case 1:
			offset += filp->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			offset = -EINVAL;
			goto out;
	}
	if (offset != filp->f_pos) {
		filp->f_pos = offset;
		nfs_file_open_context(filp)->dir_cookie = 0;
		dir_ctx->dir_cookie = 0;
		dir_ctx->duped = 0;
	}
out:
	mutex_unlock(&inode->i_mutex);
	return offset;
}

/*
 * All directory operations under NFS are synchronous, so fsync()
 * is a dummy operation.
 */
static int nfs_fsync_dir(struct file *filp, struct dentry *dentry, int datasync)
{
	dfprintk(FILE, "NFS: fsync dir(%s/%s) datasync %d\n",
			dentry->d_parent->d_name.name, dentry->d_name.name,
			datasync);

	nfs_inc_stats(dentry->d_inode, NFSIOS_VFSFSYNC);
static int nfs_fsync_dir(struct file *filp, loff_t start, loff_t end,
			 int datasync)
{
	struct inode *inode = file_inode(filp);

	dfprintk(FILE, "NFS: fsync dir(%pD2) datasync %d\n", filp, datasync);

	mutex_lock(&inode->i_mutex);
	nfs_inc_stats(inode, NFSIOS_VFSFSYNC);
	mutex_unlock(&inode->i_mutex);
	return 0;
}

/**
 * nfs_force_lookup_revalidate - Mark the directory as having changed
 * @dir - pointer to directory inode
 *
 * This forces the revalidation code in nfs_lookup_revalidate() to do a
 * full lookup on all child dentries of 'dir' whenever a change occurs
 * on the server that might have invalidated our dcache.
 *
 * The caller should be holding dir->i_lock
 */
void nfs_force_lookup_revalidate(struct inode *dir)
{
	NFS_I(dir)->cache_change_attribute = jiffies;
}
	NFS_I(dir)->cache_change_attribute++;
}
EXPORT_SYMBOL_GPL(nfs_force_lookup_revalidate);

/*
 * A check for whether or not the parent directory has changed.
 * In the case it has, we assume that the dentries are untrustworthy
 * and may need to be looked up again.
 */
static int nfs_check_verifier(struct inode *dir, struct dentry *dentry)
{
	if (IS_ROOT(dentry))
		return 1;
	if (!nfs_verify_change_attribute(dir, dentry->d_time))
		return 0;
	/* Revalidate nfsi->cache_change_attribute before we declare a match */
	if (nfs_revalidate_inode(NFS_SERVER(dir), dir) < 0)
 * If rcu_walk prevents us from performing a full check, return 0.
 */
static int nfs_check_verifier(struct inode *dir, struct dentry *dentry,
			      int rcu_walk)
{
	int ret;

	if (IS_ROOT(dentry))
		return 1;
	if (NFS_SERVER(dir)->flags & NFS_MOUNT_LOOKUP_CACHE_NONE)
		return 0;
	if (!nfs_verify_change_attribute(dir, dentry->d_time))
		return 0;
	/* Revalidate nfsi->cache_change_attribute before we declare a match */
	if (rcu_walk)
		ret = nfs_revalidate_inode_rcu(NFS_SERVER(dir), dir);
	else
		ret = nfs_revalidate_inode(NFS_SERVER(dir), dir);
	if (ret < 0)
		return 0;
	if (!nfs_verify_change_attribute(dir, dentry->d_time))
		return 0;
	return 1;
}

/*
 * Return the intent data that applies to this particular path component
 *
 * Note that the current set of intents only apply to the very last
 * component of the path.
 * We check for this using LOOKUP_CONTINUE and LOOKUP_PARENT.
 */
static inline unsigned int nfs_lookup_check_intent(struct nameidata *nd, unsigned int mask)
{
	if (nd->flags & (LOOKUP_CONTINUE|LOOKUP_PARENT))
		return 0;
	return nd->flags & mask;
}

/*
 * Use intent information to check whether or not we're going to do
 * an O_EXCL create using this path component.
 */
static int nfs_is_exclusive_create(struct inode *dir, struct nameidata *nd)
{
	if (NFS_PROTO(dir)->version == 2)
		return 0;
	if (nd == NULL || nfs_lookup_check_intent(nd, LOOKUP_CREATE) == 0)
		return 0;
	return (nd->intent.open.flags & O_EXCL) != 0;
 * Use intent information to check whether or not we're going to do
 * an O_EXCL create using this path component.
 */
static int nfs_is_exclusive_create(struct inode *dir, unsigned int flags)
{
	if (NFS_PROTO(dir)->version == 2)
		return 0;
	return flags & LOOKUP_EXCL;
}

/*
 * Inode and filehandle revalidation for lookups.
 *
 * We force revalidation in the cases where the VFS sets LOOKUP_REVAL,
 * or if the intent information indicates that we're about to open this
 * particular file and the "nocto" mount flag is not set.
 *
 */
static inline
int nfs_lookup_verify_inode(struct inode *inode, struct nameidata *nd)
{
	struct nfs_server *server = NFS_SERVER(inode);

	if (test_bit(NFS_INO_MOUNTPOINT, &NFS_I(inode)->flags))
		return 0;
	if (nd != NULL) {
		/* VFS wants an on-the-wire revalidation */
		if (nd->flags & LOOKUP_REVAL)
			goto out_force;
		/* This is an open(2) */
		if (nfs_lookup_check_intent(nd, LOOKUP_OPEN) != 0 &&
				!(server->flags & NFS_MOUNT_NOCTO) &&
				(S_ISREG(inode->i_mode) ||
				 S_ISDIR(inode->i_mode)))
			goto out_force;
		return 0;
	}
	return nfs_revalidate_inode(server, inode);
out_force:
	return __nfs_revalidate_inode(server, inode);
static
int nfs_lookup_verify_inode(struct inode *inode, unsigned int flags)
{
	struct nfs_server *server = NFS_SERVER(inode);
	int ret;

	if (IS_AUTOMOUNT(inode))
		return 0;
	/* VFS wants an on-the-wire revalidation */
	if (flags & LOOKUP_REVAL)
		goto out_force;
	/* This is an open(2) */
	if ((flags & LOOKUP_OPEN) && !(server->flags & NFS_MOUNT_NOCTO) &&
	    (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		goto out_force;
out:
	return (inode->i_nlink == 0) ? -ENOENT : 0;
out_force:
	if (flags & LOOKUP_RCU)
		return -ECHILD;
	ret = __nfs_revalidate_inode(server, inode);
	if (ret != 0)
		return ret;
	goto out;
}

/*
 * We judge how long we want to trust negative
 * dentries by looking at the parent inode mtime.
 *
 * If parent mtime has changed, we revalidate, else we wait for a
 * period corresponding to the parent's attribute cache timeout value.
 */
static inline
int nfs_neg_need_reval(struct inode *dir, struct dentry *dentry,
		       struct nameidata *nd)
{
	/* Don't revalidate a negative dentry if we're creating a new file */
	if (nd != NULL && nfs_lookup_check_intent(nd, LOOKUP_CREATE) != 0)
		return 0;
	return !nfs_check_verifier(dir, dentry);
 *
 * If LOOKUP_RCU prevents us from performing a full check, return 1
 * suggesting a reval is needed.
 */
static inline
int nfs_neg_need_reval(struct inode *dir, struct dentry *dentry,
		       unsigned int flags)
{
	/* Don't revalidate a negative dentry if we're creating a new file */
	if (flags & LOOKUP_CREATE)
		return 0;
	if (NFS_SERVER(dir)->flags & NFS_MOUNT_LOOKUP_CACHE_NONEG)
		return 1;
	return !nfs_check_verifier(dir, dentry, flags & LOOKUP_RCU);
}

/*
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that
 * lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 *
 * If the parent directory is seen to have changed, we throw out the
 * cached dentry and do a new lookup.
 */
static int nfs_lookup_revalidate(struct dentry * dentry, struct nameidata *nd)
static int nfs_lookup_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct inode *dir;
	struct inode *inode;
	struct dentry *parent;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	parent = dget_parent(dentry);
	dir = parent->d_inode;
	nfs_inc_stats(dir, NFSIOS_DENTRYREVALIDATE);
	inode = dentry->d_inode;

	if (!inode) {
		if (nfs_neg_need_reval(dir, dentry, nd))
			goto out_bad;
		goto out_valid;
	}

	if (is_bad_inode(inode)) {
		dfprintk(LOOKUPCACHE, "%s: %s/%s has dud inode\n",
				__func__, dentry->d_parent->d_name.name,
				dentry->d_name.name);
		goto out_bad;
	}

	/* Force a full look up iff the parent directory has changed */
	if (!nfs_is_exclusive_create(dir, nd) && nfs_check_verifier(dir, dentry)) {
		if (nfs_lookup_verify_inode(inode, nd))
			goto out_zap_parent;
		goto out_valid;
	}

	if (NFS_STALE(inode))
		goto out_bad;

	error = NFS_PROTO(dir)->lookup(dir, &dentry->d_name, &fhandle, &fattr);
	if (error)
		goto out_bad;
	if (nfs_compare_fh(NFS_FH(inode), &fhandle))
		goto out_bad;
	if ((error = nfs_refresh_inode(inode, &fattr)) != 0)
		goto out_bad;

	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
 out_valid:
	dput(parent);
	dfprintk(LOOKUPCACHE, "NFS: %s(%s/%s) is valid\n",
			__func__, dentry->d_parent->d_name.name,
			dentry->d_name.name);
	struct nfs_fh *fhandle = NULL;
	struct nfs_fattr *fattr = NULL;
	struct nfs4_label *label = NULL;
	int error;

	if (flags & LOOKUP_RCU) {
		parent = ACCESS_ONCE(dentry->d_parent);
		dir = d_inode_rcu(parent);
		if (!dir)
			return -ECHILD;
	} else {
		parent = dget_parent(dentry);
		dir = d_inode(parent);
	}
	nfs_inc_stats(dir, NFSIOS_DENTRYREVALIDATE);
	inode = d_inode(dentry);

	if (!inode) {
		if (nfs_neg_need_reval(dir, dentry, flags)) {
			if (flags & LOOKUP_RCU)
				return -ECHILD;
			goto out_bad;
		}
		goto out_valid_noent;
	}

	if (is_bad_inode(inode)) {
		if (flags & LOOKUP_RCU)
			return -ECHILD;
		dfprintk(LOOKUPCACHE, "%s: %pd2 has dud inode\n",
				__func__, dentry);
		goto out_bad;
	}

	if (NFS_PROTO(dir)->have_delegation(inode, FMODE_READ))
		goto out_set_verifier;

	/* Force a full look up iff the parent directory has changed */
	if (!nfs_is_exclusive_create(dir, flags) &&
	    nfs_check_verifier(dir, dentry, flags & LOOKUP_RCU)) {
		error = nfs_lookup_verify_inode(inode, flags);
		if (error) {
			if (flags & LOOKUP_RCU)
				return -ECHILD;
			if (error == -ESTALE)
				goto out_zap_parent;
			goto out_error;
		}
		goto out_valid;
	}

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (NFS_STALE(inode))
		goto out_bad;

	error = -ENOMEM;
	fhandle = nfs_alloc_fhandle();
	fattr = nfs_alloc_fattr();
	if (fhandle == NULL || fattr == NULL)
		goto out_error;

	label = nfs4_label_alloc(NFS_SERVER(inode), GFP_NOWAIT);
	if (IS_ERR(label))
		goto out_error;

	trace_nfs_lookup_revalidate_enter(dir, dentry, flags);
	error = NFS_PROTO(dir)->lookup(dir, &dentry->d_name, fhandle, fattr, label);
	trace_nfs_lookup_revalidate_exit(dir, dentry, flags, error);
	if (error == -ESTALE || error == -ENOENT)
		goto out_bad;
	if (error)
		goto out_error;
	if (nfs_compare_fh(NFS_FH(inode), fhandle))
		goto out_bad;
	if ((error = nfs_refresh_inode(inode, fattr)) != 0)
		goto out_bad;

	nfs_setsecurity(inode, fattr, label);

	nfs_free_fattr(fattr);
	nfs_free_fhandle(fhandle);
	nfs4_label_free(label);

out_set_verifier:
	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
 out_valid:
	/* Success: notify readdir to use READDIRPLUS */
	nfs_advise_use_readdirplus(dir);
 out_valid_noent:
	if (flags & LOOKUP_RCU) {
		if (parent != ACCESS_ONCE(dentry->d_parent))
			return -ECHILD;
	} else
		dput(parent);
	dfprintk(LOOKUPCACHE, "NFS: %s(%pd2) is valid\n",
			__func__, dentry);
	return 1;
out_zap_parent:
	nfs_zap_caches(dir);
 out_bad:
	WARN_ON(flags & LOOKUP_RCU);
	nfs_free_fattr(fattr);
	nfs_free_fhandle(fhandle);
	nfs4_label_free(label);
	nfs_mark_for_revalidate(dir);
	if (inode && S_ISDIR(inode->i_mode)) {
		/* Purge readdir caches. */
		nfs_zap_caches(inode);
		/* If we have submounts, don't unhash ! */
		if (have_submounts(dentry))
			goto out_valid;
		shrink_dcache_parent(dentry);
	}
	d_drop(dentry);
	dput(parent);
	dfprintk(LOOKUPCACHE, "NFS: %s(%s/%s) is invalid\n",
			__func__, dentry->d_parent->d_name.name,
			dentry->d_name.name);
	return 0;
		/*
		 * We can't d_drop the root of a disconnected tree:
		 * its d_hash is on the s_anon list and d_drop() would hide
		 * it from shrink_dcache_for_unmount(), leading to busy
		 * inodes on unmount and further oopses.
		 */
		if (IS_ROOT(dentry))
			goto out_valid;
	}
	dput(parent);
	dfprintk(LOOKUPCACHE, "NFS: %s(%pd2) is invalid\n",
			__func__, dentry);
	return 0;
out_error:
	WARN_ON(flags & LOOKUP_RCU);
	nfs_free_fattr(fattr);
	nfs_free_fhandle(fhandle);
	nfs4_label_free(label);
	dput(parent);
	dfprintk(LOOKUPCACHE, "NFS: %s(%pd2) lookup returned error %d\n",
			__func__, dentry, error);
	return error;
}

/*
 * A weaker form of d_revalidate for revalidating just the d_inode(dentry)
 * when we don't really care about the dentry name. This is called when a
 * pathwalk ends on a dentry that was not found via a normal lookup in the
 * parent dir (e.g.: ".", "..", procfs symlinks or mountpoint traversals).
 *
 * In this situation, we just want to verify that the inode itself is OK
 * since the dentry might have changed on the server.
 */
static int nfs_weak_revalidate(struct dentry *dentry, unsigned int flags)
{
	int error;
	struct inode *inode = d_inode(dentry);

	/*
	 * I believe we can only get a negative dentry here in the case of a
	 * procfs-style symlink. Just assume it's correct for now, but we may
	 * eventually need to do something more here.
	 */
	if (!inode) {
		dfprintk(LOOKUPCACHE, "%s: %pd2 has negative inode\n",
				__func__, dentry);
		return 1;
	}

	if (is_bad_inode(inode)) {
		dfprintk(LOOKUPCACHE, "%s: %pd2 has dud inode\n",
				__func__, dentry);
		return 0;
	}

	error = nfs_lookup_verify_inode(inode, flags);
	dfprintk(LOOKUPCACHE, "NFS: %s: inode %lu is %s\n",
			__func__, inode->i_ino, error ? "invalid" : "valid");
	return !error;
}

/*
 * This is called from dput() when d_count is going to 0.
 */
static int nfs_dentry_delete(struct dentry *dentry)
{
	dfprintk(VFS, "NFS: dentry_delete(%s/%s, %x)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		dentry->d_flags);

	/* Unhash any dentry with a stale inode */
	if (dentry->d_inode != NULL && NFS_STALE(dentry->d_inode))
static int nfs_dentry_delete(const struct dentry *dentry)
{
	dfprintk(VFS, "NFS: dentry_delete(%pd2, %x)\n",
		dentry, dentry->d_flags);

	/* Unhash any dentry with a stale inode */
	if (d_really_is_positive(dentry) && NFS_STALE(d_inode(dentry)))
		return 1;

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		/* Unhash it, so that ->d_iput() would be called */
		return 1;
	}
	if (!(dentry->d_sb->s_flags & MS_ACTIVE)) {
		/* Unhash it, so that ancestors of killed async unlink
		 * files will be cleaned up during umount */
		return 1;
	}
	return 0;

}

static void nfs_drop_nlink(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	if (inode->i_nlink > 0)
		drop_nlink(inode);
/* Ensure that we revalidate inode->i_nlink */
static void nfs_drop_nlink(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	/* drop the inode if we're reasonably sure this is the last link */
	if (inode->i_nlink == 1)
		clear_nlink(inode);
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ATTR;
	spin_unlock(&inode->i_lock);
}

/*
 * Called when the dentry loses inode.
 * We use it to clean up silly-renamed files.
 */
static void nfs_dentry_iput(struct dentry *dentry, struct inode *inode)
{
	if (S_ISDIR(inode->i_mode))
		/* drop any readdir cache as it could easily be old */
		NFS_I(inode)->cache_validity |= NFS_INO_INVALID_DATA;

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		drop_nlink(inode);
		nfs_complete_unlink(dentry, inode);
		nfs_complete_unlink(dentry, inode);
		nfs_drop_nlink(inode);
	}
	iput(inode);
}

struct dentry_operations nfs_dentry_operations = {
	.d_revalidate	= nfs_lookup_revalidate,
	.d_delete	= nfs_dentry_delete,
	.d_iput		= nfs_dentry_iput,
};

static struct dentry *nfs_lookup(struct inode *dir, struct dentry * dentry, struct nameidata *nd)
static void nfs_d_release(struct dentry *dentry)
{
	/* free cached devname value, if it survived that far */
	if (unlikely(dentry->d_fsdata)) {
		if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
			WARN_ON(1);
		else
			kfree(dentry->d_fsdata);
	}
}

const struct dentry_operations nfs_dentry_operations = {
	.d_revalidate	= nfs_lookup_revalidate,
	.d_weak_revalidate	= nfs_weak_revalidate,
	.d_delete	= nfs_dentry_delete,
	.d_iput		= nfs_dentry_iput,
	.d_automount	= nfs_d_automount,
	.d_release	= nfs_d_release,
};
EXPORT_SYMBOL_GPL(nfs_dentry_operations);

struct dentry *nfs_lookup(struct inode *dir, struct dentry * dentry, unsigned int flags)
{
	struct dentry *res;
	struct dentry *parent;
	struct inode *inode = NULL;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	dfprintk(VFS, "NFS: lookup(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	struct nfs_fh *fhandle = NULL;
	struct nfs_fattr *fattr = NULL;
	struct nfs4_label *label = NULL;
	int error;

	dfprintk(VFS, "NFS: lookup(%pd2)\n", dentry);
	nfs_inc_stats(dir, NFSIOS_VFSLOOKUP);

	res = ERR_PTR(-ENAMETOOLONG);
	if (dentry->d_name.len > NFS_SERVER(dir)->namelen)
		goto out;

	res = ERR_PTR(-ENOMEM);
	dentry->d_op = NFS_PROTO(dir)->dentry_ops;

	/*
	 * If we're doing an exclusive create, optimize away the lookup
	 * but don't hash the dentry.
	 */
	if (nfs_is_exclusive_create(dir, nd)) {
	if (nfs_is_exclusive_create(dir, flags)) {
		d_instantiate(dentry, NULL);
		res = NULL;
		goto out;
	}

	parent = dentry->d_parent;
	/* Protect against concurrent sillydeletes */
	nfs_block_sillyrename(parent);
	error = NFS_PROTO(dir)->lookup(dir, &dentry->d_name, &fhandle, &fattr);
	res = ERR_PTR(-ENOMEM);
	fhandle = nfs_alloc_fhandle();
	fattr = nfs_alloc_fattr();
	if (fhandle == NULL || fattr == NULL)
		goto out;

	label = nfs4_label_alloc(NFS_SERVER(dir), GFP_NOWAIT);
	if (IS_ERR(label))
		goto out;

	parent = dentry->d_parent;
	/* Protect against concurrent sillydeletes */
	trace_nfs_lookup_enter(dir, dentry, flags);
	nfs_block_sillyrename(parent);
	error = NFS_PROTO(dir)->lookup(dir, &dentry->d_name, fhandle, fattr, label);
	if (error == -ENOENT)
		goto no_entry;
	if (error < 0) {
		res = ERR_PTR(error);
		goto out_unblock_sillyrename;
	}
	inode = nfs_fhget(dentry->d_sb, &fhandle, &fattr);
	res = (struct dentry *)inode;
	if (IS_ERR(res))
		goto out_unblock_sillyrename;

no_entry:
	res = d_materialise_unique(dentry, inode);
	inode = nfs_fhget(dentry->d_sb, fhandle, fattr, label);
	res = ERR_CAST(inode);
	if (IS_ERR(res))
		goto out_unblock_sillyrename;

	/* Success: notify readdir to use READDIRPLUS */
	nfs_advise_use_readdirplus(dir);

no_entry:
	res = d_splice_alias(inode, dentry);
	if (res != NULL) {
		if (IS_ERR(res))
			goto out_unblock_sillyrename;
		dentry = res;
	}
	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
out_unblock_sillyrename:
	nfs_unblock_sillyrename(parent);
out:
	return res;
}

#ifdef CONFIG_NFS_V4
static int nfs_open_revalidate(struct dentry *, struct nameidata *);

struct dentry_operations nfs4_dentry_operations = {
	.d_revalidate	= nfs_open_revalidate,
	.d_delete	= nfs_dentry_delete,
	.d_iput		= nfs_dentry_iput,
};

/*
 * Use intent information to determine whether we need to substitute
 * the NFSv4-style stateful OPEN for the LOOKUP call
 */
static int is_atomic_open(struct inode *dir, struct nameidata *nd)
{
	if (nd == NULL || nfs_lookup_check_intent(nd, LOOKUP_OPEN) == 0)
		return 0;
	/* NFS does not (yet) have a stateful open for directories */
	if (nd->flags & LOOKUP_DIRECTORY)
		return 0;
	/* Are we trying to write to a read only partition? */
	if (__mnt_is_readonly(nd->path.mnt) &&
	    (nd->intent.open.flags & (O_CREAT|O_TRUNC|FMODE_WRITE)))
		return 0;
	return 1;
}

static struct dentry *nfs_atomic_lookup(struct inode *dir, struct dentry *dentry, struct nameidata *nd)
{
	struct dentry *res = NULL;
	int error;

	dfprintk(VFS, "NFS: atomic_lookup(%s/%ld), %s\n",
			dir->i_sb->s_id, dir->i_ino, dentry->d_name.name);

	/* Check that we are indeed trying to open this file */
	if (!is_atomic_open(dir, nd))
		goto no_open;

	if (dentry->d_name.len > NFS_SERVER(dir)->namelen) {
		res = ERR_PTR(-ENAMETOOLONG);
		goto out;
	}
	dentry->d_op = NFS_PROTO(dir)->dentry_ops;

	/* Let vfs_create() deal with O_EXCL. Instantiate, but don't hash
	 * the dentry. */
	if (nd->intent.open.flags & O_EXCL) {
		d_instantiate(dentry, NULL);
		goto out;
	}

	/* Open the file on the server */
	res = nfs4_atomic_open(dir, dentry, nd);
	if (IS_ERR(res)) {
		error = PTR_ERR(res);
		switch (error) {
			/* Make a negative dentry */
			case -ENOENT:
				res = NULL;
				goto out;
			/* This turned out not to be a regular file */
			case -EISDIR:
			case -ENOTDIR:
				goto no_open;
			case -ELOOP:
				if (!(nd->intent.open.flags & O_NOFOLLOW))
					goto no_open;
			/* case -EINVAL: */
			default:
				goto out;
		}
	} else if (res != NULL)
		dentry = res;
out:
	return res;
no_open:
	return nfs_lookup(dir, dentry, nd);
}

static int nfs_open_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct dentry *parent = NULL;
	struct inode *inode = dentry->d_inode;
	struct inode *dir;
	int openflags, ret = 0;

	parent = dget_parent(dentry);
	dir = parent->d_inode;
	if (!is_atomic_open(dir, nd))
		goto no_open;
	trace_nfs_lookup_exit(dir, dentry, flags, error);
	nfs4_label_free(label);
out:
	nfs_free_fattr(fattr);
	nfs_free_fhandle(fhandle);
	return res;
}
EXPORT_SYMBOL_GPL(nfs_lookup);

#if IS_ENABLED(CONFIG_NFS_V4)
static int nfs4_lookup_revalidate(struct dentry *, unsigned int);

const struct dentry_operations nfs4_dentry_operations = {
	.d_revalidate	= nfs4_lookup_revalidate,
	.d_weak_revalidate	= nfs_weak_revalidate,
	.d_delete	= nfs_dentry_delete,
	.d_iput		= nfs_dentry_iput,
	.d_automount	= nfs_d_automount,
	.d_release	= nfs_d_release,
};
EXPORT_SYMBOL_GPL(nfs4_dentry_operations);

static fmode_t flags_to_mode(int flags)
{
	fmode_t res = (__force fmode_t)flags & FMODE_EXEC;
	if ((flags & O_ACCMODE) != O_WRONLY)
		res |= FMODE_READ;
	if ((flags & O_ACCMODE) != O_RDONLY)
		res |= FMODE_WRITE;
	return res;
}

static struct nfs_open_context *create_nfs_open_context(struct dentry *dentry, int open_flags)
{
	return alloc_nfs_open_context(dentry, flags_to_mode(open_flags));
}

static int do_open(struct inode *inode, struct file *filp)
{
	nfs_fscache_open_file(inode, filp);
	return 0;
}

static int nfs_finish_open(struct nfs_open_context *ctx,
			   struct dentry *dentry,
			   struct file *file, unsigned open_flags,
			   int *opened)
{
	int err;

	err = finish_open(file, dentry, do_open, opened);
	if (err)
		goto out;
	nfs_file_set_open_context(file, ctx);

out:
	return err;
}

int nfs_atomic_open(struct inode *dir, struct dentry *dentry,
		    struct file *file, unsigned open_flags,
		    umode_t mode, int *opened)
{
	struct nfs_open_context *ctx;
	struct dentry *res;
	struct iattr attr = { .ia_valid = ATTR_OPEN };
	struct inode *inode;
	unsigned int lookup_flags = 0;
	int err;

	/* Expect a negative dentry */
	BUG_ON(d_inode(dentry));

	dfprintk(VFS, "NFS: atomic_open(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	err = nfs_check_flags(open_flags);
	if (err)
		return err;

	/* NFS only supports OPEN on regular files */
	if ((open_flags & O_DIRECTORY)) {
		if (!d_unhashed(dentry)) {
			/*
			 * Hashed negative dentry with O_DIRECTORY: dentry was
			 * revalidated and is fine, no need to perform lookup
			 * again
			 */
			return -ENOENT;
		}
		lookup_flags = LOOKUP_OPEN|LOOKUP_DIRECTORY;
		goto no_open;
	}

	if (dentry->d_name.len > NFS_SERVER(dir)->namelen)
		return -ENAMETOOLONG;

	if (open_flags & O_CREAT) {
		attr.ia_valid |= ATTR_MODE;
		attr.ia_mode = mode & ~current_umask();
	}
	if (open_flags & O_TRUNC) {
		attr.ia_valid |= ATTR_SIZE;
		attr.ia_size = 0;
	}

	ctx = create_nfs_open_context(dentry, open_flags);
	err = PTR_ERR(ctx);
	if (IS_ERR(ctx))
		goto out;

	trace_nfs_atomic_open_enter(dir, ctx, open_flags);
	nfs_block_sillyrename(dentry->d_parent);
	inode = NFS_PROTO(dir)->open_context(dir, ctx, open_flags, &attr, opened);
	nfs_unblock_sillyrename(dentry->d_parent);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		trace_nfs_atomic_open_exit(dir, ctx, open_flags, err);
		put_nfs_open_context(ctx);
		d_drop(dentry);
		switch (err) {
		case -ENOENT:
			d_add(dentry, NULL);
			nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
			break;
		case -EISDIR:
		case -ENOTDIR:
			goto no_open;
		case -ELOOP:
			if (!(open_flags & O_NOFOLLOW))
				goto no_open;
			break;
			/* case -EINVAL: */
		default:
			break;
		}
		goto out;
	}

	err = nfs_finish_open(ctx, ctx->dentry, file, open_flags, opened);
	trace_nfs_atomic_open_exit(dir, ctx, open_flags, err);
	put_nfs_open_context(ctx);
out:
	return err;

no_open:
	res = nfs_lookup(dir, dentry, lookup_flags);
	err = PTR_ERR(res);
	if (IS_ERR(res))
		goto out;

	return finish_no_open(file, res);
}
EXPORT_SYMBOL_GPL(nfs_atomic_open);

static int nfs4_lookup_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct inode *inode;
	int ret = 0;

	if (!(flags & LOOKUP_OPEN) || (flags & LOOKUP_DIRECTORY))
		goto no_open;
	if (d_mountpoint(dentry))
		goto no_open;
	if (NFS_SB(dentry->d_sb)->caps & NFS_CAP_ATOMIC_OPEN_V1)
		goto no_open;

	inode = d_inode(dentry);

	/* We can't create new files in nfs_open_revalidate(), so we
	 * optimize away revalidation of negative dentries.
	 */
	if (inode == NULL) {
		if (!nfs_neg_need_reval(dir, dentry, nd))
			ret = 1;
		struct dentry *parent;
		struct inode *dir;

		if (flags & LOOKUP_RCU) {
			parent = ACCESS_ONCE(dentry->d_parent);
			dir = d_inode_rcu(parent);
			if (!dir)
				return -ECHILD;
		} else {
			parent = dget_parent(dentry);
			dir = d_inode(parent);
		}
		if (!nfs_neg_need_reval(dir, dentry, flags))
			ret = 1;
		else if (flags & LOOKUP_RCU)
			ret = -ECHILD;
		if (!(flags & LOOKUP_RCU))
			dput(parent);
		else if (parent != ACCESS_ONCE(dentry->d_parent))
			return -ECHILD;
		goto out;
	}

	/* NFS only supports OPEN on regular files */
	if (!S_ISREG(inode->i_mode))
		goto no_open;
	openflags = nd->intent.open.flags;
	/* We cannot do exclusive creation on a positive dentry */
	if ((openflags & (O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL))
		goto no_open;
	/* We can't create new files, or truncate existing ones here */
	openflags &= ~(O_CREAT|O_TRUNC);

	/*
	 * Note: we're not holding inode->i_mutex and so may be racing with
	 * operations that change the directory. We therefore save the
	 * change attribute *before* we do the RPC call.
	 */
	ret = nfs4_open_revalidate(dir, dentry, openflags, nd);
out:
	dput(parent);
	if (!ret)
		d_drop(dentry);
	return ret;
no_open:
	dput(parent);
	if (inode != NULL && nfs_have_delegation(inode, FMODE_READ))
		return 1;
	return nfs_lookup_revalidate(dentry, nd);
}
#endif /* CONFIG_NFSV4 */

static struct dentry *nfs_readdir_lookup(nfs_readdir_descriptor_t *desc)
{
	struct dentry *parent = desc->file->f_path.dentry;
	struct inode *dir = parent->d_inode;
	struct nfs_entry *entry = desc->entry;
	struct dentry *dentry, *alias;
	struct qstr name = {
		.name = entry->name,
		.len = entry->len,
	};
	struct inode *inode;
	unsigned long verf = nfs_save_change_attribute(dir);

	switch (name.len) {
		case 2:
			if (name.name[0] == '.' && name.name[1] == '.')
				return dget_parent(parent);
			break;
		case 1:
			if (name.name[0] == '.')
				return dget(parent);
	}

	spin_lock(&dir->i_lock);
	if (NFS_I(dir)->cache_validity & NFS_INO_INVALID_DATA) {
		spin_unlock(&dir->i_lock);
		return NULL;
	}
	spin_unlock(&dir->i_lock);

	name.hash = full_name_hash(name.name, name.len);
	dentry = d_lookup(parent, &name);
	if (dentry != NULL) {
		/* Is this a positive dentry that matches the readdir info? */
		if (dentry->d_inode != NULL &&
				(NFS_FILEID(dentry->d_inode) == entry->ino ||
				d_mountpoint(dentry))) {
			if (!desc->plus || entry->fh->size == 0)
				return dentry;
			if (nfs_compare_fh(NFS_FH(dentry->d_inode),
						entry->fh) == 0)
				goto out_renew;
		}
		/* No, so d_drop to allow one to be created */
		d_drop(dentry);
		dput(dentry);
	}
	if (!desc->plus || !(entry->fattr->valid & NFS_ATTR_FATTR))
		return NULL;
	if (name.len > NFS_SERVER(dir)->namelen)
		return NULL;
	/* Note: caller is already holding the dir->i_mutex! */
	dentry = d_alloc(parent, &name);
	if (dentry == NULL)
		return NULL;
	dentry->d_op = NFS_PROTO(dir)->dentry_ops;
	inode = nfs_fhget(dentry->d_sb, entry->fh, entry->fattr);
	if (IS_ERR(inode)) {
		dput(dentry);
		return NULL;
	}

	alias = d_materialise_unique(dentry, inode);
	if (alias != NULL) {
		dput(dentry);
		if (IS_ERR(alias))
			return NULL;
		dentry = alias;
	}

out_renew:
	nfs_set_verifier(dentry, verf);
	return dentry;
}

	/* We cannot do exclusive creation on a positive dentry */
	if (flags & LOOKUP_EXCL)
		goto no_open;

	/* Let f_op->open() actually open (and revalidate) the file */
	ret = 1;

out:
	return ret;

no_open:
	return nfs_lookup_revalidate(dentry, flags);
}

#endif /* CONFIG_NFSV4 */

/*
 * Code common to create, mkdir, and mknod.
 */
int nfs_instantiate(struct dentry *dentry, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = parent->d_inode;
				struct nfs_fattr *fattr,
				struct nfs4_label *label)
{
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir = d_inode(parent);
	struct inode *inode;
	int error = -EACCES;

	d_drop(dentry);

	/* We may have been initialized further down */
	if (dentry->d_inode)
		goto out;
	if (fhandle->size == 0) {
		error = NFS_PROTO(dir)->lookup(dir, &dentry->d_name, fhandle, fattr);
	if (d_really_is_positive(dentry))
		goto out;
	if (fhandle->size == 0) {
		error = NFS_PROTO(dir)->lookup(dir, &dentry->d_name, fhandle, fattr, NULL);
		if (error)
			goto out_error;
	}
	nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	if (!(fattr->valid & NFS_ATTR_FATTR)) {
		struct nfs_server *server = NFS_SB(dentry->d_sb);
		error = server->nfs_client->rpc_ops->getattr(server, fhandle, fattr);
		if (error < 0)
			goto out_error;
	}
	inode = nfs_fhget(dentry->d_sb, fhandle, fattr);
		error = server->nfs_client->rpc_ops->getattr(server, fhandle, fattr, NULL);
		if (error < 0)
			goto out_error;
	}
	inode = nfs_fhget(dentry->d_sb, fhandle, fattr, label);
	error = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_error;
	d_add(dentry, inode);
out:
	dput(parent);
	return 0;
out_error:
	nfs_mark_for_revalidate(dir);
	dput(parent);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_instantiate);

/*
 * Following a failed create operation, we drop the dentry rather
 * than retain a negative dentry. This avoids a problem in the event
 * that the operation succeeded on the server, but an error in the
 * reply path made it appear to have failed.
 */
static int nfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	struct iattr attr;
	int error;
	int open_flags = 0;

	dfprintk(VFS, "NFS: create(%s/%ld), %s\n",
			dir->i_sb->s_id, dir->i_ino, dentry->d_name.name);
int nfs_create(struct inode *dir, struct dentry *dentry,
		umode_t mode, bool excl)
{
	struct iattr attr;
	int open_flags = excl ? O_CREAT | O_EXCL : O_CREAT;
	int error;

	dfprintk(VFS, "NFS: create(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	attr.ia_mode = mode;
	attr.ia_valid = ATTR_MODE;

	if ((nd->flags & LOOKUP_CREATE) != 0)
		open_flags = nd->intent.open.flags;

	error = NFS_PROTO(dir)->create(dir, dentry, &attr, open_flags, nd);
	trace_nfs_create_enter(dir, dentry, open_flags);
	error = NFS_PROTO(dir)->create(dir, dentry, &attr, open_flags);
	trace_nfs_create_exit(dir, dentry, open_flags, error);
	if (error != 0)
		goto out_err;
	return 0;
out_err:
	d_drop(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_create);

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
static int
nfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t rdev)
int
nfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct iattr attr;
	int status;

	dfprintk(VFS, "NFS: mknod(%s/%ld), %s\n",
			dir->i_sb->s_id, dir->i_ino, dentry->d_name.name);

	if (!new_valid_dev(rdev))
		return -EINVAL;
	dfprintk(VFS, "NFS: mknod(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	attr.ia_mode = mode;
	attr.ia_valid = ATTR_MODE;

	status = NFS_PROTO(dir)->mknod(dir, dentry, &attr, rdev);
	trace_nfs_mknod_enter(dir, dentry);
	status = NFS_PROTO(dir)->mknod(dir, dentry, &attr, rdev);
	trace_nfs_mknod_exit(dir, dentry, status);
	if (status != 0)
		goto out_err;
	return 0;
out_err:
	d_drop(dentry);
	return status;
}
EXPORT_SYMBOL_GPL(nfs_mknod);

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
static int nfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
int nfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct iattr attr;
	int error;

	dfprintk(VFS, "NFS: mkdir(%s/%ld), %s\n",
			dir->i_sb->s_id, dir->i_ino, dentry->d_name.name);
	dfprintk(VFS, "NFS: mkdir(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	attr.ia_valid = ATTR_MODE;
	attr.ia_mode = mode | S_IFDIR;

	error = NFS_PROTO(dir)->mkdir(dir, dentry, &attr);
	trace_nfs_mkdir_enter(dir, dentry);
	error = NFS_PROTO(dir)->mkdir(dir, dentry, &attr);
	trace_nfs_mkdir_exit(dir, dentry, error);
	if (error != 0)
		goto out_err;
	return 0;
out_err:
	d_drop(dentry);
	return error;
}

static void nfs_dentry_handle_enoent(struct dentry *dentry)
{
	if (dentry->d_inode != NULL && !d_unhashed(dentry))
		d_delete(dentry);
}

static int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%s/%ld), %s\n",
			dir->i_sb->s_id, dir->i_ino, dentry->d_name.name);

	error = NFS_PROTO(dir)->rmdir(dir, &dentry->d_name);
	/* Ensure the VFS deletes this inode */
	if (error == 0 && dentry->d_inode != NULL)
		clear_nlink(dentry->d_inode);
	else if (error == -ENOENT)
		nfs_dentry_handle_enoent(dentry);

	return error;
}

static int nfs_sillyrename(struct inode *dir, struct dentry *dentry)
{
	static unsigned int sillycounter;
	const int      fileidsize  = sizeof(NFS_FILEID(dentry->d_inode))*2;
	const int      countersize = sizeof(sillycounter)*2;
	const int      slen        = sizeof(".nfs")+fileidsize+countersize-1;
	char           silly[slen+1];
	struct qstr    qsilly;
	struct dentry *sdentry;
	int            error = -EIO;

	dfprintk(VFS, "NFS: silly-rename(%s/%s, ct=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name, 
		atomic_read(&dentry->d_count));
	nfs_inc_stats(dir, NFSIOS_SILLYRENAME);

	/*
	 * We don't allow a dentry to be silly-renamed twice.
	 */
	error = -EBUSY;
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto out;

	sprintf(silly, ".nfs%*.*Lx",
		fileidsize, fileidsize,
		(unsigned long long)NFS_FILEID(dentry->d_inode));

	/* Return delegation in anticipation of the rename */
	nfs_inode_return_delegation(dentry->d_inode);

	sdentry = NULL;
	do {
		char *suffix = silly + slen - countersize;

		dput(sdentry);
		sillycounter++;
		sprintf(suffix, "%*.*x", countersize, countersize, sillycounter);

		dfprintk(VFS, "NFS: trying to rename %s to %s\n",
				dentry->d_name.name, silly);
		
		sdentry = lookup_one_len(silly, dentry->d_parent, slen);
		/*
		 * N.B. Better to return EBUSY here ... it could be
		 * dangerous to delete the file while it's in use.
		 */
		if (IS_ERR(sdentry))
			goto out;
	} while(sdentry->d_inode != NULL); /* need negative lookup */

	qsilly.name = silly;
	qsilly.len  = strlen(silly);
	if (dentry->d_inode) {
		error = NFS_PROTO(dir)->rename(dir, &dentry->d_name,
				dir, &qsilly);
		nfs_mark_for_revalidate(dentry->d_inode);
	} else
		error = NFS_PROTO(dir)->rename(dir, &dentry->d_name,
				dir, &qsilly);
	if (!error) {
		nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
		d_move(dentry, sdentry);
		error = nfs_async_unlink(dir, dentry);
 		/* If we return 0 we don't unlink */
	}
	dput(sdentry);
out:
	return error;
}
EXPORT_SYMBOL_GPL(nfs_mkdir);

static void nfs_dentry_handle_enoent(struct dentry *dentry)
{
	if (simple_positive(dentry))
		d_delete(dentry);
}

int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%s/%lu), %pd\n",
			dir->i_sb->s_id, dir->i_ino, dentry);

	trace_nfs_rmdir_enter(dir, dentry);
	if (d_really_is_positive(dentry)) {
		nfs_wait_on_sillyrename(dentry);
		error = NFS_PROTO(dir)->rmdir(dir, &dentry->d_name);
		/* Ensure the VFS deletes this inode */
		switch (error) {
		case 0:
			clear_nlink(d_inode(dentry));
			break;
		case -ENOENT:
			nfs_dentry_handle_enoent(dentry);
		}
	} else
		error = NFS_PROTO(dir)->rmdir(dir, &dentry->d_name);
	trace_nfs_rmdir_exit(dir, dentry, error);

	return error;
}
EXPORT_SYMBOL_GPL(nfs_rmdir);

/*
 * Remove a file after making sure there are no pending writes,
 * and after checking that the file has only one user. 
 *
 * We invalidate the attribute cache and free the inode prior to the operation
 * to avoid possible races if the server reuses the inode.
 */
static int nfs_safe_remove(struct dentry *dentry)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct inode *inode = dentry->d_inode;
	int error = -EBUSY;
		
	dfprintk(VFS, "NFS: safe_remove(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);
	struct inode *dir = d_inode(dentry->d_parent);
	struct inode *inode = d_inode(dentry);
	int error = -EBUSY;
		
	dfprintk(VFS, "NFS: safe_remove(%pd2)\n", dentry);

	/* If the dentry was sillyrenamed, we simply call d_delete() */
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		error = 0;
		goto out;
	}

	if (inode != NULL) {
		nfs_inode_return_delegation(inode);
		error = NFS_PROTO(dir)->remove(dir, &dentry->d_name);
		/* The VFS may want to delete this inode */
		if (error == 0)
			nfs_drop_nlink(inode);
		nfs_mark_for_revalidate(inode);
	trace_nfs_remove_enter(dir, dentry);
	if (inode != NULL) {
		NFS_PROTO(inode)->return_delegation(inode);
		error = NFS_PROTO(dir)->remove(dir, &dentry->d_name);
		if (error == 0)
			nfs_drop_nlink(inode);
	} else
		error = NFS_PROTO(dir)->remove(dir, &dentry->d_name);
	if (error == -ENOENT)
		nfs_dentry_handle_enoent(dentry);
	trace_nfs_remove_exit(dir, dentry, error);
out:
	return error;
}

/*  We do silly rename. In case sillyrename() returns -EBUSY, the inode
 *  belongs to an active ".nfs..." file and we return -EBUSY.
 *
 *  If sillyrename() returns 0, we do nothing, otherwise we unlink.
 */
static int nfs_unlink(struct inode *dir, struct dentry *dentry)
int nfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;
	int need_rehash = 0;

	dfprintk(VFS, "NFS: unlink(%s/%ld, %s)\n", dir->i_sb->s_id,
		dir->i_ino, dentry->d_name.name);

	spin_lock(&dcache_lock);
	spin_lock(&dentry->d_lock);
	if (atomic_read(&dentry->d_count) > 1) {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&dcache_lock);
		/* Start asynchronous writeout of the inode */
		write_inode_now(dentry->d_inode, 0);
		error = nfs_sillyrename(dir, dentry);
		return error;
	dfprintk(VFS, "NFS: unlink(%s/%lu, %pd)\n", dir->i_sb->s_id,
		dir->i_ino, dentry);

	trace_nfs_unlink_enter(dir, dentry);
	spin_lock(&dentry->d_lock);
	if (d_count(dentry) > 1) {
		spin_unlock(&dentry->d_lock);
		/* Start asynchronous writeout of the inode */
		write_inode_now(d_inode(dentry), 0);
		error = nfs_sillyrename(dir, dentry);
		goto out;
	}
	if (!d_unhashed(dentry)) {
		__d_drop(dentry);
		need_rehash = 1;
	}
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dcache_lock);
	error = nfs_safe_remove(dentry);
	if (!error || error == -ENOENT) {
		nfs_set_verifier(dentry, nfs_save_change_attribute(dir));
	} else if (need_rehash)
		d_rehash(dentry);
	return error;
}
out:
	trace_nfs_unlink_exit(dir, dentry, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_unlink);

/*
 * To create a symbolic link, most file systems instantiate a new inode,
 * add a page to it containing the path, then write it out to the disk
 * using prepare_write/commit_write.
 *
 * Unfortunately the NFS client can't create the in-core inode first
 * because it needs a file handle to create an in-core inode (see
 * fs/nfs/inode.c:nfs_fhget).  We only have a file handle *after* the
 * symlink request has completed on the server.
 *
 * So instead we allocate a raw page, copy the symname into it, then do
 * the SYMLINK request with the page as the buffer.  If it succeeds, we
 * now have a new file handle and can instantiate an in-core NFS inode
 * and move the raw page into its mapping.
 */
static int nfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct pagevec lru_pvec;
int nfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct page *page;
	char *kaddr;
	struct iattr attr;
	unsigned int pathlen = strlen(symname);
	int error;

	dfprintk(VFS, "NFS: symlink(%s/%ld, %s, %s)\n", dir->i_sb->s_id,
		dir->i_ino, dentry->d_name.name, symname);
	dfprintk(VFS, "NFS: symlink(%s/%lu, %pd, %s)\n", dir->i_sb->s_id,
		dir->i_ino, dentry, symname);

	if (pathlen > PAGE_SIZE)
		return -ENAMETOOLONG;

	attr.ia_mode = S_IFLNK | S_IRWXUGO;
	attr.ia_valid = ATTR_MODE;

	page = alloc_page(GFP_HIGHUSER);
	if (!page)
		return -ENOMEM;

	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(kaddr, symname, pathlen);
	if (pathlen < PAGE_SIZE)
		memset(kaddr + pathlen, 0, PAGE_SIZE - pathlen);
	kunmap_atomic(kaddr, KM_USER0);

	error = NFS_PROTO(dir)->symlink(dir, dentry, page, pathlen, &attr);
	if (error != 0) {
		dfprintk(VFS, "NFS: symlink(%s/%ld, %s, %s) error %d\n",
			dir->i_sb->s_id, dir->i_ino,
			dentry->d_name.name, symname, error);
	kaddr = kmap_atomic(page);
	memcpy(kaddr, symname, pathlen);
	if (pathlen < PAGE_SIZE)
		memset(kaddr + pathlen, 0, PAGE_SIZE - pathlen);
	kunmap_atomic(kaddr);

	trace_nfs_symlink_enter(dir, dentry);
	error = NFS_PROTO(dir)->symlink(dir, dentry, page, pathlen, &attr);
	trace_nfs_symlink_exit(dir, dentry, error);
	if (error != 0) {
		dfprintk(VFS, "NFS: symlink(%s/%lu, %pd, %s) error %d\n",
			dir->i_sb->s_id, dir->i_ino,
			dentry, symname, error);
		d_drop(dentry);
		__free_page(page);
		return error;
	}

	/*
	 * No big deal if we can't add this page to the page cache here.
	 * READLINK will get the missing page from the server if needed.
	 */
	pagevec_init(&lru_pvec, 0);
	if (!add_to_page_cache(page, dentry->d_inode->i_mapping, 0,
							GFP_KERNEL)) {
		pagevec_add(&lru_pvec, page);
		pagevec_lru_add(&lru_pvec);
		SetPageUptodate(page);
		unlock_page(page);
	if (!add_to_page_cache_lru(page, d_inode(dentry)->i_mapping, 0,
							GFP_KERNEL)) {
		SetPageUptodate(page);
		unlock_page(page);
		/*
		 * add_to_page_cache_lru() grabs an extra page refcount.
		 * Drop it here to avoid leaking this page later.
		 */
		page_cache_release(page);
	} else
		__free_page(page);

	return 0;
}

static int 
nfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int error;

	dfprintk(VFS, "NFS: link(%s/%s -> %s/%s)\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		dentry->d_parent->d_name.name, dentry->d_name.name);
EXPORT_SYMBOL_GPL(nfs_symlink);

int
nfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	int error;

	dfprintk(VFS, "NFS: link(%pd2 -> %pd2)\n",
		old_dentry, dentry);

	trace_nfs_link_enter(inode, dir, dentry);
	NFS_PROTO(inode)->return_delegation(inode);

	d_drop(dentry);
	error = NFS_PROTO(dir)->link(inode, dir, &dentry->d_name);
	if (error == 0) {
		atomic_inc(&inode->i_count);
		d_add(dentry, inode);
	}
	return error;
}
		ihold(inode);
		d_add(dentry, inode);
	}
	trace_nfs_link_exit(inode, dir, dentry, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_link);

/*
 * RENAME
 * FIXME: Some nfsds, like the Linux user space nfsd, may generate a
 * different file handle for the same inode after a rename (e.g. when
 * moving to a different directory). A fail-safe method to do so would
 * be to look up old_dir/old_name, create a link to new_dir/new_name and
 * rename the old file using the sillyrename stuff. This way, the original
 * file in old_dir will go away when the last process iput()s the inode.
 *
 * FIXED.
 * 
 * It actually works quite well. One needs to have the possibility for
 * at least one ".nfs..." file in each directory the file ever gets
 * moved or linked to which happens automagically with the new
 * implementation that only depends on the dcache stuff instead of
 * using the inode layer
 *
 * Unfortunately, things are a little more complicated than indicated
 * above. For a cross-directory move, we want to make sure we can get
 * rid of the old inode after the operation.  This means there must be
 * no pending writes (if it's a file), and the use count must be 1.
 * If these conditions are met, we can drop the dentries before doing
 * the rename.
 */
static int nfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct dentry *dentry = NULL, *rehash = NULL;
	int error = -EBUSY;

	/*
	 * To prevent any new references to the target during the rename,
	 * we unhash the dentry and free the inode in advance.
	 */
	if (!d_unhashed(new_dentry)) {
		d_drop(new_dentry);
		rehash = new_dentry;
	}

	dfprintk(VFS, "NFS: rename(%s/%s -> %s/%s, ct=%d)\n",
		 old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		 new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
		 atomic_read(&new_dentry->d_count));

	/*
	 * First check whether the target is busy ... we can't
	 * safely do _any_ rename if the target is in use.
	 *
	 * For files, make a copy of the dentry and then do a 
	 * silly-rename. If the silly-rename succeeds, the
	 * copied dentry is hashed and becomes the new target.
	 */
	if (!new_inode)
		goto go_ahead;
	if (S_ISDIR(new_inode->i_mode)) {
		error = -EISDIR;
		if (!S_ISDIR(old_inode->i_mode))
			goto out;
	} else if (atomic_read(&new_dentry->d_count) > 2) {
		int err;
		/* copy the target dentry's name */
		dentry = d_alloc(new_dentry->d_parent,
				 &new_dentry->d_name);
		if (!dentry)
			goto out;

		/* silly-rename the existing target ... */
		err = nfs_sillyrename(new_dir, new_dentry);
		if (!err) {
			new_dentry = rehash = dentry;
			new_inode = NULL;
			/* instantiate the replacement target */
			d_instantiate(new_dentry, NULL);
		} else if (atomic_read(&new_dentry->d_count) > 1)
			/* dentry still busy? */
			goto out;
	} else
		nfs_drop_nlink(new_inode);

go_ahead:
	/*
	 * ... prune child dentries and writebacks if needed.
	 */
	if (atomic_read(&old_dentry->d_count) > 1) {
		if (S_ISREG(old_inode->i_mode))
			nfs_wb_all(old_inode);
		shrink_dcache_parent(old_dentry);
	}
	nfs_inode_return_delegation(old_inode);

	if (new_inode != NULL) {
		nfs_inode_return_delegation(new_inode);
		d_delete(new_dentry);
	}

	error = NFS_PROTO(old_dir)->rename(old_dir, &old_dentry->d_name,
					   new_dir, &new_dentry->d_name);
int nfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct dentry *dentry = NULL, *rehash = NULL;
	struct rpc_task *task;
	int error = -EBUSY;

	dfprintk(VFS, "NFS: rename(%pd2 -> %pd2, ct=%d)\n",
		 old_dentry, new_dentry,
		 d_count(new_dentry));

	trace_nfs_rename_enter(old_dir, old_dentry, new_dir, new_dentry);
	/*
	 * For non-directories, check whether the target is busy and if so,
	 * make a copy of the dentry and then do a silly-rename. If the
	 * silly-rename succeeds, the copied dentry is hashed and becomes
	 * the new target.
	 */
	if (new_inode && !S_ISDIR(new_inode->i_mode)) {
		/*
		 * To prevent any new references to the target during the
		 * rename, we unhash the dentry in advance.
		 */
		if (!d_unhashed(new_dentry)) {
			d_drop(new_dentry);
			rehash = new_dentry;
		}

		if (d_count(new_dentry) > 2) {
			int err;

			/* copy the target dentry's name */
			dentry = d_alloc(new_dentry->d_parent,
					 &new_dentry->d_name);
			if (!dentry)
				goto out;

			/* silly-rename the existing target ... */
			err = nfs_sillyrename(new_dir, new_dentry);
			if (err)
				goto out;

			new_dentry = dentry;
			rehash = NULL;
			new_inode = NULL;
		}
	}

	NFS_PROTO(old_inode)->return_delegation(old_inode);
	if (new_inode != NULL)
		NFS_PROTO(new_inode)->return_delegation(new_inode);

	task = nfs_async_rename(old_dir, new_dir, old_dentry, new_dentry, NULL);
	if (IS_ERR(task)) {
		error = PTR_ERR(task);
		goto out;
	}

	error = rpc_wait_for_completion_task(task);
	if (error == 0)
		error = task->tk_status;
	rpc_put_task(task);
	nfs_mark_for_revalidate(old_inode);
out:
	if (rehash)
		d_rehash(rehash);
	if (!error) {
	trace_nfs_rename_exit(old_dir, old_dentry,
			new_dir, new_dentry, error);
	if (!error) {
		if (new_inode != NULL)
			nfs_drop_nlink(new_inode);
		d_move(old_dentry, new_dentry);
		nfs_set_verifier(old_dentry,
					nfs_save_change_attribute(new_dir));
	} else if (error == -ENOENT)
		nfs_dentry_handle_enoent(old_dentry);

	/* new dentry created? */
	if (dentry)
		dput(dentry);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_rename);

static DEFINE_SPINLOCK(nfs_access_lru_lock);
static LIST_HEAD(nfs_access_lru_list);
static atomic_long_t nfs_access_nr_entries;

static void nfs_access_free_entry(struct nfs_access_entry *entry)
{
	put_rpccred(entry->cred);
	kfree(entry);
	smp_mb__before_atomic_dec();
	atomic_long_dec(&nfs_access_nr_entries);
	smp_mb__after_atomic_dec();
}

int nfs_access_cache_shrinker(int nr_to_scan, gfp_t gfp_mask)
{
	LIST_HEAD(head);
	struct nfs_inode *nfsi;
	struct nfs_access_entry *cache;

restart:
	spin_lock(&nfs_access_lru_lock);
	list_for_each_entry(nfsi, &nfs_access_lru_list, access_cache_inode_lru) {
		struct rw_semaphore *s_umount;
static unsigned long nfs_access_max_cachesize = ULONG_MAX;
module_param(nfs_access_max_cachesize, ulong, 0644);
MODULE_PARM_DESC(nfs_access_max_cachesize, "NFS access maximum total cache length");

static void nfs_access_free_entry(struct nfs_access_entry *entry)
{
	put_rpccred(entry->cred);
	kfree_rcu(entry, rcu_head);
	smp_mb__before_atomic();
	atomic_long_dec(&nfs_access_nr_entries);
	smp_mb__after_atomic();
}

static void nfs_access_free_list(struct list_head *head)
{
	struct nfs_access_entry *cache;

	while (!list_empty(head)) {
		cache = list_entry(head->next, struct nfs_access_entry, lru);
		list_del(&cache->lru);
		nfs_access_free_entry(cache);
	}
}

static unsigned long
nfs_do_access_cache_scan(unsigned int nr_to_scan)
{
	LIST_HEAD(head);
	struct nfs_inode *nfsi, *next;
	struct nfs_access_entry *cache;
	long freed = 0;

	spin_lock(&nfs_access_lru_lock);
	list_for_each_entry_safe(nfsi, next, &nfs_access_lru_list, access_cache_inode_lru) {
		struct inode *inode;

		if (nr_to_scan-- == 0)
			break;
		s_umount = &nfsi->vfs_inode.i_sb->s_umount;
		if (!down_read_trylock(s_umount))
			continue;
		inode = igrab(&nfsi->vfs_inode);
		if (inode == NULL) {
			up_read(s_umount);
			continue;
		}
		inode = &nfsi->vfs_inode;
		spin_lock(&inode->i_lock);
		if (list_empty(&nfsi->access_cache_entry_lru))
			goto remove_lru_entry;
		cache = list_entry(nfsi->access_cache_entry_lru.next,
				struct nfs_access_entry, lru);
		list_move(&cache->lru, &head);
		rb_erase(&cache->rb_node, &nfsi->access_cache);
		freed++;
		if (!list_empty(&nfsi->access_cache_entry_lru))
			list_move_tail(&nfsi->access_cache_inode_lru,
					&nfs_access_lru_list);
		else {
remove_lru_entry:
			list_del_init(&nfsi->access_cache_inode_lru);
			clear_bit(NFS_INO_ACL_LRU_SET, &nfsi->flags);
		}
		spin_unlock(&inode->i_lock);
		spin_unlock(&nfs_access_lru_lock);
		iput(inode);
		up_read(s_umount);
		goto restart;
	}
	spin_unlock(&nfs_access_lru_lock);
	while (!list_empty(&head)) {
		cache = list_entry(head.next, struct nfs_access_entry, lru);
		list_del(&cache->lru);
		nfs_access_free_entry(cache);
	}
	return (atomic_long_read(&nfs_access_nr_entries) / 100) * sysctl_vfs_cache_pressure;
}

static void __nfs_access_zap_cache(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct rb_root *root_node = &nfsi->access_cache;
	struct rb_node *n, *dispose = NULL;
			smp_mb__before_atomic();
			clear_bit(NFS_INO_ACL_LRU_SET, &nfsi->flags);
			smp_mb__after_atomic();
		}
		spin_unlock(&inode->i_lock);
	}
	spin_unlock(&nfs_access_lru_lock);
	nfs_access_free_list(&head);
	return freed;
}

unsigned long
nfs_access_cache_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	int nr_to_scan = sc->nr_to_scan;
	gfp_t gfp_mask = sc->gfp_mask;

	if ((gfp_mask & GFP_KERNEL) != GFP_KERNEL)
		return SHRINK_STOP;
	return nfs_do_access_cache_scan(nr_to_scan);
}


unsigned long
nfs_access_cache_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return vfs_pressure_ratio(atomic_long_read(&nfs_access_nr_entries));
}

static void
nfs_access_cache_enforce_limit(void)
{
	long nr_entries = atomic_long_read(&nfs_access_nr_entries);
	unsigned long diff;
	unsigned int nr_to_scan;

	if (nr_entries < 0 || nr_entries <= nfs_access_max_cachesize)
		return;
	nr_to_scan = 100;
	diff = nr_entries - nfs_access_max_cachesize;
	if (diff < nr_to_scan)
		nr_to_scan = diff;
	nfs_do_access_cache_scan(nr_to_scan);
}

static void __nfs_access_zap_cache(struct nfs_inode *nfsi, struct list_head *head)
{
	struct rb_root *root_node = &nfsi->access_cache;
	struct rb_node *n;
	struct nfs_access_entry *entry;

	/* Unhook entries from the cache */
	while ((n = rb_first(root_node)) != NULL) {
		entry = rb_entry(n, struct nfs_access_entry, rb_node);
		rb_erase(n, root_node);
		list_del(&entry->lru);
		n->rb_left = dispose;
		dispose = n;
	}
	nfsi->cache_validity &= ~NFS_INO_INVALID_ACCESS;
	spin_unlock(&inode->i_lock);

	/* Now kill them all! */
	while (dispose != NULL) {
		n = dispose;
		dispose = n->rb_left;
		nfs_access_free_entry(rb_entry(n, struct nfs_access_entry, rb_node));
	}
		list_move(&entry->lru, head);
	}
	nfsi->cache_validity &= ~NFS_INO_INVALID_ACCESS;
}

void nfs_access_zap_cache(struct inode *inode)
{
	/* Remove from global LRU init */
	if (test_and_clear_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags)) {
		spin_lock(&nfs_access_lru_lock);
		list_del_init(&NFS_I(inode)->access_cache_inode_lru);
		spin_unlock(&nfs_access_lru_lock);
	}

	spin_lock(&inode->i_lock);
	/* This will release the spinlock */
	__nfs_access_zap_cache(inode);
}
	LIST_HEAD(head);

	if (test_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags) == 0)
		return;
	/* Remove from global LRU init */
	spin_lock(&nfs_access_lru_lock);
	if (test_and_clear_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags))
		list_del_init(&NFS_I(inode)->access_cache_inode_lru);

	spin_lock(&inode->i_lock);
	__nfs_access_zap_cache(NFS_I(inode), &head);
	spin_unlock(&inode->i_lock);
	spin_unlock(&nfs_access_lru_lock);
	nfs_access_free_list(&head);
}
EXPORT_SYMBOL_GPL(nfs_access_zap_cache);

static struct nfs_access_entry *nfs_access_search_rbtree(struct inode *inode, struct rpc_cred *cred)
{
	struct rb_node *n = NFS_I(inode)->access_cache.rb_node;
	struct nfs_access_entry *entry;

	while (n != NULL) {
		entry = rb_entry(n, struct nfs_access_entry, rb_node);

		if (cred < entry->cred)
			n = n->rb_left;
		else if (cred > entry->cred)
			n = n->rb_right;
		else
			return entry;
	}
	return NULL;
}

static int nfs_access_get_cached(struct inode *inode, struct rpc_cred *cred, struct nfs_access_entry *res)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_access_entry *cache;
	int err = -ENOENT;

	spin_lock(&inode->i_lock);
	if (nfsi->cache_validity & NFS_INO_INVALID_ACCESS)
		goto out_zap;
	cache = nfs_access_search_rbtree(inode, cred);
	if (cache == NULL)
		goto out;
	if (!time_in_range(jiffies, cache->jiffies, cache->jiffies + nfsi->attrtimeo))
	if (!nfs_have_delegated_attributes(inode) &&
	    !time_in_range_open(jiffies, cache->jiffies, cache->jiffies + nfsi->attrtimeo))
		goto out_stale;
	res->jiffies = cache->jiffies;
	res->cred = cache->cred;
	res->mask = cache->mask;
	list_move_tail(&cache->lru, &nfsi->access_cache_entry_lru);
	err = 0;
out:
	spin_unlock(&inode->i_lock);
	return err;
out_stale:
	rb_erase(&cache->rb_node, &nfsi->access_cache);
	list_del(&cache->lru);
	spin_unlock(&inode->i_lock);
	nfs_access_free_entry(cache);
	return -ENOENT;
out_zap:
	/* This will release the spinlock */
	__nfs_access_zap_cache(inode);
	return -ENOENT;
}

	spin_unlock(&inode->i_lock);
	nfs_access_zap_cache(inode);
	return -ENOENT;
}

static int nfs_access_get_cached_rcu(struct inode *inode, struct rpc_cred *cred, struct nfs_access_entry *res)
{
	/* Only check the most recently returned cache entry,
	 * but do it without locking.
	 */
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_access_entry *cache;
	int err = -ECHILD;
	struct list_head *lh;

	rcu_read_lock();
	if (nfsi->cache_validity & NFS_INO_INVALID_ACCESS)
		goto out;
	lh = rcu_dereference(nfsi->access_cache_entry_lru.prev);
	cache = list_entry(lh, struct nfs_access_entry, lru);
	if (lh == &nfsi->access_cache_entry_lru ||
	    cred != cache->cred)
		cache = NULL;
	if (cache == NULL)
		goto out;
	if (!nfs_have_delegated_attributes(inode) &&
	    !time_in_range_open(jiffies, cache->jiffies, cache->jiffies + nfsi->attrtimeo))
		goto out;
	res->jiffies = cache->jiffies;
	res->cred = cache->cred;
	res->mask = cache->mask;
	err = 0;
out:
	rcu_read_unlock();
	return err;
}

static void nfs_access_add_rbtree(struct inode *inode, struct nfs_access_entry *set)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct rb_root *root_node = &nfsi->access_cache;
	struct rb_node **p = &root_node->rb_node;
	struct rb_node *parent = NULL;
	struct nfs_access_entry *entry;

	spin_lock(&inode->i_lock);
	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct nfs_access_entry, rb_node);

		if (set->cred < entry->cred)
			p = &parent->rb_left;
		else if (set->cred > entry->cred)
			p = &parent->rb_right;
		else
			goto found;
	}
	rb_link_node(&set->rb_node, parent, p);
	rb_insert_color(&set->rb_node, root_node);
	list_add_tail(&set->lru, &nfsi->access_cache_entry_lru);
	spin_unlock(&inode->i_lock);
	return;
found:
	rb_replace_node(parent, &set->rb_node, root_node);
	list_add_tail(&set->lru, &nfsi->access_cache_entry_lru);
	list_del(&entry->lru);
	spin_unlock(&inode->i_lock);
	nfs_access_free_entry(entry);
}

static void nfs_access_add_cache(struct inode *inode, struct nfs_access_entry *set)
void nfs_access_add_cache(struct inode *inode, struct nfs_access_entry *set)
{
	struct nfs_access_entry *cache = kmalloc(sizeof(*cache), GFP_KERNEL);
	if (cache == NULL)
		return;
	RB_CLEAR_NODE(&cache->rb_node);
	cache->jiffies = set->jiffies;
	cache->cred = get_rpccred(set->cred);
	cache->mask = set->mask;

	nfs_access_add_rbtree(inode, cache);

	/* Update accounting */
	smp_mb__before_atomic_inc();
	atomic_long_inc(&nfs_access_nr_entries);
	smp_mb__after_atomic_inc();

	/* Add inode to global LRU list */
	if (!test_and_set_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags)) {
		spin_lock(&nfs_access_lru_lock);
		list_add_tail(&NFS_I(inode)->access_cache_inode_lru, &nfs_access_lru_list);
		spin_unlock(&nfs_access_lru_lock);
	}
}
	/* The above field assignments must be visible
	 * before this item appears on the lru.  We cannot easily
	 * use rcu_assign_pointer, so just force the memory barrier.
	 */
	smp_wmb();
	nfs_access_add_rbtree(inode, cache);

	/* Update accounting */
	smp_mb__before_atomic();
	atomic_long_inc(&nfs_access_nr_entries);
	smp_mb__after_atomic();

	/* Add inode to global LRU list */
	if (!test_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags)) {
		spin_lock(&nfs_access_lru_lock);
		if (!test_and_set_bit(NFS_INO_ACL_LRU_SET, &NFS_I(inode)->flags))
			list_add_tail(&NFS_I(inode)->access_cache_inode_lru,
					&nfs_access_lru_list);
		spin_unlock(&nfs_access_lru_lock);
	}
	nfs_access_cache_enforce_limit();
}
EXPORT_SYMBOL_GPL(nfs_access_add_cache);

void nfs_access_set_mask(struct nfs_access_entry *entry, u32 access_result)
{
	entry->mask = 0;
	if (access_result & NFS4_ACCESS_READ)
		entry->mask |= MAY_READ;
	if (access_result &
	    (NFS4_ACCESS_MODIFY | NFS4_ACCESS_EXTEND | NFS4_ACCESS_DELETE))
		entry->mask |= MAY_WRITE;
	if (access_result & (NFS4_ACCESS_LOOKUP|NFS4_ACCESS_EXECUTE))
		entry->mask |= MAY_EXEC;
}
EXPORT_SYMBOL_GPL(nfs_access_set_mask);

static int nfs_do_access(struct inode *inode, struct rpc_cred *cred, int mask)
{
	struct nfs_access_entry cache;
	int status;

	status = nfs_access_get_cached(inode, cred, &cache);
	if (status == 0)
	trace_nfs_access_enter(inode);

	status = nfs_access_get_cached_rcu(inode, cred, &cache);
	if (status != 0)
		status = nfs_access_get_cached(inode, cred, &cache);
	if (status == 0)
		goto out_cached;

	status = -ECHILD;
	if (mask & MAY_NOT_BLOCK)
		goto out;

	/* Be clever: ask server to check for all possible rights */
	cache.mask = MAY_EXEC | MAY_WRITE | MAY_READ;
	cache.cred = cred;
	cache.jiffies = jiffies;
	status = NFS_PROTO(inode)->access(inode, &cache);
	if (status != 0)
		return status;
	nfs_access_add_cache(inode, &cache);
out:
	if ((mask & ~cache.mask & (MAY_READ | MAY_WRITE | MAY_EXEC)) == 0)
		return 0;
	return -EACCES;
	if (status != 0) {
		if (status == -ESTALE) {
			nfs_zap_caches(inode);
			if (!S_ISDIR(inode->i_mode))
				set_bit(NFS_INO_STALE, &NFS_I(inode)->flags);
		}
		goto out;
	}
	nfs_access_add_cache(inode, &cache);
out_cached:
	if ((mask & ~cache.mask & (MAY_READ | MAY_WRITE | MAY_EXEC)) != 0)
		status = -EACCES;
out:
	trace_nfs_access_exit(inode, status);
	return status;
}

static int nfs_open_permission_mask(int openflags)
{
	int mask = 0;

	if (openflags & FMODE_READ)
		mask |= MAY_READ;
	if (openflags & FMODE_WRITE)
		mask |= MAY_WRITE;
	if (openflags & FMODE_EXEC)
		mask |= MAY_EXEC;
	if (openflags & __FMODE_EXEC) {
		/* ONLY check exec rights */
		mask = MAY_EXEC;
	} else {
		if ((openflags & O_ACCMODE) != O_WRONLY)
			mask |= MAY_READ;
		if ((openflags & O_ACCMODE) != O_RDONLY)
			mask |= MAY_WRITE;
	}

	return mask;
}

int nfs_may_open(struct inode *inode, struct rpc_cred *cred, int openflags)
{
	return nfs_do_access(inode, cred, nfs_open_permission_mask(openflags));
}
EXPORT_SYMBOL_GPL(nfs_may_open);

static int nfs_execute_ok(struct inode *inode, int mask)
{
	struct nfs_server *server = NFS_SERVER(inode);
	int ret;

	if (mask & MAY_NOT_BLOCK)
		ret = nfs_revalidate_inode_rcu(server, inode);
	else
		ret = nfs_revalidate_inode(server, inode);
	if (ret == 0 && !execute_ok(inode))
		ret = -EACCES;
	return ret;
}

int nfs_permission(struct inode *inode, int mask)
{
	struct rpc_cred *cred;
	int res = 0;

	nfs_inc_stats(inode, NFSIOS_VFSACCESS);

	if ((mask & (MAY_READ | MAY_WRITE | MAY_EXEC)) == 0)
		goto out;
	/* Is this sys_access() ? */
	if (mask & MAY_ACCESS)
	if (mask & (MAY_ACCESS | MAY_CHDIR))
		goto force_lookup;

	switch (inode->i_mode & S_IFMT) {
		case S_IFLNK:
			goto out;
		case S_IFREG:
			/* NFSv4 has atomic_open... */
			if (nfs_server_capable(inode, NFS_CAP_ATOMIC_OPEN)
					&& (mask & MAY_OPEN))
				goto out;
			if ((mask & MAY_OPEN) &&
			   nfs_server_capable(inode, NFS_CAP_ATOMIC_OPEN))
				return 0;
			break;
		case S_IFDIR:
			/*
			 * Optimize away all write operations, since the server
			 * will check permissions when we perform the op.
			 */
			if ((mask & MAY_WRITE) && !(mask & MAY_READ))
				goto out;
	}

force_lookup:
	if (!NFS_PROTO(inode)->access)
		goto out_notsup;

	cred = rpc_lookup_cred();
	if (!IS_ERR(cred)) {
		res = nfs_do_access(inode, cred, mask);
		put_rpccred(cred);
	} else
		res = PTR_ERR(cred);
out:
	dfprintk(VFS, "NFS: permission(%s/%ld), mask=0x%x, res=%d\n",
		inode->i_sb->s_id, inode->i_ino, mask, res);
	return res;
out_notsup:
	res = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (res == 0)
		res = generic_permission(inode, mask, NULL);
	goto out;
}
	/* Always try fast lookups first */
	rcu_read_lock();
	cred = rpc_lookup_cred_nonblock();
	if (!IS_ERR(cred))
		res = nfs_do_access(inode, cred, mask|MAY_NOT_BLOCK);
	else
		res = PTR_ERR(cred);
	rcu_read_unlock();
	if (res == -ECHILD && !(mask & MAY_NOT_BLOCK)) {
		/* Fast lookup failed, try the slow way */
		cred = rpc_lookup_cred();
		if (!IS_ERR(cred)) {
			res = nfs_do_access(inode, cred, mask);
			put_rpccred(cred);
		} else
			res = PTR_ERR(cred);
	}
out:
	if (!res && (mask & MAY_EXEC))
		res = nfs_execute_ok(inode, mask);

	dfprintk(VFS, "NFS: permission(%s/%lu), mask=0x%x, res=%d\n",
		inode->i_sb->s_id, inode->i_ino, mask, res);
	return res;
out_notsup:
	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	res = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (res == 0)
		res = generic_permission(inode, mask);
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_permission);

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
