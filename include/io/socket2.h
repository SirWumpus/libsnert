/*
 * socket2.h
 *
 * Socket Portability API version 2
 *
 * Copyright 2001, 2008 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_socket_h__
#define __com_snert_lib_io_socket_h__	1

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
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN6_H
# include <netinet/in6.h>
#endif
#ifdef HAVE_NETINET6_IN6_H
# include <netinet6/in6.h>
#endif
#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#ifdef HAVE_SYS_UN_H
# include <sys/un.h>
#endif
#ifdef HAVE_POLL_H
# include <poll.h>
# ifndef INFTIM
#  define INFTIM			(-1)
# endif
#endif

#include <com/snert/lib/io/socketAddress.h>

#if defined(__WIN32__)
# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif

/* IPv6 support such as getaddrinfo, freeaddrinfo, getnameinfo
 * only available in Windows XP or later.
 */
# define WINVER		0x0501

# ifdef HAVE_WS2TCPIP_H
#  include <ws2tcpip.h>	/* includes winsock2.h -> windows.h */
# endif
# ifdef HAVE_IPHLPAPI_H
#  include <Iphlpapi.h>
# endif

# ifndef ETIMEDOUT
#  define ETIMEDOUT			WSAETIMEDOUT
# endif
# ifndef EISCONN
#  define EISCONN        		WSAEISCONN
# endif
# ifndef ESHUTDOWN
#  define ESHUTDOWN			WSAESHUTDOWN
# endif
# ifndef EINPROGRESS
#  define EINPROGRESS			WSAEINPROGRESS
# endif
# ifndef EWOULDBLOCK
#  define EWOULDBLOCK			WSAEWOULDBLOCK
# endif
# ifndef ENOTCONN
#  define ENOTCONN			WSAENOTCONN
# endif
# ifndef EADDRINUSE
#  define EADDRINUSE			WSAEADDRINUSE
# endif
# ifndef EPFNOSUPPORT
#  define EPFNOSUPPORT			WSAEPFNOSUPPORT
# endif
# ifndef ECONNABORTED
#  define ECONNABORTED			WSAECONNABORTED
# endif
# ifndef UPDATE_ERRNO
#  define UPDATE_ERRNO			(errno = WSAGetLastError())
# endif

# ifndef SHUT_RDWR
#  define SHUT_RD			SD_RECEIVE
#  define SHUT_WR			SD_SEND
#  define SHUT_RDWR			SD_BOTH
# endif

#endif /* __WIN32__ */

#ifndef INVALID_SOCKET
# define INVALID_SOCKET			(-1)
#endif
# define ERROR_SOCKET			(-2)

#ifdef __unix__
# define closesocket			close
# define UPDATE_ERRNO

# ifndef SOCKET
#  define SOCKET			int
# endif

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
# define ERRNO_EQ_EAGAIN		(errno == EAGAIN)
#else
# define ERRNO_EQ_EAGAIN		(errno == EAGAIN || errno == EWOULDBLOCK)
#endif

#ifndef SOCKET_BUFSIZ
# define SOCKET_BUFSIZ			1024
#endif
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
#elif defined(HAVE_SELECT)
#else
# error " No suitable IO Event API"
#endif

/***********************************************************************
 *** Portable Socket API
 ***********************************************************************/

typedef union {
	struct ip_mreq mreq;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	struct ipv6_mreq mreq6;
#endif
} SocketMulticast;

typedef struct {
	SOCKET fd;
	int readOffset;
	int readLength;
	long readTimeout;
	int isNonBlocking;
	SocketAddress address;
	unsigned char readBuffer[SOCKET_BUFSIZ];
} Socket2;

#define socketGetFd(s)		(s)->fd

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
extern void socketSetDebug(int level);

/**
 * Initialise the socket subsystem.
 */
extern int socketInit(void);

/**
 * We're finished with the socket subsystem.
 */
extern void socketFini(void);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	Zero (0) for no error, else an errno code number and the error
 *	status of the socket is reset to zero. If there was a problem
 *	fetching the the error status, -1 is returned.
 */
