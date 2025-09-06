/*
 * NET		An implementation of the SOCKET network access protocol.
 *		This is the master header file for the Linux NET layer,
 *		or, in plain English: the networking handling part of the
 *		kernel.
 *
 * Version:	@(#)net.h	1.0.3	05/25/93
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_NET_H
#define _LINUX_NET_H

#include <linux/socket.h>
#include <asm/socket.h>

#define NPROTO		AF_MAX

#define SYS_SOCKET	1		/* sys_socket(2)		*/
#define SYS_BIND	2		/* sys_bind(2)			*/
#define SYS_CONNECT	3		/* sys_connect(2)		*/
#define SYS_LISTEN	4		/* sys_listen(2)		*/
#define SYS_ACCEPT	5		/* sys_accept(2)		*/
#define SYS_GETSOCKNAME	6		/* sys_getsockname(2)		*/
#define SYS_GETPEERNAME	7		/* sys_getpeername(2)		*/
#define SYS_SOCKETPAIR	8		/* sys_socketpair(2)		*/
#define SYS_SEND	9		/* sys_send(2)			*/
#define SYS_RECV	10		/* sys_recv(2)			*/
#define SYS_SENDTO	11		/* sys_sendto(2)		*/
#define SYS_RECVFROM	12		/* sys_recvfrom(2)		*/
#define SYS_SHUTDOWN	13		/* sys_shutdown(2)		*/
#define SYS_SETSOCKOPT	14		/* sys_setsockopt(2)		*/
#define SYS_GETSOCKOPT	15		/* sys_getsockopt(2)		*/
#define SYS_SENDMSG	16		/* sys_sendmsg(2)		*/
#define SYS_RECVMSG	17		/* sys_recvmsg(2)		*/
#define SYS_PACCEPT	18		/* sys_paccept(2)		*/

typedef enum {
	SS_FREE = 0,			/* not allocated		*/
	SS_UNCONNECTED,			/* unconnected to any socket	*/
	SS_CONNECTING,			/* in process of connecting	*/
	SS_CONNECTED,			/* connected to socket		*/
	SS_DISCONNECTING		/* in process of disconnecting	*/
} socket_state;

#define __SO_ACCEPTCON	(1 << 16)	/* performed a listen		*/

#ifdef __KERNEL__
#include <linux/stringify.h>
#include <linux/random.h>
#include <linux/wait.h>
#include <linux/fcntl.h>	/* For O_CLOEXEC and O_NONBLOCK */
#include <linux/kmemcheck.h>
#include <linux/rcupdate.h>
#include <linux/once.h>

#include <uapi/linux/net.h>

struct poll_table_struct;
struct pipe_inode_info;
struct inode;
struct net;

#define SOCK_ASYNC_NOSPACE	0
#define SOCK_ASYNC_WAITDATA	1
struct file;
struct net;

/* Historically, SOCKWQ_ASYNC_NOSPACE & SOCKWQ_ASYNC_WAITDATA were located
 * in sock->flags, but moved into sk->sk_wq->flags to be RCU protected.
 * Eventually all flags will be in sk->sk_wq_flags.
 */
#define SOCKWQ_ASYNC_NOSPACE	0
#define SOCKWQ_ASYNC_WAITDATA	1
#define SOCK_NOSPACE		2
#define SOCK_PASSCRED		3
#define SOCK_PASSSEC		4

#ifndef ARCH_HAS_SOCKET_TYPES
/**
 * enum sock_type - Socket types
 * @SOCK_STREAM: stream (connection) socket
 * @SOCK_DGRAM: datagram (conn.less) socket
 * @SOCK_RAW: raw socket
 * @SOCK_RDM: reliably-delivered message
 * @SOCK_SEQPACKET: sequential packet socket
 * @SOCK_DCCP: Datagram Congestion Control Protocol socket
 * @SOCK_PACKET: linux specific way of getting packets at the dev level.
 *		  For writing rarp and other similar things on the user level.
 *
 * When adding some new socket type please
 * grep ARCH_HAS_SOCKET_TYPE include/asm-* /socket.h, at least MIPS
 * overrides this enum for binary compat reasons.
 */
enum sock_type {
	SOCK_STREAM	= 1,
	SOCK_DGRAM	= 2,
	SOCK_RAW	= 3,
	SOCK_RDM	= 4,
	SOCK_SEQPACKET	= 5,
	SOCK_DCCP	= 6,
	SOCK_PACKET	= 10,
};

