/*
 * socket3.h
 *
 * Socket Portability API version 3
 *
 * Copyright 2001, 2011 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_socket3_h__
#define __com_snert_lib_io_socket3_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <errno.h>
#include <sys/types.h>

#ifndef __MINGW32__
# ifdef HAVE_NETDB_H
#  include <netdb.h>
extern int h_error;
# endif
#endif

#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#ifdef HAVE_POLL_H
# include <poll.h>
# ifndef INFTIM
#  define INFTIM			(-1)
# endif
#endif

#include <com/snert/lib/io/socketAddress.h>
#include <com/snert/lib/net/network.h>

#if defined(__WIN32__)
# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif

/* IPv6 support such as getaddrinfo, freeaddrinfo, getnameinfo
 * only available in Windows XP or later.
 */
# define WINVER		0x0501

# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <Iphlpapi.h>

# define ETIMEDOUT			WSAETIMEDOUT
# define EISCONN        		WSAEISCONN
# define ESHUTDOWN			WSAESHUTDOWN
# define EINPROGRESS			WSAEINPROGRESS
# define EWOULDBLOCK			WSAEWOULDBLOCK
# define ENOTCONN			WSAENOTCONN
# define EADDRINUSE			WSAEADDRINUSE
# define EPFNOSUPPORT			WSAEPFNOSUPPORT
# define ECONNABORTED			WSAECONNABORTED
# define UPDATE_ERRNO			(errno = WSAGetLastError())

# define SHUT_RD			SD_RECEIVE
# define SHUT_WR			SD_SEND
# define SHUT_RDWR			SD_BOTH

#endif /* __WIN32__ */

# define ERROR_SOCKET			(-2)

#ifdef __unix__
# define INVALID_SOCKET			(-1)
# define closesocket			close
# define UPDATE_ERRNO

#ifndef SOCKET
#define SOCKET				int
#endif

# ifndef EWOULDBLOCK
#  define EWOULDBLOCK			EAGAIN
# endif

# ifndef SHUT_RDWR
#  define SHUT_RD			0
#  define SHUT_WR			1
#  define SHUT_RDWR			2
# endif
#endif /* __unix__ */

#if defined(EAGAIN) && defined(EWOULDBLOCK) && EAGAIN == EWOULDBLOCK
# define IS_EAGAIN(e)			(e) == EAGAIN)
#else
# define IS_EAGAIN(e)			((e) == EAGAIN || (e) == EWOULDBLOCK)
#endif

#define SOCKET_BUFSIZ			4096
#define SOCKET_ERROR			(-1)
#define SOCKET_EOF			(-2)
#define SOCKET_CONNECT_TIMEOUT		60000

/* Use with select() in order to compute the file descriptor set size. */
#ifndef howmany
# define howmany(x, y)   		(((x) + ((y) - 1)) / (y))
#endif

#if defined(IPV6_ADD_MEMBERSHIP) && ! defined(IPV6_JOIN_GROUP)
# define IPV6_JOIN_GROUP		IPV6_ADD_MEMBERSHIP
#endif
#if defined(IPV6_DROP_MEMBERSHIP) && ! defined(IPV6_LEAVE_GROUP)
# define IPV6_LEAVE_GROUP		IPV6_DROP_MEMBERSHIP
#endif

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# ifndef INFTIM
#  define INFTIM	(-1)
# endif
#elif defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>
#elif defined(HAVE_POLL)
# include <poll.h>
#elif defined(HAVE_SELECT)
#else
# error " No suitable IO Event API"
#endif

/***********************************************************************
 *** Portable Socket API
 ***********************************************************************/

extern void socket3_set_debug(int level);
extern void *socket3_get_userdata(SOCKET fd);
extern int socket3_set_userdata(SOCKET fd, void *data);

/**
 * Initialise the socket subsystem.
 */
extern int socket3_init(void);

/**
 * Initialise the socket and SSL/TLS subsystems.
 */
extern int socket3_init_tls(
	const char *ca_pem_dir, const char *ca_pem_chain,
	const char *key_crt_pem, const char *key_pass,
	const char *dh_pem
);

/**
 * We're finished with the socket subsystem.
 */
extern void socket3_fini(void);

