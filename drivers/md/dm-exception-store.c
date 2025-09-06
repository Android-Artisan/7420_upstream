/*
 * dm-exception-store.c
 *
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006 Red Hat GmbH
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006-2008 Red Hat GmbH
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-snap.h"

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>

#define DM_MSG_PREFIX "snapshots"
#define DM_CHUNK_SIZE_DEFAULT_SECTORS 32	/* 16KB */

/*-----------------------------------------------------------------
 * Persistent snapshots, by persistent we mean that the snapshot
 * will survive a reboot.
 *---------------------------------------------------------------*/

/*
 * We need to store a record of which parts of the origin have
 * been copied to the snapshot device.  The snapshot code
 * requires that we copy exception chunks to chunk aligned areas
 * of the COW store.  It makes sense therefore, to store the
 * metadata in chunk size blocks.
 *
 * There is no backward or forward compatibility implemented,
 * snapshots with different disk versions than the kernel will
 * not be usable.  It is expected that "lvcreate" will blank out
 * the start of a fresh COW device before calling the snapshot
 * constructor.
 *
 * The first chunk of the COW device just contains the header.
 * After this there is a chunk filled with exception metadata,
 * followed by as many exception chunks as can fit in the
 * metadata areas.
 *
 * All on disk structures are in little-endian format.  The end
 * of the exceptions info is indicated by an exception with a
 * new_chunk of 0, which is invalid since it would point to the
 * header chunk.
 */

/*
 * Magic for persistent snapshots: "SnAp" - Feeble isn't it.
 */
#define SNAP_MAGIC 0x70416e53

/*
 * The on-disk version of the metadata.
 */
#define SNAPSHOT_DISK_VERSION 1

struct disk_header {
	uint32_t magic;

	/*
	 * Is this snapshot valid.  There is no way of recovering
	 * an invalid snapshot.
	 */
	uint32_t valid;

	/*
	 * Simple, incrementing version. no backward
	 * compatibility.
	 */
	uint32_t version;

	/* In sectors */
	uint32_t chunk_size;
};

struct disk_exception {
	uint64_t old_chunk;
	uint64_t new_chunk;
};

struct commit_callback {
	void (*callback)(void *, int success);
	void *context;
};

/*
 * The top level structure for a persistent exception store.
 */
struct pstore {
	struct dm_snapshot *snap;	/* up pointer to my snapshot */
	int version;
	int valid;
	uint32_t exceptions_per_area;

	/*
	 * Now that we have an asynchronous kcopyd there is no
	 * need for large chunk sizes, so it wont hurt to have a
	 * whole chunks worth of metadata in memory at once.
	 */
	void *area;

	/*
	 * Used to keep track of which metadata area the data in
	 * 'chunk' refers to.
	 */
	uint32_t current_area;

	/*
	 * The next free chunk for an exception.
	 */
	uint32_t next_free;

	/*
	 * The index of next free exception in the current
	 * metadata area.
	 */
	uint32_t current_committed;

	atomic_t pending_count;
	uint32_t callback_count;
	struct commit_callback *callbacks;
	struct dm_io_client *io_client;

	struct workqueue_struct *metadata_wq;
};

static unsigned sectors_to_pages(unsigned sectors)
{
	return DIV_ROUND_UP(sectors, PAGE_SIZE >> 9);
}

static int alloc_area(struct pstore *ps)
{
	int r = -ENOMEM;
	size_t len;

	len = ps->snap->chunk_size << SECTOR_SHIFT;

	/*
	 * Allocate the chunk_size block of memory that will hold
	 * a single metadata area.
	 */
	ps->area = vmalloc(len);
	if (!ps->area)
		return r;

	return 0;
}

static void free_area(struct pstore *ps)
{
	vfree(ps->area);
	ps->area = NULL;
}

struct mdata_req {
	struct dm_io_region *where;
	struct dm_io_request *io_req;
	struct work_struct work;
	int result;
};

static void do_metadata(struct work_struct *work)
{
	struct mdata_req *req = container_of(work, struct mdata_req, work);

	req->result = dm_io(req->io_req, 1, req->where, NULL);
}

/*
 * Read or write a chunk aligned and sized block of data from a device.
 */
