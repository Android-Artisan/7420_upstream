#
# S/390 crypto devices
#

ifdef CONFIG_ZCRYPT_MONOLITHIC

z90crypt-objs := zcrypt_mono.o ap_bus.o zcrypt_api.o \
		zcrypt_pcica.o zcrypt_pcicc.o zcrypt_pcixcc.o zcrypt_cex2a.o
obj-$(CONFIG_ZCRYPT) += z90crypt.o

else

ap-objs := ap_bus.o
obj-$(CONFIG_ZCRYPT) += ap.o zcrypt_api.o zcrypt_pcicc.o zcrypt_pcixcc.o
obj-$(CONFIG_ZCRYPT) += zcrypt_pcica.o zcrypt_cex2a.o

endif
ap-objs := ap_bus.o
# zcrypt_api depends on ap
obj-$(CONFIG_ZCRYPT) += ap.o zcrypt_api.o
# msgtype* depend on zcrypt_api
obj-$(CONFIG_ZCRYPT) += zcrypt_msgtype6.o zcrypt_msgtype50.o
# adapter drivers depend on ap, zcrypt_api and msgtype*
obj-$(CONFIG_ZCRYPT) += zcrypt_pcixcc.o zcrypt_cex2a.o zcrypt_cex4.o