extern void socket3_fini_fd(void);
extern void socket3_fini_tls(void);
extern void (*socket3_fini_hook)(void);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param is_server
 *	True for server, false for a client connection.
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for the socket TLS accept
 *	with the server. Zero or negative value for an infinite (system)
 *	timeout.
 *
 * @return
 * 	Zero if the security handshake was successful; otherwise
 *	-1 on error.
 */
extern int socket3_start_tls(SOCKET fd, int is_server, long timeout);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param expect_cn
 *	A C string of the expected common name to match.
 *
 * @return
 *	True if the common name (CN) matches that of the peer's
 *	presented certificate.
 */
extern int socket3_is_cn_tls(SOCKET fd, const char *expect_cn);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @return
 *	Zero (0) for no error, else an errno code number and the error
 *	status of the socket is reset to zero. If there was a problem
 *	fetching the the error status, -1 is returned.
 */
extern int socket3_get_error(SOCKET fd);

/**
 * @param addr
 *	A SocketAddress pointer. For a client, this will be the
 *	destination address and for a server the local interface
 *	and port.
 *
 * @param isStream
 *	If true, then a connection oriented TCP socket is
 *	created, otherwise its a connectionless UDP socket.
 *
 * @return
 *	A SOCKET. Its the caller's responsibility to
 *	pass this pointer to socketClose() when done.
 */
extern SOCKET socket3_open(SocketAddress *addr, int isStream);

/**
 * Shutdown and close a socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open(), socket3_server(),
 *	or socket3_accept().
 */
extern void socket3_close(SOCKET fd);

extern void socket3_close_fd(SOCKET fd);
extern void socket3_close_tls(SOCKET fd);
extern void (*socket3_close_hook)(SOCKET fd);

extern void socket3_set_keep_alive(SOCKET fd, int flag, int idle, int interval, int count);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param addr;
 *	A SocketAddress pointer to which the client or server
 *	socket wishes to bind.
 *
 * @return
 * 	Zero on success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_bind(SOCKET fd, SocketAddress *addr);

/**
 * Shutdown the socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param fdhut
 *	Shutdown the read, write, or both directions of the socket.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_shutdown(SOCKET fd, int shut);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param flag
 *	If true, the socket is set non-blocking; otherwise to blocking.
 *	Remember that setting this affects both socket input and output.
 *
 * @retrun
 *	Zero (0) on success, otherwise -1 on error.
 */