static int chunk_io(struct pstore *ps, uint32_t chunk, int rw, int metadata)
{
	struct dm_io_region where = {
		.bdev = ps->snap->cow->bdev,
		.sector = ps->snap->chunk_size * chunk,
		.count = ps->snap->chunk_size,
	};
	struct dm_io_request io_req = {
		.bi_rw = rw,
		.mem.type = DM_IO_VMA,
		.mem.ptr.vma = ps->area,
		.client = ps->io_client,
		.notify.fn = NULL,
	};
	struct mdata_req req;

	if (!metadata)
		return dm_io(&io_req, 1, &where, NULL);

	req.where = &where;
	req.io_req = &io_req;

	/*
	 * Issue the synchronous I/O from a different thread
	 * to avoid generic_make_request recursion.
	 */
	INIT_WORK(&req.work, do_metadata);
	queue_work(ps->metadata_wq, &req.work);
	flush_workqueue(ps->metadata_wq);

	return req.result;
}

/*
 * Read or write a metadata area.  Remembering to skip the first
 * chunk which holds the header.
 */
static int area_io(struct pstore *ps, uint32_t area, int rw)
{
	int r;
	uint32_t chunk;

	/* convert a metadata area index to a chunk index */
	chunk = 1 + ((ps->exceptions_per_area + 1) * area);

	r = chunk_io(ps, chunk, rw, 0);
	if (r)
		return r;

	ps->current_area = area;
	return 0;
}

static int zero_area(struct pstore *ps, uint32_t area)
{
	memset(ps->area, 0, ps->snap->chunk_size << SECTOR_SHIFT);
	return area_io(ps, area, WRITE);
}

static int read_header(struct pstore *ps, int *new_snapshot)
{
	int r;
	struct disk_header *dh;
	chunk_t chunk_size;
	int chunk_size_supplied = 1;

	/*
	 * Use default chunk size (or hardsect_size, if larger) if none supplied
	 */
	if (!ps->snap->chunk_size) {
        	ps->snap->chunk_size = max(DM_CHUNK_SIZE_DEFAULT_SECTORS,
		    bdev_hardsect_size(ps->snap->cow->bdev) >> 9);
		ps->snap->chunk_mask = ps->snap->chunk_size - 1;
		ps->snap->chunk_shift = ffs(ps->snap->chunk_size) - 1;
		chunk_size_supplied = 0;
	}

	ps->io_client = dm_io_client_create(sectors_to_pages(ps->snap->
							     chunk_size));
	if (IS_ERR(ps->io_client))
		return PTR_ERR(ps->io_client);

	r = alloc_area(ps);
	if (r)
		return r;

	r = chunk_io(ps, 0, READ, 1);
	if (r)
		goto bad;

	dh = (struct disk_header *) ps->area;

	if (le32_to_cpu(dh->magic) == 0) {
		*new_snapshot = 1;
		return 0;
	}

	if (le32_to_cpu(dh->magic) != SNAP_MAGIC) {
		DMWARN("Invalid or corrupt snapshot");
		r = -ENXIO;
		goto bad;
	}

	*new_snapshot = 0;
	ps->valid = le32_to_cpu(dh->valid);
	ps->version = le32_to_cpu(dh->version);
	chunk_size = le32_to_cpu(dh->chunk_size);

	if (!chunk_size_supplied || ps->snap->chunk_size == chunk_size)
		return 0;

	DMWARN("chunk size %llu in device metadata overrides "
	       "table chunk size of %llu.",
	       (unsigned long long)chunk_size,
	       (unsigned long long)ps->snap->chunk_size);

	/* We had a bogus chunk_size. Fix stuff up. */
	free_area(ps);

	ps->snap->chunk_size = chunk_size;
	ps->snap->chunk_mask = chunk_size - 1;
	ps->snap->chunk_shift = ffs(chunk_size) - 1;

	r = dm_io_client_resize(sectors_to_pages(ps->snap->chunk_size),
				ps->io_client);
	if (r)
		return r;

	r = alloc_area(ps);
	return r;

bad:
	free_area(ps);
	return r;
}

static int write_header(struct pstore *ps)
{
	struct disk_header *dh;

	memset(ps->area, 0, ps->snap->chunk_size << SECTOR_SHIFT);

	dh = (struct disk_header *) ps->area;
	dh->magic = cpu_to_le32(SNAP_MAGIC);
	dh->valid = cpu_to_le32(ps->valid);
	dh->version = cpu_to_le32(ps->version);
	dh->chunk_size = cpu_to_le32(ps->snap->chunk_size);

	return chunk_io(ps, 0, WRITE, 1);
}

