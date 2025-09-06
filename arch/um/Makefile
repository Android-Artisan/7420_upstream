#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
# Licensed under the GPL
#

# select defconfig based on actual architecture
ifeq ($(SUBARCH),x86)
  ifeq ($(shell uname -m),x86_64)
        KBUILD_DEFCONFIG := x86_64_defconfig
  else
        KBUILD_DEFCONFIG := i386_defconfig
  endif
else
        KBUILD_DEFCONFIG := $(SUBARCH)_defconfig
endif

ARCH_DIR := arch/um
OS := $(shell uname -s)
# We require bash because the vmlinux link and loader script cpp use bash
# features.
SHELL := /bin/bash

filechk_gen_header = $<

core-y			+= $(ARCH_DIR)/kernel/		\
			   $(ARCH_DIR)/drivers/		\
			   $(ARCH_DIR)/os-$(OS)/

# Have to precede the include because the included Makefiles reference them.
SYMLINK_HEADERS := archparam.h system.h sigcontext.h processor.h ptrace.h \
	module.h vm-flags.h elf.h host_ldt.h
SYMLINK_HEADERS := $(foreach header,$(SYMLINK_HEADERS),include/asm-um/$(header))

# XXX: The "os" symlink is only used by arch/um/include/os.h, which includes
# ../os/include/file.h
#
# These are cleaned up during mrproper. Please DO NOT fix it again, this is
# the Correct Thing(tm) to do!
ARCH_SYMLINKS = include/asm-um/arch $(ARCH_DIR)/include/sysdep $(ARCH_DIR)/os \
	$(SYMLINK_HEADERS) $(ARCH_DIR)/include/uml-config.h

MODE_INCLUDE	+= -I$(srctree)/$(ARCH_DIR)/include/skas

include $(srctree)/$(ARCH_DIR)/Makefile-skas

ARCH_INCLUDE	:= -I$(ARCH_DIR)/include
ifneq ($(KBUILD_SRC),)
ARCH_INCLUDE	+= -I$(srctree)/$(ARCH_DIR)/include
endif
SYS_DIR		:= $(ARCH_DIR)/include/sysdep-$(SUBARCH)
MODE_INCLUDE	+= -I$(srctree)/$(ARCH_DIR)/include/shared/skas

HEADER_ARCH 	:= $(SUBARCH)

ifneq ($(filter $(SUBARCH),x86 x86_64 i386),)
	HEADER_ARCH := x86
endif

ifdef CONFIG_64BIT
	KBUILD_CFLAGS += -mcmodel=large
endif

HOST_DIR := arch/$(HEADER_ARCH)

include $(ARCH_DIR)/Makefile-skas
include $(HOST_DIR)/Makefile.um

core-y += $(HOST_DIR)/um/

SHARED_HEADERS	:= $(ARCH_DIR)/include/shared
ARCH_INCLUDE	:= -I$(srctree)/$(SHARED_HEADERS)
ARCH_INCLUDE	+= -I$(srctree)/$(HOST_DIR)/um/shared
KBUILD_CPPFLAGS += -I$(srctree)/$(HOST_DIR)/um

# -Dvmap=kernel_vmap prevents anything from referencing the libpcap.o symbol so
# named - it's a common symbol in libpcap, so we get a binary which crashes.
#
# Same things for in6addr_loopback and mktime - found in libc. For these two we
# only get link-time error, luckily.
#
# -Dlongjmp=kernel_longjmp prevents anything from referencing the libpthread.a
# embedded copy of longjmp, same thing for setjmp.
#
# These apply to USER_CFLAGS to.

KBUILD_CFLAGS += $(CFLAGS) $(CFLAGS-y) -D__arch_um__ -DSUBARCH=\"$(SUBARCH)\" \
	$(ARCH_INCLUDE) $(MODE_INCLUDE) -Dvmap=kernel_vmap	\
	-Din6addr_loopback=kernel_in6addr_loopback \
	-Din6addr_any=kernel_in6addr_any

KBUILD_AFLAGS += $(ARCH_INCLUDE)

USER_CFLAGS = $(patsubst $(KERNEL_DEFINES),,$(patsubst -D__KERNEL__,,\
	$(patsubst -I%,,$(KBUILD_CFLAGS)))) $(ARCH_INCLUDE) $(MODE_INCLUDE) \
	$(filter -I%,$(CFLAGS)) -D_FILE_OFFSET_BITS=64

include $(srctree)/$(ARCH_DIR)/Makefile-$(SUBARCH)