extern int socket3_set_nonblocking(SOCKET fd, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param flag
 *	True to enable the Nagle algorithm (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_set_nagle(SOCKET fd, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param fdeconds
 *	The number of seconds to linger after a socket close; zero (0) to
 *	turn off lingering.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_set_linger(SOCKET fd, int seconds);

/**
 * @param fd
 *	A SOCKET returned by socket3_open().
 *
 * @param flag
 *	True to enable address and port reuse, false to disable (default).
 *	Note this call must be made before socketBind().
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_set_reuse(SOCKET fd, int flag);

/**
 * Establish a TCP connection with the destination address given when
 * socket3_open() created the socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for the socket connection
 *	with the server. Zero or negative value for an infinite (system)
 *	timeout.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_client(SOCKET fd, SocketAddress *addr, long timeout);

/**
 * A convenience function that combines the steps for socketAddressNew(),
 * socket3_open() and socket3_client() into one function call. This version
 * handles multi-homed hosts and replaces socket3_openClient().
 *
 * @param host
 *	The server name or IP address string to connect to.
 *
 * @param port
 *	If the port is not specified as part of the host argument, then
 *	use this value.
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for the socket connection
 *	with the server. Zero or negative value for an infinite (system)
 *	timeout.
 *
 * @return
 *	A SOCKET. Its the caller's responsibility to pass this
 *	pointer to socketClose() when done.
 */
extern SOCKET socket3_connect(const char *host, unsigned port, long timeout);

/**
 * @param addr
 *	A SocketAddress pointer of a local interface and port.
 *
 * @param isStream
 *	If true, then a connection oriented TCP socket is created,
 *	otherwise its a connectionless UDP socket.
 *
 * @param queue_size
 *	The connection queue size.
 *
 * @return
 *	A server SOCKET or SOCKET_ERROR.
 */
extern SOCKET socket3_server(SocketAddress *addr, int is_stream, int queue_size);

/**
 * Wait for TCP client connections on this server socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket must be a
 *	connection oriented socket returned by socket3_server().
 *
 * @param addrp
 *	A SocketAddres pointer that will contain the client address.
 *	When NULL the client address is ignored.
 *
 * @return
 *	A SOCKET for the client connection. Note that its the caller's
 *	responsiblity to call socket3_close() when done.
 */
extern SOCKET socket3_accept(SOCKET fd, SocketAddress *addr);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param buffer
 *	A buffer to save input to. The input is first taken from the
 *	read buffer, if there is any. Any remaining space in the buffer
 *	is then filled by a peek on the actual socket.
 *
 * @param size
 *	The size of the buffer to fill.
 *
 * @param from
 *	The origin of the input. May be NULL.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
extern long socket3_peek(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);

extern long socket3_peek_fd(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);
extern long socket3_peek_tls(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);
extern long (*socket3_peek_hook)(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);

/**
 * Read in a chunk of input from a socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open().
 *
 * @param buffer
 *	A buffer to save input to.
 *
 * @param size
 *	The size of the buffer.
 *
 * @param from
 *	The origin of the input. May be NULL.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
extern long socket3_read(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);

extern long socket3_read_fd(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);
extern long socket3_read_tls(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);
extern long (*socket3_read_hook)(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);

/**
 * Write buffer through a socket to the specified destination.
 *
 * @param fd
 *	A SOCKET returned by socket3_open().
 *
 * @param buffer
 *	The buffer to send.
 *
 * @param fdize
 *	The size of the buffer.
 *
 * @param to
 *	A SocketAddress pointer where to send the buffer.
 *	May be NULL for a connection based socket.
 *
 * @return
 *	The number of bytes written or SOCKET_ERROR.
 */
extern long socket3_write(SOCKET fd, unsigned char *buf, long size, SocketAddress *to);

extern long socket3_write_fd(SOCKET fd, unsigned char *buf, long size, SocketAddress *to);
extern long socket3_write_tls(SOCKET fd, unsigned char *buf, long size, SocketAddress *to);
extern long (*socket3_write_hook)(SOCKET fd, unsigned char *buf, long size, SocketAddress *to);

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). Its
 *	assumed that this socket is connectionless.
 *
 * @param group
 *	A SocketAddress pointer for the multicast group.
 *
 * @param join
 *	Boolean true to join or false to leave.
 *
 * @return
 *	Zero on success or SOCKET_ERROR.
 */
extern int socket3_multicast(SOCKET fd, SocketAddress *group, int join);

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @param flag
 *	True to enable multicast loopback (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_multicast_loopback(SOCKET fd, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @param ttl
 *	The multicast TTL to assign.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_multicast_ttl(SOCKET fd, int ttl);

/**
 * @param fd
 *	A socket file descriptor returned by socket() or accept().
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for socket input.
 *	A negative value for an infinite timeout.
 *
 * @param rw_flags
 *	Whether to wait for input, output, or both.
 *
 * @return
 *	Zero if the socket is ready; otherwise errno code.
 */
extern int socket3_wait(SOCKET fd, long timeout, unsigned rw_flags);

#define SOCKET_WAIT_READ		0x1
#define SOCKET_WAIT_WRITE		0x2

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# ifndef INFTIM
#  define INFTIM	(-1)
# endif

extern int socket3_wait_kqueue(SOCKET fd, long ms, unsigned rw_flags);

#endif
#if defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>

extern int socket3_wait_epoll(SOCKET fd, long ms, unsigned rw_flags);

#endif
#if defined(HAVE_POLL)
# if defined(HAVE_POLL_H)
#  include <poll.h>
# elif defined(HAVE_SYS_POLL_H)
#  include <sys/poll.h>
# endif

extern int socket3_wait_poll(SOCKET fd, long ms, unsigned rw_flags);

#endif
#if defined(HAVE_SELECT)

extern int socket3_wait_select(SOCKET fd, long ms, unsigned rw_flags);

#endif

#define socket3_has_input(fd, ms)	socket3_wait(fd, ms, SOCKET_WAIT_READ)
#define socket3_can_send(fd, ms)	socket3_wait(fd, ms, SOCKET_WAIT_WRITE)

extern int (*socket3_wait_fn)(SOCKET, long, unsigned);
extern void socket3_wait_fn_set(const char *name);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_socket3_h__ */