/*
 * Access functions for the disk exceptions, these do the endian conversions.
 */
static struct disk_exception *get_exception(struct pstore *ps, uint32_t index)
{
	BUG_ON(index >= ps->exceptions_per_area);

	return ((struct disk_exception *) ps->area) + index;
}

static void read_exception(struct pstore *ps,
			   uint32_t index, struct disk_exception *result)
{
	struct disk_exception *e = get_exception(ps, index);

	/* copy it */
	result->old_chunk = le64_to_cpu(e->old_chunk);
	result->new_chunk = le64_to_cpu(e->new_chunk);
}

static void write_exception(struct pstore *ps,
			    uint32_t index, struct disk_exception *de)
{
	struct disk_exception *e = get_exception(ps, index);

	/* copy it */
	e->old_chunk = cpu_to_le64(de->old_chunk);
	e->new_chunk = cpu_to_le64(de->new_chunk);
}

/*
 * Registers the exceptions that are present in the current area.
 * 'full' is filled in to indicate if the area has been
 * filled.
 */
static int insert_exceptions(struct pstore *ps, int *full)
{
	int r;
	unsigned int i;
	struct disk_exception de;

	/* presume the area is full */
	*full = 1;

	for (i = 0; i < ps->exceptions_per_area; i++) {
		read_exception(ps, i, &de);

		/*
		 * If the new_chunk is pointing at the start of
		 * the COW device, where the first metadata area
		 * is we know that we've hit the end of the
		 * exceptions.  Therefore the area is not full.
		 */
		if (de.new_chunk == 0LL) {
			ps->current_committed = i;
			*full = 0;
			break;
		}

		/*
		 * Keep track of the start of the free chunks.
		 */
		if (ps->next_free <= de.new_chunk)
			ps->next_free = de.new_chunk + 1;

		/*
		 * Otherwise we add the exception to the snapshot.
		 */
		r = dm_add_exception(ps->snap, de.old_chunk, de.new_chunk);
		if (r)
			return r;
	}

	return 0;
}

static int read_exceptions(struct pstore *ps)
{
	uint32_t area;
	int r, full = 1;

	/*
	 * Keeping reading chunks and inserting exceptions until
	 * we find a partially full area.
	 */
	for (area = 0; full; area++) {
		r = area_io(ps, area, READ);
		if (r)
			return r;

		r = insert_exceptions(ps, &full);
		if (r)
			return r;
	}

	return 0;
}

static struct pstore *get_info(struct exception_store *store)
{
	return (struct pstore *) store->context;
}

static void persistent_fraction_full(struct exception_store *store,
				     sector_t *numerator, sector_t *denominator)
{
	*numerator = get_info(store)->next_free * store->snap->chunk_size;
	*denominator = get_dev_size(store->snap->cow->bdev);
}

static void persistent_destroy(struct exception_store *store)
{
	struct pstore *ps = get_info(store);

	destroy_workqueue(ps->metadata_wq);
	dm_io_client_destroy(ps->io_client);
	vfree(ps->callbacks);
	free_area(ps);
	kfree(ps);
}

static int persistent_read_metadata(struct exception_store *store)
{
	int r, uninitialized_var(new_snapshot);
	struct pstore *ps = get_info(store);

	/*
	 * Read the snapshot header.
	 */
	r = read_header(ps, &new_snapshot);
	if (r)
		return r;

	/*
	 * Now we know correct chunk_size, complete the initialisation.
	 */
	ps->exceptions_per_area = (ps->snap->chunk_size << SECTOR_SHIFT) /
				  sizeof(struct disk_exception);
	ps->callbacks = dm_vcalloc(ps->exceptions_per_area,
			sizeof(*ps->callbacks));
	if (!ps->callbacks)
		return -ENOMEM;

	/*
	 * Do we need to setup a new snapshot ?
	 */
	if (new_snapshot) {
		r = write_header(ps);
		if (r) {
			DMWARN("write_header failed");
			return r;
		}

		r = zero_area(ps, 0);
		if (r) {
			DMWARN("zero_area(0) failed");
			return r;
		}

	} else {
		/*
		 * Sanity checks.
		 */
		if (ps->version != SNAPSHOT_DISK_VERSION) {
			DMWARN("unable to handle snapshot disk version %d",
			       ps->version);
			return -EINVAL;
		}

		/*
		 * Metadata are valid, but snapshot is invalidated
		 */
		if (!ps->valid)
			return 1;

		/*
		 * Read the metadata.
		 */
		r = read_exceptions(ps);
		if (r)
			return r;
	}

	return 0;
}