#define SOCK_MAX (SOCK_PACKET + 1)
/* Mask which covers at least up to SOCK_MASK-1.  The
 * remaining bits are used as flags. */
#define SOCK_TYPE_MASK 0xf

/* Flags for socket, socketpair, paccept */
/* Flags for socket, socketpair, accept4 */
#define SOCK_CLOEXEC	O_CLOEXEC
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK	O_NONBLOCK
#endif

#endif /* ARCH_HAS_SOCKET_TYPES */

enum sock_shutdown_cmd {
	SHUT_RD		= 0,
	SHUT_WR		= 1,
	SHUT_RDWR	= 2,
};

	SHUT_RD,
	SHUT_WR,
	SHUT_RDWR,
};

struct socket_wq {
	/* Note: wait MUST be first field of socket_wq */
	wait_queue_head_t	wait;
	struct fasync_struct	*fasync_list;
	unsigned long		flags; /* %SOCKWQ_ASYNC_NOSPACE, etc */
	struct rcu_head		rcu;
} ____cacheline_aligned_in_smp;

/**
 *  struct socket - general BSD socket
 *  @state: socket state (%SS_CONNECTED, etc)
 *  @type: socket type (%SOCK_STREAM, etc)
 *  @flags: socket flags (%SOCK_ASYNC_NOSPACE, etc)
 *  @ops: protocol specific socket operations
 *  @fasync_list: Asynchronous wake up list
 *  @file: File back pointer for gc
 *  @sk: internal networking protocol agnostic socket representation
 *  @wait: wait queue for several uses
 */
struct socket {
	socket_state		state;
	short			type;
	unsigned long		flags;
	const struct proto_ops	*ops;
	struct fasync_struct	*fasync_list;
	struct file		*file;
	struct sock		*sk;
	wait_queue_head_t	wait;
 *  @flags: socket flags (%SOCK_NOSPACE, etc)
 *  @ops: protocol specific socket operations
 *  @file: File back pointer for gc
 *  @sk: internal networking protocol agnostic socket representation
 *  @wq: wait queue for several uses
 */
struct socket {
	socket_state		state;

	kmemcheck_bitfield_begin(type);
	short			type;
	kmemcheck_bitfield_end(type);

	unsigned long		flags;

	struct socket_wq __rcu	*wq;

