#
# Makefile for the Linux TIPC layer
#

obj-$(CONFIG_TIPC) := tipc.o

tipc-y	+= addr.o bcast.o bearer.o config.o cluster.o \
	   core.o handler.o link.o discover.o msg.o  \
	   name_distr.o  subscr.o name_table.o net.o  \
	   netlink.o node.o node_subscr.o port.o ref.o  \
	   socket.o user_reg.o zone.o dbg.o eth_media.o

# End of file
tipc-y	+= addr.o bcast.o bearer.o \
	   core.o link.o discover.o msg.o  \
	   name_distr.o  subscr.o name_table.o net.o  \
	   netlink.o netlink_compat.o node.o socket.o eth_media.o \
	   server.o socket.o

tipc-$(CONFIG_TIPC_MEDIA_UDP)	+= udp_media.o
tipc-$(CONFIG_TIPC_MEDIA_IB)	+= ib_media.o
tipc-$(CONFIG_SYSCTL)		+= sysctl.o