static int persistent_prepare(struct exception_store *store,
			      struct dm_snap_exception *e)
{
	struct pstore *ps = get_info(store);
	uint32_t stride;
	sector_t size = get_dev_size(store->snap->cow->bdev);

	/* Is there enough room ? */
	if (size < ((ps->next_free + 1) * store->snap->chunk_size))
		return -ENOSPC;

	e->new_chunk = ps->next_free;

	/*
	 * Move onto the next free pending, making sure to take
	 * into account the location of the metadata chunks.
	 */
	stride = (ps->exceptions_per_area + 1);
	if ((++ps->next_free % stride) == 1)
		ps->next_free++;

	atomic_inc(&ps->pending_count);
	return 0;
}

static void persistent_commit(struct exception_store *store,
			      struct dm_snap_exception *e,
			      void (*callback) (void *, int success),
			      void *callback_context)
{
	int r;
	unsigned int i;
	struct pstore *ps = get_info(store);
	struct disk_exception de;
	struct commit_callback *cb;

	de.old_chunk = e->old_chunk;
	de.new_chunk = e->new_chunk;
	write_exception(ps, ps->current_committed++, &de);

	/*
	 * Add the callback to the back of the array.  This code
	 * is the only place where the callback array is
	 * manipulated, and we know that it will never be called
	 * multiple times concurrently.
	 */
	cb = ps->callbacks + ps->callback_count++;
	cb->callback = callback;
	cb->context = callback_context;

	/*
	 * If there are no more exceptions in flight, or we have
	 * filled this metadata area we commit the exceptions to
	 * disk.
	 */
	if (atomic_dec_and_test(&ps->pending_count) ||
	    (ps->current_committed == ps->exceptions_per_area)) {
		r = area_io(ps, ps->current_area, WRITE);
		if (r)
			ps->valid = 0;

		/*
		 * Have we completely filled the current area ?
		 */
		if (ps->current_committed == ps->exceptions_per_area) {
			ps->current_committed = 0;
			r = zero_area(ps, ps->current_area + 1);
			if (r)
				ps->valid = 0;
		}

		for (i = 0; i < ps->callback_count; i++) {
			cb = ps->callbacks + i;
			cb->callback(cb->context, r == 0 ? 1 : 0);
		}

		ps->callback_count = 0;
	}
}

static void persistent_drop(struct exception_store *store)
{
	struct pstore *ps = get_info(store);

	ps->valid = 0;
	if (write_header(ps))
		DMWARN("write header failed");
}

int dm_create_persistent(struct exception_store *store)
{
	struct pstore *ps;

	/* allocate the pstore */
	ps = kmalloc(sizeof(*ps), GFP_KERNEL);
	if (!ps)
		return -ENOMEM;

	ps->snap = store->snap;
	ps->valid = 1;
	ps->version = SNAPSHOT_DISK_VERSION;
	ps->area = NULL;
	ps->next_free = 2;	/* skipping the header and first area */
	ps->current_committed = 0;

	ps->callback_count = 0;
	atomic_set(&ps->pending_count, 0);
	ps->callbacks = NULL;

	ps->metadata_wq = create_singlethread_workqueue("ksnaphd");
	if (!ps->metadata_wq) {
		kfree(ps);
		DMERR("couldn't start header metadata update thread");
		return -ENOMEM;
	}

	store->destroy = persistent_destroy;
	store->read_metadata = persistent_read_metadata;
	store->prepare_exception = persistent_prepare;
	store->commit_exception = persistent_commit;
	store->drop_snapshot = persistent_drop;
	store->fraction_full = persistent_fraction_full;
	store->context = ps;

	return 0;
}

/*-----------------------------------------------------------------
 * Implementation of the store for non-persistent snapshots.
 *---------------------------------------------------------------*/
struct transient_c {
	sector_t next_free;
};

static void transient_destroy(struct exception_store *store)
{
	kfree(store->context);
}

static int transient_read_metadata(struct exception_store *store)
{
	return 0;
}