	struct file		*file;
	struct sock		*sk;
	const struct proto_ops	*ops;
};

struct vm_area_struct;
struct page;
struct kiocb;
struct sockaddr;
struct msghdr;
struct module;

struct proto_ops {
	int		family;
	struct module	*owner;
	int		(*release)   (struct socket *sock);
	int		(*bind)	     (struct socket *sock,
				      struct sockaddr *myaddr,
				      int sockaddr_len);
	int		(*connect)   (struct socket *sock,
				      struct sockaddr *vaddr,
				      int sockaddr_len, int flags);
	int		(*socketpair)(struct socket *sock1,
				      struct socket *sock2);
	int		(*accept)    (struct socket *sock,
				      struct socket *newsock, int flags);
	int		(*getname)   (struct socket *sock,
				      struct sockaddr *addr,
				      int *sockaddr_len, int peer);
	unsigned int	(*poll)	     (struct file *file, struct socket *sock,
				      struct poll_table_struct *wait);
	int		(*ioctl)     (struct socket *sock, unsigned int cmd,
				      unsigned long arg);
	int	 	(*compat_ioctl) (struct socket *sock, unsigned int cmd,
				      unsigned long arg);
	int		(*listen)    (struct socket *sock, int len);
	int		(*shutdown)  (struct socket *sock, int flags);
	int		(*setsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int optlen);
	int		(*getsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int __user *optlen);
	int		(*compat_setsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int optlen);
	int		(*compat_getsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int __user *optlen);
	int		(*sendmsg)   (struct kiocb *iocb, struct socket *sock,
				      struct msghdr *m, size_t total_len);
	int		(*recvmsg)   (struct kiocb *iocb, struct socket *sock,
				      struct msghdr *m, size_t total_len,
				      int flags);
#ifdef CONFIG_COMPAT
	int	 	(*compat_ioctl) (struct socket *sock, unsigned int cmd,
				      unsigned long arg);
#endif
	int		(*listen)    (struct socket *sock, int len);
	int		(*shutdown)  (struct socket *sock, int flags);
	int		(*setsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, unsigned int optlen);
	int		(*getsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int __user *optlen);
#ifdef CONFIG_COMPAT
	int		(*compat_setsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, unsigned int optlen);
	int		(*compat_getsockopt)(struct socket *sock, int level,
				      int optname, char __user *optval, int __user *optlen);
#endif
	int		(*sendmsg)   (struct socket *sock, struct msghdr *m,
				      size_t total_len);
	/* Notes for implementing recvmsg:
	 * msg->msg_namelen should get updated by the recvmsg handlers
	 * iff msg_name != NULL. It is by default 0 to prevent
	 * returning uninitialized memory to user space.  The recvfrom
	 * handlers can assume that msg.msg_name is either NULL or has
	 * a minimum size of sizeof(struct sockaddr_storage).
	 */
	int		(*recvmsg)   (struct socket *sock, struct msghdr *m,
				      size_t total_len, int flags);
	int		(*mmap)	     (struct file *file, struct socket *sock,
				      struct vm_area_struct * vma);
	ssize_t		(*sendpage)  (struct socket *sock, struct page *page,
				      int offset, size_t size, int flags);
	ssize_t 	(*splice_read)(struct socket *sock,  loff_t *ppos,
				       struct pipe_inode_info *pipe, size_t len, unsigned int flags);
};

struct net_proto_family {
	int		family;
	int		(*create)(struct net *net, struct socket *sock, int protocol);
	int		(*set_peek_off)(struct sock *sk, int val);
};

#define DECLARE_SOCKADDR(type, dst, src)	\
	type dst = ({ __sockaddr_check_size(sizeof(*dst)); (type) src; })

struct net_proto_family {
	int		family;
	int		(*create)(struct net *net, struct socket *sock,
				  int protocol, int kern);
	struct module	*owner;
};

struct iovec;
struct kvec;

enum {
	SOCK_WAKE_IO,
	SOCK_WAKE_WAITD,
	SOCK_WAKE_SPACE,
	SOCK_WAKE_URG,
};

extern int	     sock_wake_async(struct socket *sk, int how, int band);
extern int	     sock_register(const struct net_proto_family *fam);
extern void	     sock_unregister(int family);
extern int	     sock_create(int family, int type, int proto,
				 struct socket **res);
extern int	     sock_create_kern(int family, int type, int proto,
				      struct socket **res);
extern int	     sock_create_lite(int family, int type, int proto,
				      struct socket **res); 
extern void	     sock_release(struct socket *sock);
extern int   	     sock_sendmsg(struct socket *sock, struct msghdr *msg,
				  size_t len);
extern int	     sock_recvmsg(struct socket *sock, struct msghdr *msg,
				  size_t size, int flags);
extern int 	     sock_map_fd(struct socket *sock, int flags);
extern struct socket *sockfd_lookup(int fd, int *err);
#define		     sockfd_put(sock) fput(sock->file)
extern int	     net_ratelimit(void);
extern long	     do_accept(int fd, struct sockaddr __user *upeer_sockaddr,
			       int __user *upeer_addrlen, int flags);

#define net_random()		random32()
#define net_srandom(seed)	srandom32((__force u32)seed)

extern int   	     kernel_sendmsg(struct socket *sock, struct msghdr *msg,
				    struct kvec *vec, size_t num, size_t len);
extern int   	     kernel_recvmsg(struct socket *sock, struct msghdr *msg,
				    struct kvec *vec, size_t num,
				    size_t len, int flags);

extern int kernel_bind(struct socket *sock, struct sockaddr *addr,
		       int addrlen);
extern int kernel_listen(struct socket *sock, int backlog);
extern int kernel_accept(struct socket *sock, struct socket **newsock,
			 int flags);
extern int kernel_connect(struct socket *sock, struct sockaddr *addr,
			  int addrlen, int flags);
extern int kernel_getsockname(struct socket *sock, struct sockaddr *addr,
			      int *addrlen);
extern int kernel_getpeername(struct socket *sock, struct sockaddr *addr,
			      int *addrlen);
extern int kernel_getsockopt(struct socket *sock, int level, int optname,
			     char *optval, int *optlen);
extern int kernel_setsockopt(struct socket *sock, int level, int optname,
			     char *optval, int optlen);
extern int kernel_sendpage(struct socket *sock, struct page *page, int offset,
			   size_t size, int flags);
extern int kernel_sock_ioctl(struct socket *sock, int cmd, unsigned long arg);
extern int kernel_sock_shutdown(struct socket *sock,
				enum sock_shutdown_cmd how);

#ifndef CONFIG_SMP
#define SOCKOPS_WRAPPED(name) name
#define SOCKOPS_WRAP(name, fam)
#else

#define SOCKOPS_WRAPPED(name) __unlocked_##name

#define SOCKCALL_WRAP(name, call, parms, args)		\
static int __lock_##name##_##call  parms		\
{							\
	int ret;					\
	lock_kernel();					\
	ret = __unlocked_##name##_ops.call  args ;\
	unlock_kernel();				\
	return ret;					\
}

#define SOCKCALL_UWRAP(name, call, parms, args)		\
static unsigned int __lock_##name##_##call  parms	\
{							\
	int ret;					\
	lock_kernel();					\
	ret = __unlocked_##name##_ops.call  args ;\
	unlock_kernel();				\
	return ret;					\
}


#define SOCKOPS_WRAP(name, fam)					\
SOCKCALL_WRAP(name, release, (struct socket *sock), (sock))	\
SOCKCALL_WRAP(name, bind, (struct socket *sock, struct sockaddr *uaddr, int addr_len), \
	      (sock, uaddr, addr_len))				\
SOCKCALL_WRAP(name, connect, (struct socket *sock, struct sockaddr * uaddr, \
			      int addr_len, int flags), 	\
	      (sock, uaddr, addr_len, flags))			\
SOCKCALL_WRAP(name, socketpair, (struct socket *sock1, struct socket *sock2), \
	      (sock1, sock2))					\
SOCKCALL_WRAP(name, accept, (struct socket *sock, struct socket *newsock, \
			 int flags), (sock, newsock, flags)) \
SOCKCALL_WRAP(name, getname, (struct socket *sock, struct sockaddr *uaddr, \
			 int *addr_len, int peer), (sock, uaddr, addr_len, peer)) \
SOCKCALL_UWRAP(name, poll, (struct file *file, struct socket *sock, struct poll_table_struct *wait), \
	      (file, sock, wait)) \
SOCKCALL_WRAP(name, ioctl, (struct socket *sock, unsigned int cmd, \
			 unsigned long arg), (sock, cmd, arg)) \
SOCKCALL_WRAP(name, compat_ioctl, (struct socket *sock, unsigned int cmd, \
			 unsigned long arg), (sock, cmd, arg)) \
SOCKCALL_WRAP(name, listen, (struct socket *sock, int len), (sock, len)) \
SOCKCALL_WRAP(name, shutdown, (struct socket *sock, int flags), (sock, flags)) \
SOCKCALL_WRAP(name, setsockopt, (struct socket *sock, int level, int optname, \
			 char __user *optval, int optlen), (sock, level, optname, optval, optlen)) \
SOCKCALL_WRAP(name, getsockopt, (struct socket *sock, int level, int optname, \
			 char __user *optval, int __user *optlen), (sock, level, optname, optval, optlen)) \
SOCKCALL_WRAP(name, sendmsg, (struct kiocb *iocb, struct socket *sock, struct msghdr *m, size_t len), \
	      (iocb, sock, m, len)) \
SOCKCALL_WRAP(name, recvmsg, (struct kiocb *iocb, struct socket *sock, struct msghdr *m, size_t len, int flags), \
	      (iocb, sock, m, len, flags)) \
SOCKCALL_WRAP(name, mmap, (struct file *file, struct socket *sock, struct vm_area_struct *vma), \
	      (file, sock, vma)) \
	      \
static const struct proto_ops name##_ops = {			\
	.family		= fam,				\
	.owner		= THIS_MODULE,			\
	.release	= __lock_##name##_release,	\
	.bind		= __lock_##name##_bind,		\
	.connect	= __lock_##name##_connect,	\
	.socketpair	= __lock_##name##_socketpair,	\
	.accept		= __lock_##name##_accept,	\
	.getname	= __lock_##name##_getname,	\
	.poll		= __lock_##name##_poll,		\
	.ioctl		= __lock_##name##_ioctl,	\
	.compat_ioctl	= __lock_##name##_compat_ioctl,	\
	.listen		= __lock_##name##_listen,	\
	.shutdown	= __lock_##name##_shutdown,	\
	.setsockopt	= __lock_##name##_setsockopt,	\
	.getsockopt	= __lock_##name##_getsockopt,	\
	.sendmsg	= __lock_##name##_sendmsg,	\
	.recvmsg	= __lock_##name##_recvmsg,	\
	.mmap		= __lock_##name##_mmap,		\
};