#This will adjust *FLAGS accordingly to the platform.
include $(srctree)/$(ARCH_DIR)/Makefile-os-$(OS)
KBUILD_CFLAGS += $(CFLAGS) $(CFLAGS-y) -D__arch_um__ \
	$(ARCH_INCLUDE) $(MODE_INCLUDE) -Dvmap=kernel_vmap	\
	-Dlongjmp=kernel_longjmp -Dsetjmp=kernel_setjmp \
	-Din6addr_loopback=kernel_in6addr_loopback \
	-Din6addr_any=kernel_in6addr_any -Dstrrchr=kernel_strrchr

KBUILD_AFLAGS += $(ARCH_INCLUDE)

USER_CFLAGS = $(patsubst $(KERNEL_DEFINES),,$(patsubst -I%,,$(KBUILD_CFLAGS))) \
		$(ARCH_INCLUDE) $(MODE_INCLUDE) $(filter -I%,$(CFLAGS)) \
		-D_FILE_OFFSET_BITS=64 -idirafter $(srctree)/include \
		-idirafter $(obj)/include -D__KERNEL__ -D__UM_HOST__

#This will adjust *FLAGS accordingly to the platform.
include $(ARCH_DIR)/Makefile-os-$(OS)

KBUILD_CPPFLAGS += -I$(srctree)/$(HOST_DIR)/include \
		   -I$(srctree)/$(HOST_DIR)/include/uapi \
		   -I$(HOST_DIR)/include/generated \
		   -I$(HOST_DIR)/include/generated/uapi

# -Derrno=kernel_errno - This turns all kernel references to errno into
# kernel_errno to separate them from the libc errno.  This allows -fno-common
# in KBUILD_CFLAGS.  Otherwise, it would cause ld to complain about the two different
# errnos.
# These apply to kernelspace only.
#
# strip leading and trailing whitespace to make the USER_CFLAGS removal of these
# defines more robust

KERNEL_DEFINES = $(strip -Derrno=kernel_errno -Dsigprocmask=kernel_sigprocmask \
			 -Dmktime=kernel_mktime $(ARCH_KERNEL_DEFINES))
KBUILD_CFLAGS += $(KERNEL_DEFINES)

PHONY += linux

all: linux

linux: vmlinux
	@echo '  LINK $@'
	$(Q)ln -f $< $@

define archhelp
  echo '* linux		- Binary kernel image (./linux) - for backward'
  echo '		   compatibility only, this creates a hard link to the'
  echo '		   real kernel binary, the "vmlinux" binary you'
  echo '		   find in the kernel root.'
endef

ifneq ($(KBUILD_SRC),)
$(shell mkdir -p $(ARCH_DIR) && ln -fsn $(srctree)/$(ARCH_DIR)/Kconfig.$(SUBARCH) $(ARCH_DIR)/Kconfig.arch)
else
$(shell cd $(ARCH_DIR) && ln -sf Kconfig.$(SUBARCH) Kconfig.arch)
endif

archprepare: $(ARCH_SYMLINKS) $(ARCH_DIR)/include/user_constants.h
prepare: $(ARCH_DIR)/include/kern_constants.h
KBUILD_KCONFIG := $(HOST_DIR)/um/Kconfig

archheaders:
	$(Q)$(MAKE) KBUILD_SRC= ARCH=$(HEADER_ARCH) archheaders

archprepare: include/generated/user_constants.h

LINK-$(CONFIG_LD_SCRIPT_STATIC) += -static
LINK-$(CONFIG_LD_SCRIPT_DYN) += $(call cc-option, -no-pie)
LINK-$(CONFIG_LD_SCRIPT_DYN_RPATH) += -Wl,-rpath,/lib

CFLAGS_NO_HARDENING := $(call cc-option, -fno-PIC,) $(call cc-option, -fno-pic,) \
	$(call cc-option, -fno-stack-protector,) \
	$(call cc-option, -fno-stack-protector-all,)

CONFIG_KERNEL_STACK_ORDER ?= 2
STACK_SIZE := $(shell echo $$[ 4096 * (1 << $(CONFIG_KERNEL_STACK_ORDER)) ] )

CPPFLAGS_vmlinux.lds = -U$(SUBARCH) -DSTART=$(START) -DELF_ARCH=$(ELF_ARCH) \
	-DELF_FORMAT="$(ELF_FORMAT)" -DKERNEL_STACK_SIZE=$(STACK_SIZE)
# Options used by linker script
export LDS_START      := $(START)
export LDS_ELF_ARCH   := $(ELF_ARCH)
export LDS_ELF_FORMAT := $(ELF_FORMAT)

# The wrappers will select whether using "malloc" or the kernel allocator.
LINK_WRAPS = -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc

LD_FLAGS_CMDLINE = $(foreach opt,$(LDFLAGS),-Wl,$(opt))