extern int socketGetError(Socket2 *s);

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
 *	A Socket2 pointer. Its the caller's responsibility to
 *	pass this pointer to socketClose() when done.
 */
extern Socket2 *socketOpen(SocketAddress *addr, int isStream);

extern Socket2 *socketFdOpen(SOCKET fd);
extern void socketFdSetKeepAlive(SOCKET fd, int flag, int idle, int interval, int count);
extern long socketFdWriteTo(SOCKET fd, unsigned char *buffer, long size, SocketAddress *to);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param addr;
 *	A SocketAddress pointer to which the client or server
 *	socket wishes to bind.
 *
 * @return
 * 	Zero on success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketBind(Socket2 *s, SocketAddress *addr);

/**
 * Shutdown the socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param shut
 *	Shutdown the read, write, or both directions of the socket.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketShutdown(Socket2 *s, int shut);

/**
 * Shutdown and close a socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 */
extern void socketClose(Socket2 *s);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param flag
 *	If true, the socket is set non-blocking; otherwise to blocking.
 *	Remember that setting this affects both socket input and output.
 *
 * @retrun
 *	Zero (0) on success, otherwise -1 on error.
 */
extern int socketSetNonBlocking(Socket2 *s, int flag);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param flag
 *	True to enable the Nagle algorithm (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketSetNagle(Socket2 *s, int flag);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param seconds
 *	The number of seconds to linger after a socket close; zero (0) to
 *	turn off lingering.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketSetLinger(Socket2 *s, int seconds);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen().
 *
 * @param flag
 *	True to enable address and port reuse, false to disable (default).
 *	Note this call must be made before socketBind().
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketSetReuse(Socket2 *s, int flag);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param flag
 *	True to enable the keep-alive, false to disable (default).
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketSetKeepAlive(Socket2 *s, int flag);

/**
 * Establish a TCP connection with the destination address given when
 * socketOpen() created the socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
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
extern int socketClient(Socket2 *s, long timeout);

/**
 * A convenience function that combines the steps for socketAddressCreate()
 * socketOpen() and socketClient() into one function call.
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
 * @param out_addr
 *	If not-NULL, then a SocketAddress pointer is passed back to the
 *	caller. Its the callers responsibility to free() this memory.
 *
 * @param out_sock
 *	A Socket2 pointer connected to the server host is passed back to
 *	the caller.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 *
 * @notes
 *	--DEPRICATED--
 */
extern int socketOpenClient(const char *host, unsigned port, long timeout, SocketAddress **out_addr, Socket2 **out_sock);

/**
 * A convenience function that combines the steps for socketAddressNew(),
 * socketOpen() and socketClient() into one function call. This version
 * handles multi-homed hosts and replaces socketOpenClient().
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
 *	A Socket2 pointer. Its the caller's responsibility to pass this
 *	pointer to socketClose() when done.
 */
extern Socket2 *socketConnect(const char *host, unsigned port, long timeout);

/**
 * Prepare the socket to wait for TCP client connections on the address given
 * when socketOpen() created the socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param queue_size
 *	The connection queue size.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketServer(Socket2 *s, int queue_size);

/**
 * Wait for TCP client connections on this server socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is
 *	assumed to be a connection oriented socket setup by a prior
 *	call to socketServer().
 *
 * @return
 *	A pointer to a Socket2 for the client connection. Note that its
 *	the caller's responsiblity pass this pointer to socketClose()
 *	when done.
 */
extern Socket2 *socketAccept(Socket2 *s);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for socket input.
 *	A negative value for an infinite timeout.
 *
 * @return
 *	True if there is input waiting to be read.
 */
extern int socketHasInput(Socket2 *s, long timeout);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	True if there is input waiting to be read in the read buffer.
 */
extern int socketHasBufferedInput(Socket2 *s);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for socket output.
 *	A negative value for an infinite timeout.
 *
 * @return
 *	True if the output buffer is ready to send again.
 */
extern int socketCanSend(Socket2 *s, long timeout);

/**
 * @param fd
 *	A socket file descriptor returned by socket() or accept().
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for socket input.
 *	A negative value for an infinite timeout.
 *
 * @param is_input
 *	True if waiting on input, else false for output.
 *
 * @return
 *	True if the socket is ready.
 */
extern int socketTimeoutIO(SOCKET fd, long timeout, int is_input);

/**
 * @param fd_table
 *	An array of socket file descriptors returned by socket() or accept().
 *
 * @param fd_ready
 *	An array of socket file descriptors that are ready, INVALID_SOCKET
 *	(not ready), or ERROR_SOCKET (unspecified error).
 *
 * @param length
 *	The length of both fd_table and fd_ready.
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for socket input.
 *	A negative value for an infinite timeout.
 *
 * @param is_input
 *	True if waiting on input, else false for output.
 *
 * @return
 *	True if the socket is ready.
 */
extern int socketTimeouts(SOCKET *fd_table, SOCKET *fd_ready, int length, long timeout, int is_input);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	Examine the next octet in the read buffer or socket input queue
 *	without removing the octet. Otherwise SOCKET_ERROR if the buffer
 *	is empty or other error.
 */
extern int socketPeekByte(Socket2 *s);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param buffer
 *	A buffer to save input to. The input is first taken from the
 *	read buffer, if there is any. Any remaining space in the buffer
 *	is then filled by a peek on the actual socket.
 *
 * @param size
 *	The size of the buffer to fill.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
extern long socketPeek(Socket2 *s, unsigned char *buffer, long size);

/**
 * Read in a chunk of input from a connect oriented socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param buffer
 *	A buffer to save input to.
 *
 * @param size
 *	The size of the buffer.
 *
 * @return
 *	Return the number of bytes read, SOCKET_EOF, or SOCKET_ERROR.
 */
extern long socketRead(Socket2 *s, unsigned char *buf, long size);

/**
 * Get the socketReadLine() timeout used for non-blocking sockets.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	The current timeout in milliseconds. A negative value and
 *	errno == EFAULT indicates that ``s'' is a NULL pointer,
 */
extern long socketGetTimeout(Socket2 *s);

/**
 * Set the socketReadLine() timeout used for non-blocking sockets.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for socket input.
 *	A negative value for an infinite timeout.
 */
extern void socketSetTimeout(Socket2 *s, long timeout);

/**
 * Read in a line of at most size-1 bytes from the socket, stopping on
 * a newline (LF) or when the buffer is full.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param line
 *	A buffer to save a line of input to. The ASCII LF byte and any
 *	preceeding ASCII CR, are always removed. The buffer is always
 *	'\0' terminated.
 *
 * @param size
 *	The size of the line buffer.
 *
 * @return
 *	Return the number of bytes read, SOCKET_EOF, or SOCKET_ERROR.
 */
extern long socketReadLine(Socket2 *s, char *line, long size);

/**
 * Read in a line of at most size-1 bytes from the socket, stopping on
 * a newline (LF) or when the buffer is full.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param line
 *	A buffer to save a line of input to. The buffer is always '\0'
 *	terminated.
 *
 * @param size
 *	The size of the line buffer.
 *
 * @param keep_nl
 *	True if the ASCII LF and any preceeding ASCII CR should be
 *	retained in the line buffer.
 *
 * @return
 *	Return the number of bytes read, SOCKET_EOF, or SOCKET_ERROR.
 */
extern long socketReadLine2(Socket2 *s, char *line, long size, int keep_nl);

/**
 * Read in a chunk of input from a connectionless socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen().
 *
 * @param buffer
 *	A buffer to save input to.
 *
 * @param size
 *	The size of the buffer.
 *
 * @param from
 *	The origin of the input.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
extern long socketReadFrom(Socket2 *s, unsigned char *buf, long size, SocketAddress *from);

/**
 * Write buffer through a connection oriented socket to the pre-established
 * destination.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *	Its assumed this is a connection oriented socket.
 *
 * @param buffer
 *	The buffer to send.
 *
 * @param size
 *	The size of the buffer.
 *
 * @return
 *	The number of bytes written or SOCKET_ERROR.
 */
extern long socketWrite(Socket2 *s, unsigned char *buf, long size);

/**
 * Write buffer through a connectionless socket to the specified destination.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen(). Its
 *	assumed that this socket is connectionless.
 *
 * @param buffer
 *	The buffer to send.
 *
 * @param size
 *	The size of the buffer.
 *
 * @param to
 *	A SocketAddress pointer where to send the buffer.
 *
 * @return
 *	The number of bytes written or SOCKET_ERROR.
 */
extern long socketWriteTo(Socket2 *s, unsigned char *buf, long size, SocketAddress *to);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen(). Its
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
extern int socketMulticast(Socket2 *s, SocketAddress *group, int join);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @param flag
 *	True to enable multicast loopback (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketMulticastLoopback(Socket2 *s, int flag);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @param ttl
 *	The multicast TTL to assign.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socketMulticastTTL(Socket2 *s, int ttl);

/***********************************************************************
 *** Socket Events (EXPERIMENTAL)
 ***********************************************************************/

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# ifndef INFTIM
#  define INFTIM	(-1)
# endif

# define SOCKET_EVENT_READ	EVFILT_READ
# define SOCKET_EVENT_WRITE	EVFILT_WRITE

typedef struct kevent socket_ev;

#elif defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>

# define SOCKET_EVENT_READ	(EPOLLIN | EPOLLHUP)
# define SOCKET_EVENT_WRITE	(EPOLLOUT | EPOLLHUP)

typedef struct epoll_event socket_ev;

#elif defined(HAVE_POLL)
# if defined(HAVE_POLL_H)
#  include <poll.h>
# elif defined(HAVE_SYS_POLL_H)
#  include <sys/poll.h>
# endif

# define SOCKET_EVENT_READ	(POLLIN | POLLHUP)
# define SOCKET_EVENT_WRITE	(POLLOUT | POLLHUP)

typedef struct pollfd socket_ev;

#else
# error "kqueue, epoll, or poll APIs required."
#endif

#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif
#include <com/snert/lib/type/Vector.h>

typedef struct socket_event SocketEvent;
typedef struct socket_events SocketEvents;
typedef void (*SocketEventHook)(SocketEvents *loop, SocketEvent *event);

typedef struct {
	SocketEventHook io;		/* input ready or output buffer available */
	SocketEventHook close;		/* called immediately before socketClose() */
	SocketEventHook error;		/* errno will be explicitly set */
} SocketEventOn;

struct socket_event {
	/* Private */
	FreeFn free;
	time_t expire;
	int io_type;
	int enable;

	/* Public */
	void *data;
	Socket2 *socket;
	SocketEventOn on;
};

struct socket_events {
	/* Public read only */
	JMP_BUF on_error;

	/* Private */
	int running;
	Vector events;
	socket_ev *set;
	unsigned set_size;
};

extern void socketEventFree(void *_event);
extern SocketEvent *socketEventAlloc(Socket2 *socket, int type);
extern void socketEventInit(SocketEvent *event, Socket2 *socket, int type);
extern void socketEventExpire(SocketEvent *event, const time_t *now, long ms);
extern void socketEventSetEnable(SocketEvent *event, int flag);
extern  int socketEventGetEnable(SocketEvent *event);

extern  int socketEventAdd(SocketEvents *loop, SocketEvent *event);
extern void socketEventClose(SocketEvents *loop, SocketEvent *event);
extern void socketEventRemove(SocketEvents *loop, SocketEvent *event);

extern  int socketEventsInit(SocketEvents *loop);
extern void socketEventsFree(SocketEvents *loop);
extern void socketEventsStop(SocketEvents *loop);
extern void socketEventsRun(SocketEvents *loop);
extern  int socketEventsWait(SocketEvents *loop, long ms);
extern long socketEventsTimeout(SocketEvents *loop, const time_t *now);
extern void socketEventsExpire(SocketEvents *loop, const time_t *expire);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_socket_h__ */