#endif

int sock_wake_async(struct socket_wq *sk_wq, int how, int band);
int sock_register(const struct net_proto_family *fam);
void sock_unregister(int family);
int __sock_create(struct net *net, int family, int type, int proto,
		  struct socket **res, int kern);
int sock_create(int family, int type, int proto, struct socket **res);
int sock_create_kern(struct net *net, int family, int type, int proto, struct socket **res);
int sock_create_lite(int family, int type, int proto, struct socket **res);
void sock_release(struct socket *sock);
int sock_sendmsg(struct socket *sock, struct msghdr *msg);
int sock_recvmsg(struct socket *sock, struct msghdr *msg, size_t size,
		 int flags);
struct file *sock_alloc_file(struct socket *sock, int flags, const char *dname);
struct socket *sockfd_lookup(int fd, int *err);
struct socket *sock_from_file(struct file *file, int *err);
#define		     sockfd_put(sock) fput(sock->file)
int net_ratelimit(void);

#define net_ratelimited_function(function, ...)			\
do {								\
	if (net_ratelimit())					\
		function(__VA_ARGS__);				\
} while (0)

#define net_emerg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_emerg, fmt, ##__VA_ARGS__)
#define net_alert_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_alert, fmt, ##__VA_ARGS__)
#define net_crit_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_crit, fmt, ##__VA_ARGS__)
#define net_err_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_err, fmt, ##__VA_ARGS__)
#define net_notice_ratelimited(fmt, ...)			\
	net_ratelimited_function(pr_notice, fmt, ##__VA_ARGS__)
#define net_warn_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_warn, fmt, ##__VA_ARGS__)
#define net_info_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_info, fmt, ##__VA_ARGS__)
#if defined(CONFIG_DYNAMIC_DEBUG)
#define net_dbg_ratelimited(fmt, ...)					\
do {									\
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, fmt);			\
	if (unlikely(descriptor.flags & _DPRINTK_FLAGS_PRINT) &&	\
	    net_ratelimit())						\
		__dynamic_pr_debug(&descriptor, pr_fmt(fmt),		\
		                   ##__VA_ARGS__);			\
} while (0)
#elif defined(DEBUG)
#define net_dbg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_debug, fmt, ##__VA_ARGS__)
#else
#define net_dbg_ratelimited(fmt, ...)				\
	do {							\
		if (0)						\
			no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__); \
	} while (0)
