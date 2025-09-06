/*
 *  linux/include/linux/sunrpc/xprtsock.h
 *
 *  Declarations for the RPC transport socket provider.
 */

#ifndef _LINUX_SUNRPC_XPRTSOCK_H
#define _LINUX_SUNRPC_XPRTSOCK_H

#ifdef __KERNEL__

int		init_socket_xprt(void);
void		cleanup_socket_xprt(void);

/*
 * RPC transport identifiers for UDP, TCP
 *
 * To preserve compatibility with the historical use of raw IP protocol
 * id's for transport selection, these are specified with the previous
 * values. No such restriction exists for new transports, except that
 * they may not collide with these values (17 and 6, respectively).
 */
#define XPRT_TRANSPORT_UDP	IPPROTO_UDP
#define XPRT_TRANSPORT_TCP	IPPROTO_TCP

/*
 * RPC slot table sizes for UDP, TCP transports
 */
extern unsigned int xprt_udp_slot_table_entries;
extern unsigned int xprt_tcp_slot_table_entries;

/*
 * Parameters for choosing a free port
 */
extern unsigned int xprt_min_resvport;
extern unsigned int xprt_max_resvport;

#define RPC_MIN_RESVPORT	(1U)
#define RPC_MAX_RESVPORT	(65535U)
#define RPC_DEF_MIN_RESVPORT	(665U)
#define RPC_DEF_MAX_RESVPORT	(1023U)

struct sock_xprt {
	struct rpc_xprt		xprt;

	/*
	 * Network layer
	 */
	struct socket *		sock;
	struct sock *		inet;

	/*
	 * State of TCP reply receive
	 */
	__be32			tcp_fraghdr,
				tcp_xid,
				tcp_calldir;

	u32			tcp_offset,
				tcp_reclen;

	unsigned long		tcp_copied,
				tcp_flags;

	/*
	 * Connection of transports
	 */
	unsigned long		sock_state;
	struct delayed_work	connect_worker;
	struct work_struct	recv_worker;
	struct mutex		recv_mutex;
	struct sockaddr_storage	srcaddr;
	unsigned short		srcport;

	/*
	 * UDP socket buffer size parameters
	 */
	size_t			rcvsize,
				sndsize;

	/*
	 * Saved socket callback addresses
	 */
	void			(*old_data_ready)(struct sock *);
	void			(*old_state_change)(struct sock *);
	void			(*old_write_space)(struct sock *);
	void			(*old_error_report)(struct sock *);
};

/*
 * TCP receive state flags
 */
#define TCP_RCV_LAST_FRAG	(1UL << 0)
#define TCP_RCV_COPY_FRAGHDR	(1UL << 1)
#define TCP_RCV_COPY_XID	(1UL << 2)
#define TCP_RCV_COPY_DATA	(1UL << 3)
#define TCP_RCV_READ_CALLDIR	(1UL << 4)
#define TCP_RCV_COPY_CALLDIR	(1UL << 5)

/*
 * TCP RPC flags
 */
#define TCP_RPC_REPLY		(1UL << 6)

#define XPRT_SOCK_CONNECTING	1U

#endif /* __KERNEL__ */

#endif /* _LINUX_SUNRPC_XPRTSOCK_H */
