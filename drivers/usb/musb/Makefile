#
# for USB OTG silicon based on Mentor Graphics INVENTRA designs
#

musb_hdrc-objs := musb_core.o

obj-$(CONFIG_USB_MUSB_HDRC)	+= musb_hdrc.o

ifeq ($(CONFIG_ARCH_DAVINCI),y)
	musb_hdrc-objs	+= davinci.o
endif

ifeq ($(CONFIG_USB_TUSB6010),y)
	musb_hdrc-objs	+= tusb6010.o
endif

ifeq ($(CONFIG_ARCH_OMAP2430),y)
	musb_hdrc-objs	+= omap2430.o
endif

ifeq ($(CONFIG_ARCH_OMAP3430),y)
	musb_hdrc-objs	+= omap2430.o
endif

ifeq ($(CONFIG_USB_GADGET_MUSB_HDRC),y)
	musb_hdrc-objs		+= musb_gadget_ep0.o musb_gadget.o
endif

ifeq ($(CONFIG_USB_MUSB_HDRC_HCD),y)
	musb_hdrc-objs		+= musb_virthub.o musb_host.o
endif
obj-$(CONFIG_USB_MUSB_HDRC) += musb_hdrc.o

musb_hdrc-y := musb_core.o

musb_hdrc-$(CONFIG_USB_MUSB_HOST)$(CONFIG_USB_MUSB_DUAL_ROLE) += musb_virthub.o musb_host.o
musb_hdrc-$(CONFIG_USB_MUSB_GADGET)$(CONFIG_USB_MUSB_DUAL_ROLE) += musb_gadget_ep0.o musb_gadget.o
musb_hdrc-$(CONFIG_DEBUG_FS)			+= musb_debugfs.o

# Hardware Glue Layer
obj-$(CONFIG_USB_MUSB_OMAP2PLUS)		+= omap2430.o
obj-$(CONFIG_USB_MUSB_AM35X)			+= am35x.o
obj-$(CONFIG_USB_MUSB_DSPS)			+= musb_dsps.o
obj-$(CONFIG_USB_MUSB_TUSB6010)			+= tusb6010.o
obj-$(CONFIG_USB_MUSB_DAVINCI)			+= davinci.o
obj-$(CONFIG_USB_MUSB_DA8XX)			+= da8xx.o
obj-$(CONFIG_USB_MUSB_BLACKFIN)			+= blackfin.o
obj-$(CONFIG_USB_MUSB_UX500)			+= ux500.o
obj-$(CONFIG_USB_MUSB_JZ4740)			+= jz4740.o
obj-$(CONFIG_USB_MUSB_SUNXI)			+= sunxi.o


obj-$(CONFIG_USB_MUSB_AM335X_CHILD)		+= musb_am335x.o

# the kconfig must guarantee that only one of the
# possible I/O schemes will be enabled at a time ...
# PIO only, or DMA (several potential schemes).
# though PIO is always there to back up DMA, and for ep0

ifneq ($(CONFIG_MUSB_PIO_ONLY),y)

  ifeq ($(CONFIG_USB_INVENTRA_DMA),y)
    musb_hdrc-objs		+= musbhsdma.o

  else
    ifeq ($(CONFIG_USB_TI_CPPI_DMA),y)
      musb_hdrc-objs		+= cppi_dma.o

    else
      ifeq ($(CONFIG_USB_TUSB_OMAP_DMA),y)
        musb_hdrc-objs		+= tusb6010_omap.o

      endif
    endif
  endif
endif


################################################################################

# FIXME remove all these extra "-DMUSB_* things, stick to CONFIG_*

ifeq ($(CONFIG_USB_INVENTRA_MUSB_HAS_AHB_ID),y)
	EXTRA_CFLAGS += -DMUSB_AHB_ID
endif

# Debugging

ifeq ($(CONFIG_USB_MUSB_DEBUG),y)
	EXTRA_CFLAGS += -DDEBUG
endif
musb_hdrc-$(CONFIG_USB_INVENTRA_DMA)		+= musbhsdma.o
musb_hdrc-$(CONFIG_USB_TI_CPPI_DMA)		+= cppi_dma.o
musb_hdrc-$(CONFIG_USB_TUSB_OMAP_DMA)		+= tusb6010_omap.o
musb_hdrc-$(CONFIG_USB_UX500_DMA)		+= ux500_dma.o
musb_hdrc-$(CONFIG_USB_TI_CPPI41_DMA)		+= musb_cppi41.o