#endif

#define net_get_random_once(buf, nbytes)			\
	get_random_once((buf), (nbytes))

int kernel_sendmsg(struct socket *sock, struct msghdr *msg, struct kvec *vec,
		   size_t num, size_t len);
int kernel_recvmsg(struct socket *sock, struct msghdr *msg, struct kvec *vec,
		   size_t num, size_t len, int flags);

int kernel_bind(struct socket *sock, struct sockaddr *addr, int addrlen);
int kernel_listen(struct socket *sock, int backlog);
int kernel_accept(struct socket *sock, struct socket **newsock, int flags);
int kernel_connect(struct socket *sock, struct sockaddr *addr, int addrlen,
		   int flags);
int kernel_getsockname(struct socket *sock, struct sockaddr *addr,
		       int *addrlen);
int kernel_getpeername(struct socket *sock, struct sockaddr *addr,
		       int *addrlen);
int kernel_getsockopt(struct socket *sock, int level, int optname, char *optval,
		      int *optlen);
int kernel_setsockopt(struct socket *sock, int level, int optname, char *optval,
		      unsigned int optlen);
int kernel_sendpage(struct socket *sock, struct page *page, int offset,
		    size_t size, int flags);
int kernel_sock_ioctl(struct socket *sock, int cmd, unsigned long arg);
int kernel_sock_shutdown(struct socket *sock, enum sock_shutdown_cmd how);

/* Routine returns the IP overhead imposed by a (caller-protected) socket. */
u32 kernel_sock_ip_overhead(struct sock *sk);

#define MODULE_ALIAS_NETPROTO(proto) \
	MODULE_ALIAS("net-pf-" __stringify(proto))

#define MODULE_ALIAS_NET_PF_PROTO(pf, proto) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto))

#define MODULE_ALIAS_NET_PF_PROTO_TYPE(pf, proto, type) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto) \
		     "-type-" __stringify(type))

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
extern struct ratelimit_state net_ratelimit_state;
#endif

#endif /* __KERNEL__ */
#define MODULE_ALIAS_NET_PF_PROTO_NAME(pf, proto, name) \
	MODULE_ALIAS("net-pf-" __stringify(pf) "-proto-" __stringify(proto) \
		     name)
#endif	/* _LINUX_NET_H */
