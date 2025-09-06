obj-$(CONFIG_IWLCORE)	+= iwlcore.o
iwlcore-objs 		:= iwl-core.o iwl-eeprom.o iwl-hcmd.o iwl-power.o
iwlcore-objs 		+= iwl-rx.o iwl-tx.o iwl-sta.o iwl-calib.o
iwlcore-objs 		+= iwl-scan.o
iwlcore-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-debugfs.o
iwlcore-$(CONFIG_IWLWIFI_LEDS) += iwl-led.o
iwlcore-$(CONFIG_IWLWIFI_RFKILL) += iwl-rfkill.o

obj-$(CONFIG_IWLAGN)	+= iwlagn.o
iwlagn-objs		:= iwl-agn.o iwl-agn-rs.o

iwlagn-$(CONFIG_IWL4965) += iwl-4965.o
iwlagn-$(CONFIG_IWL5000) += iwl-5000.o

obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		:= iwl3945-base.o iwl-3945.o iwl-3945-rs.o
iwl3945-$(CONFIG_IWL3945_LEDS) += iwl-3945-led.o


# common
obj-$(CONFIG_IWLWIFI)	+= iwlwifi.o
iwlwifi-objs		+= iwl-io.o
iwlwifi-objs		+= iwl-drv.o
iwlwifi-objs		+= iwl-debug.o
iwlwifi-objs		+= iwl-notif-wait.o
iwlwifi-objs		+= iwl-eeprom-read.o iwl-eeprom-parse.o
iwlwifi-objs		+= iwl-phy-db.o iwl-nvm-parse.o
iwlwifi-objs		+= pcie/drv.o pcie/rx.o pcie/tx.o pcie/trans.o
iwlwifi-$(CONFIG_IWLDVM) += iwl-1000.o iwl-2000.o iwl-5000.o iwl-6000.o
iwlwifi-$(CONFIG_IWLMVM) += iwl-7000.o iwl-8000.o
iwlwifi-objs		+= iwl-trans.o

iwlwifi-objs += $(iwlwifi-m)

iwlwifi-$(CONFIG_IWLWIFI_DEVICE_TRACING) += iwl-devtrace.o

ccflags-y += -D__CHECK_ENDIAN__ -I$(src)

obj-$(CONFIG_IWLDVM)	+= dvm/
obj-$(CONFIG_IWLMVM)	+= mvm/

CFLAGS_iwl-devtrace.o := -I$(src)
