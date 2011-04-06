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

/**
 * Initialise the socket subsystem.
 */
extern int socket_init(void);

/**
 * We're finished with the socket subsystem.
 */
extern void socket_fini(void);

/**
 * @param fd
 *	A SOCKET returned by socket_open() or socket_accept().
 *
 * @return
 *	Zero (0) for no error, else an errno code number and the error
 *	status of the socket is reset to zero. If there was a problem
 *	fetching the the error status, -1 is returned.
 */
extern int socket_get_error(SOCKET fd);

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
extern SOCKET socket_open(SocketAddress *addr, int isStream);

/**
 * Shutdown and close a socket.
 *
 * @param fd
 *	A SOCKET returned by socket_open(), socket_server(),
 *	or socket_accept().
 */
extern void socket_close(SOCKET fd);

extern void socket_set_keep_alive(SOCKET fd, int flag, int idle, int interval, int count);

/**
 * @param fd
 *	A SOCKET returned by socket_open() or socket_accept().
 *
 * @param addr;
 *	A SocketAddress pointer to which the client or server
 *	socket wishes to bind.
 *
 * @return
 * 	Zero on success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_bind(SOCKET fd, SocketAddress *addr);

/**
 * Shutdown the socket.
 *
 * @param fd
 *	A SOCKET returned by socket_open() or socket_accept().
 *
 * @param fdhut
 *	Shutdown the read, write, or both directions of the socket.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_shutdown(SOCKET fd, int shut);

/**
 * @param fd
 *	A SOCKET returned by socket_open() or socket_accept().
 *
 * @param flag
 *	If true, the socket is set non-blocking; otherwise to blocking.
 *	Remember that setting this affects both socket input and output.
 *
 * @retrun
 *	Zero (0) on success, otherwise -1 on error.
 */
extern int socket_set_nonblocking(SOCKET fd, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket_open(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param flag
 *	True to enable the Nagle algorithm (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_set_nagle(SOCKET fd, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket_open(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param fdeconds
 *	The number of seconds to linger after a socket close; zero (0) to
 *	turn off lingering.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_set_linger(SOCKET fd, int seconds);

/**
 * @param fd
 *	A SOCKET returned by socket_open().
 *
 * @param flag
 *	True to enable address and port reuse, false to disable (default).
 *	Note this call must be made before socketBind().
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_set_reuse(SOCKET fd, int flag);

/**
 * Establish a TCP connection with the destination address given when
 * socket_open() created the socket.
 *
 * @param fd
 *	A SOCKET returned by socket_open(). This socket is assumed
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
extern int socket_client(SOCKET fd, SocketAddress *addr, long timeout);

/**
 * A convenience function that combines the steps for socketAddressNew(),
 * socket_open() and socket_client() into one function call. This version
 * handles multi-homed hosts and replaces socket_openClient().
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
extern SOCKET socket_connect(const char *host, unsigned port, long timeout);

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
extern SOCKET socket_server(SocketAddress *addr, int is_stream, int queue_size);

/**
 * Wait for TCP client connections on this server socket.
 *
 * @param fd
 *	A SOCKET returned by socket_open(). This socket must be a
 *	connection oriented socket returned by socket_server().
 *
 * @param addrp
 *	A SocketAddres pointer that will contain the client address.
 *	When NULL the client address is ignored.
 *
 * @return
 *	A SOCKET for the client connection. Note that its the caller's
 *	responsiblity to call socket_close() when done.
 */
extern SOCKET socket_accept(SOCKET fd, SocketAddress *addr);

/**
 * @param fd
 *	A SOCKET returned by socket_open() or socket_accept().
 *
 * @param buffer
 *	A buffer to save input to. The input is first taken from the
 *	read buffer, if there is any. Any remaining space in the buffer
 *	is then filled by a peek on the actual socket.
 *
 * @param fdize
 *	The size of the buffer to fill.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
extern long socket_peek(SOCKET fd, unsigned char *buffer, long size);

/**
 * Read in a chunk of input from a socket.
 *
 * @param fd
 *	A SOCKET returned by socket_open().
 *
 * @param buffer
 *	A buffer to save input to.
 *
 * @param fdize
 *	The size of the buffer.
 *
 * @param from
 *	The origin of the input. May be NULL.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
extern long socket_read(SOCKET fd, unsigned char *buf, long size, SocketAddress *from);

/**
 * Write buffer through a socket to the specified destination.
 *
 * @param fd
 *	A SOCKET returned by socket_open().
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
extern long socket_write(SOCKET fd, unsigned char *buf, long size, SocketAddress *to);

/**
 * @param fd
 *	A SOCKET returned by socket_open(). Its
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
extern int socket_multicast(SOCKET fd, SocketAddress *group, int join);

/**
 * @param fd
 *	A SOCKET returned by socket_open(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @param flag
 *	True to enable multicast loopback (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_multicast_loopback(SOCKET fd, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket_open(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @param ttl
 *	The multicast TTL to assign.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket_multicast_ttl(SOCKET fd, int ttl);

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
 *	True if the socket is ready.
 */
extern int socket_wait(SOCKET fd, long timeout, unsigned rw_flags);

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# ifndef INFTIM
#  define INFTIM	(-1)
# endif

# define SOCKET_WAIT_READ		EVFILT_READ
# define SOCKET_WAIT_WRITE		EVFILT_WRITE

#elif defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>

# define SOCKET_WAIT_READ		(EPOLLIN | EPOLLHUP)
# define SOCKET_WAIT_WRITE		(EPOLLOUT | EPOLLHUP)

#elif defined(HAVE_POLL)
# if defined(HAVE_POLL_H)
#  include <poll.h>
# elif defined(HAVE_SYS_POLL_H)
#  include <sys/poll.h>
# endif

# define SOCKET_WAIT_READ		(POLLIN | POLLHUP)
# define SOCKET_WAIT_WRITE		(POLLOUT | POLLHUP)

#elif defined(HAVE_SELECT)

# define SOCKET_WAIT_READ		0x1
# define SOCKET_WAIT_WRITE		0x2

#else
# error "kqueue, epoll, poll, or select APIs required."
#endif

#define SOCKET_WAIT_RW			(SOCKET_WAIT_READ|SOCKET_WAIT_WRITE)
#define socket_has_input(fd, ms)	socket_wait(fd, ms, SOCKET_WAIT_READ)
#define socket_can_send(fd, ms)		socket_wait(fd, ms, SOCKET_WAIT_WRITE)

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_socket3_h__ */