CFLAGS_vmlinux := $(LINK-y) $(LINK_WRAPS) $(LD_FLAGS_CMDLINE)
define cmd_vmlinux__
	$(CC) $(CFLAGS_vmlinux) -o $@ \
	-Wl,-T,$(vmlinux-lds) $(vmlinux-init) \
	-Wl,--start-group $(vmlinux-main) -Wl,--end-group \
	-lutil \
	$(filter-out $(vmlinux-lds) $(vmlinux-init) $(vmlinux-main) vmlinux.o \
	FORCE ,$^) ; rm -f linux
endef

# When cleaning we don't include .config, so we don't include
# TT or skas makefiles and don't clean skas_ptregs.h.
CLEAN_FILES += linux x.i gmon.out $(ARCH_DIR)/include/uml-config.h \
	$(ARCH_DIR)/include/user_constants.h \
	$(ARCH_DIR)/include/kern_constants.h $(ARCH_DIR)/Kconfig.arch

MRPROPER_FILES += $(ARCH_SYMLINKS)
# Used by link-vmlinux.sh which has special support for um link
export CFLAGS_vmlinux := $(LINK-y) $(LINK_WRAPS) $(LD_FLAGS_CMDLINE)

# When cleaning we don't include .config, so we don't include
# TT or skas makefiles and don't clean skas_ptregs.h.
CLEAN_FILES += linux x.i gmon.out

archclean:
	@find . \( -name '*.bb' -o -name '*.bbg' -o -name '*.da' \
		-o -name '*.gcov' \) -type f -print | xargs rm -f

$(SYMLINK_HEADERS):
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p $(objtree)/include/asm-um
	$(Q)ln -fsn $(srctree)/include/asm-um/$(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $@
else
	$(Q)cd $(srctree)/$(dir $@) ; \
	ln -sf $(basename $(notdir $@))-$(SUBARCH)$(suffix $@) $(notdir $@)
endif

include/asm-um/arch:
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)mkdir -p $(objtree)/include/asm-um
	$(Q)ln -fsn $(srctree)/include/asm-$(HEADER_ARCH) include/asm-um/arch
else
	$(Q)cd $(srctree)/include/asm-um && ln -fsn ../asm-$(HEADER_ARCH) arch
endif

$(objtree)/$(ARCH_DIR)/include:
	@echo '  MKDIR $@'
	$(Q)mkdir -p $@

$(ARCH_DIR)/include/sysdep: $(objtree)/$(ARCH_DIR)/include
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(srctree)/$(ARCH_DIR)/include/sysdep-$(SUBARCH) $(ARCH_DIR)/include/sysdep
else
	$(Q)cd $(ARCH_DIR)/include && ln -fsn sysdep-$(SUBARCH) sysdep
endif

$(ARCH_DIR)/os:
	@echo '  SYMLINK $@'
ifneq ($(KBUILD_SRC),)
	$(Q)ln -fsn $(srctree)/$(ARCH_DIR)/os-$(OS) $(ARCH_DIR)/os
else
	$(Q)cd $(ARCH_DIR) && ln -fsn os-$(OS) os
endif

# Generated files
define filechk_umlconfig
	sed 's/ CONFIG/ UML_CONFIG/'
endef

$(ARCH_DIR)/include/uml-config.h : include/linux/autoconf.h
	$(call filechk,umlconfig)

$(ARCH_DIR)/sys-$(SUBARCH)/user-offsets.s: FORCE
	$(Q)$(MAKE) $(build)=$(ARCH_DIR)/sys-$(SUBARCH) $@
# Generated files

$(HOST_DIR)/um/user-offsets.s: __headers FORCE
	$(Q)$(MAKE) $(build)=$(HOST_DIR)/um $@

define filechk_gen-asm-offsets
        (set -e; \
         echo "/*"; \
         echo " * DO NOT MODIFY."; \
         echo " *"; \
         echo " * This file was generated by arch/$(ARCH)/Makefile"; \
         echo " *"; \
         echo " */"; \
         echo ""; \
         sed -ne "/^->/{s:^->\([^ ]*\) [\$$#]*\([^ ]*\) \(.*\):#define \1 \2 /* \3 */:; s:->::; p;}"; \
         echo ""; )
endef

$(ARCH_DIR)/include/user_constants.h: $(ARCH_DIR)/sys-$(SUBARCH)/user-offsets.s
	$(call filechk,gen-asm-offsets)

$(ARCH_DIR)/include/kern_constants.h: $(objtree)/$(ARCH_DIR)/include
	@echo '  SYMLINK $@'
	$(Q)ln -sf ../../../include/asm-um/asm-offsets.h $@

export SUBARCH USER_CFLAGS CFLAGS_NO_HARDENING OS HEADER_ARCH
include/generated/user_constants.h: $(HOST_DIR)/um/user-offsets.s
	$(call filechk,gen-asm-offsets)

export SUBARCH USER_CFLAGS CFLAGS_NO_HARDENING OS DEV_NULL_PATH
