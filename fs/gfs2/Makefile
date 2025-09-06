obj-$(CONFIG_GFS2_FS) += gfs2.o
gfs2-y := acl.o bmap.o daemon.o dir.o eaops.o eattr.o glock.o \
	glops.o inode.o log.o lops.o locking.o main.o meta_io.o \
	mount.o ops_address.o ops_dentry.o ops_export.o ops_file.o \
	ops_fstype.o ops_inode.o ops_super.o quota.o \
	recovery.o rgrp.o super.o sys.o trans.o util.o

obj-$(CONFIG_GFS2_FS_LOCKING_DLM) += locking/dlm/
ccflags-y := -I$(src)
obj-$(CONFIG_GFS2_FS) += gfs2.o
gfs2-y := acl.o bmap.o dir.o xattr.o glock.o \
	glops.o log.o lops.o main.o meta_io.o \
	aops.o dentry.o export.o file.o \
	ops_fstype.o inode.o quota.o \
	recovery.o rgrp.o super.o sys.o trans.o util.o

gfs2-$(CONFIG_GFS2_FS_LOCKING_DLM) += lock_dlm.o