static int transient_prepare(struct exception_store *store,
			     struct dm_snap_exception *e)
{
	struct transient_c *tc = (struct transient_c *) store->context;
	sector_t size = get_dev_size(store->snap->cow->bdev);

	if (size < (tc->next_free + store->snap->chunk_size))
		return -1;

	e->new_chunk = sector_to_chunk(store->snap, tc->next_free);
	tc->next_free += store->snap->chunk_size;

	return 0;
}

static void transient_commit(struct exception_store *store,
			     struct dm_snap_exception *e,
			     void (*callback) (void *, int success),
			     void *callback_context)
{
	/* Just succeed */
	callback(callback_context, 1);
}

static void transient_fraction_full(struct exception_store *store,
				    sector_t *numerator, sector_t *denominator)
{
	*numerator = ((struct transient_c *) store->context)->next_free;
	*denominator = get_dev_size(store->snap->cow->bdev);
}

int dm_create_transient(struct exception_store *store)
{
	struct transient_c *tc;

	store->destroy = transient_destroy;
	store->read_metadata = transient_read_metadata;
	store->prepare_exception = transient_prepare;
	store->commit_exception = transient_commit;
	store->drop_snapshot = NULL;
	store->fraction_full = transient_fraction_full;

	tc = kmalloc(sizeof(struct transient_c), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	tc->next_free = 0;
	store->context = tc;

	return 0;
#include "dm-exception-store.h"

#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "snapshot exception stores"

static LIST_HEAD(_exception_store_types);
static DEFINE_SPINLOCK(_lock);

static struct dm_exception_store_type *__find_exception_store_type(const char *name)
{
	struct dm_exception_store_type *type;

	list_for_each_entry(type, &_exception_store_types, list)
		if (!strcmp(name, type->name))
			return type;

	return NULL;
}

static struct dm_exception_store_type *_get_exception_store_type(const char *name)
{
	struct dm_exception_store_type *type;

	spin_lock(&_lock);

	type = __find_exception_store_type(name);

	if (type && !try_module_get(type->module))
		type = NULL;

	spin_unlock(&_lock);

	return type;
}

/*
 * get_type
 * @type_name
 *
 * Attempt to retrieve the dm_exception_store_type by name.  If not already
 * available, attempt to load the appropriate module.
 *
 * Exstore modules are named "dm-exstore-" followed by the 'type_name'.
 * Modules may contain multiple types.
 * This function will first try the module "dm-exstore-<type_name>",
 * then truncate 'type_name' on the last '-' and try again.
 *
 * For example, if type_name was "clustered-shared", it would search
 * 'dm-exstore-clustered-shared' then 'dm-exstore-clustered'.
 *
 * 'dm-exception-store-<type_name>' is too long of a name in my
 * opinion, which is why I've chosen to have the files
 * containing exception store implementations be 'dm-exstore-<type_name>'.
 * If you want your module to be autoloaded, you will follow this
 * naming convention.
 *
 * Returns: dm_exception_store_type* on success, NULL on failure
 */
static struct dm_exception_store_type *get_type(const char *type_name)
{
	char *p, *type_name_dup;
	struct dm_exception_store_type *type;

	type = _get_exception_store_type(type_name);
	if (type)
		return type;

	type_name_dup = kstrdup(type_name, GFP_KERNEL);
	if (!type_name_dup) {
		DMERR("No memory left to attempt load for \"%s\"", type_name);
		return NULL;
	}

	while (request_module("dm-exstore-%s", type_name_dup) ||
	       !(type = _get_exception_store_type(type_name))) {
		p = strrchr(type_name_dup, '-');
		if (!p)
			break;
		p[0] = '\0';
	}

	if (!type)
		DMWARN("Module for exstore type \"%s\" not found.", type_name);

	kfree(type_name_dup);

	return type;
}

static void put_type(struct dm_exception_store_type *type)
{
	spin_lock(&_lock);
	module_put(type->module);
	spin_unlock(&_lock);
}

int dm_exception_store_type_register(struct dm_exception_store_type *type)
{
	int r = 0;

	spin_lock(&_lock);
	if (!__find_exception_store_type(type->name))
		list_add(&type->list, &_exception_store_types);
	else
		r = -EEXIST;
	spin_unlock(&_lock);

	return r;
}
EXPORT_SYMBOL(dm_exception_store_type_register);

int dm_exception_store_type_unregister(struct dm_exception_store_type *type)
{
	spin_lock(&_lock);

	if (!__find_exception_store_type(type->name)) {
		spin_unlock(&_lock);
		return -EINVAL;
	}

	list_del(&type->list);

	spin_unlock(&_lock);

	return 0;
}
EXPORT_SYMBOL(dm_exception_store_type_unregister);

static int set_chunk_size(struct dm_exception_store *store,
			  const char *chunk_size_arg, char **error)
{
	unsigned chunk_size;

	if (kstrtouint(chunk_size_arg, 10, &chunk_size)) {
		*error = "Invalid chunk size";
		return -EINVAL;
	}

	if (!chunk_size) {
		store->chunk_size = store->chunk_mask = store->chunk_shift = 0;
		return 0;
	}

	return dm_exception_store_set_chunk_size(store, chunk_size, error);
}

int dm_exception_store_set_chunk_size(struct dm_exception_store *store,
				      unsigned chunk_size,
				      char **error)
{
	/* Check chunk_size is a power of 2 */
	if (!is_power_of_2(chunk_size)) {
		*error = "Chunk size is not a power of 2";
		return -EINVAL;
	}

	/* Validate the chunk size against the device block size */
	if (chunk_size %
	    (bdev_logical_block_size(dm_snap_cow(store->snap)->bdev) >> 9) ||
	    chunk_size %
	    (bdev_logical_block_size(dm_snap_origin(store->snap)->bdev) >> 9)) {
		*error = "Chunk size is not a multiple of device blocksize";
		return -EINVAL;
	}

	if (chunk_size > INT_MAX >> SECTOR_SHIFT) {
		*error = "Chunk size is too high";
		return -EINVAL;
	}

	store->chunk_size = chunk_size;
	store->chunk_mask = chunk_size - 1;
	store->chunk_shift = __ffs(chunk_size);

	return 0;
}

int dm_exception_store_create(struct dm_target *ti, int argc, char **argv,
			      struct dm_snapshot *snap,
			      unsigned *args_used,
			      struct dm_exception_store **store)
{
	int r = 0;
	struct dm_exception_store_type *type = NULL;
	struct dm_exception_store *tmp_store;
	char persistent;

	if (argc < 2) {
		ti->error = "Insufficient exception store arguments";
		return -EINVAL;
	}

	tmp_store = kzalloc(sizeof(*tmp_store), GFP_KERNEL);
	if (!tmp_store) {
		ti->error = "Exception store allocation failed";
		return -ENOMEM;
	}

	persistent = toupper(*argv[0]);
	if (persistent == 'P')
		type = get_type("P");
	else if (persistent == 'N')
		type = get_type("N");
	else {
		ti->error = "Exception store type is not P or N";
		r = -EINVAL;
		goto bad_type;
	}

	if (!type) {
		ti->error = "Exception store type not recognised";
		r = -EINVAL;
		goto bad_type;
	}

	tmp_store->type = type;
	tmp_store->snap = snap;

	r = set_chunk_size(tmp_store, argv[1], &ti->error);
	if (r)
		goto bad;

	r = type->ctr(tmp_store, (strlen(argv[0]) > 1 ? &argv[0][1] : NULL));
	if (r) {
		ti->error = "Exception store type constructor failed";
		goto bad;
	}

	*args_used = 2;
	*store = tmp_store;
	return 0;

bad:
	put_type(type);
bad_type:
	kfree(tmp_store);
	return r;
}
EXPORT_SYMBOL(dm_exception_store_create);

void dm_exception_store_destroy(struct dm_exception_store *store)
{
	store->type->dtr(store);
	put_type(store->type);
	kfree(store);
}
EXPORT_SYMBOL(dm_exception_store_destroy);

int dm_exception_store_init(void)
{
	int r;

	r = dm_transient_snapshot_init();
	if (r) {
		DMERR("Unable to register transient exception store type.");
		goto transient_fail;
	}

	r = dm_persistent_snapshot_init();
	if (r) {
		DMERR("Unable to register persistent exception store type");
		goto persistent_fail;
	}

	return 0;

persistent_fail:
	dm_transient_snapshot_exit();
transient_fail:
	return r;
}

void dm_exception_store_exit(void)
{
	dm_persistent_snapshot_exit();
	dm_transient_snapshot_exit();
}
