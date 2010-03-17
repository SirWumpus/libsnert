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
typedef int SOCKET;

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

typedef union {
	struct ip_mreq mreq;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	struct ipv6_mreq mreq6;
#endif
} SocketMulticast;

typedef union {
	struct sockaddr sa;
	struct sockaddr_in in;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	struct sockaddr_in6 in6;
#endif
#ifdef HAVE_STRUCT_SOCKADDR_UN
	struct sockaddr_un un;
#endif
} SocketAddress;

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
 * @param host
 *	A unix domain socket or internet host[:port]. Note that the colon
 *	delimiter can actually be any punctuation delimiter, particularly
 *	when host is an IPv6 address, for example "2001:DB8::beef,25".
 *
 * @param port
 *	If the port is not specified as part of the host argument, then
 *	use this value.
 *
 * @return
 *	A pointer to a SocketAddress structure. It is the caller's
 *	responsibility to free() this pointer when done.
 */
extern SocketAddress *socketAddressCreate(const char *host, unsigned port);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @return
 *	The size of the SocketAddress structure.
 */
extern size_t socketAddressLength(SocketAddress *addr);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @return
 *	The SocketAddress port number, otherwise SOCKET_ERROR if not an
 *	Internet socket.
 */
extern int socketAddressGetPort(SocketAddress *addr);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @param port
 *	A new port number to set for the socket.
 *
 * @return
 *	The 0 on success, otherwise SOCKET_ERROR if not an Internet socket.
 */
extern int socketAddressSetPort(SocketAddress *addr, unsigned port);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @param flags
 *	The flags argument is formed by OR'ing the following values:
 *	SOCKET_ADDRESS_AS_IPV4
 *
 * @param ipv6
 *	An IPV6_BYTE_LENGTH buffer in which to copy of an IPv4 or IPv6 address.
 *
 * @retrun
 *	Return 0 on success; otherwise -1 if the addres is not IPv4 or IPv6.
 */
extern int socketAddressGetIPv6(SocketAddress *addr, int flags, unsigned char *ipv6);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @param flags
 *	The flags argument is formed by OR'ing the following values:
 *	SOCKET_ADDRESS_WITH_PORT, SOCKET_ADDRESS_WITH_BRACKETS,
 *	SOCKET_ADDRESS_AS_FULL, SOCKET_ADDRESS_AS_IPV4.
 *
 * @param buffer
 *	A buffer to hold the C string.
 *
 * @param size
 *	The size of the buffer. Should be at least SOCKET_ADDRESS_STRING_SIZE.
 *	It may need to be longer when using unix domain sockets.
 *
 * @retrun
 *	The length of the formatted address, excluding the terminating null
 *	byte if the buffer were of infinite size. If the return value is
 *	greater than or equal to the buffer size, then the contents of the
 *	buffer are truncated.
 */
extern long socketAddressGetString(SocketAddress *addr, int flags, char *buffer, size_t size);

#define SOCKET_ADDRESS_WITH_PORT		0x0001	/* For backwards compatibility, must be 1. */
#define SOCKET_ADDRESS_WITH_BRACKETS		0x0002	/* IP-domain literal. */
#define SOCKET_ADDRESS_AS_FULL			0x0004	/* Full IPv6 address instead of compact. */
#define SOCKET_ADDRESS_AS_IPV4			0x0008	/* Format IPv4-mapped-IPv6 as IPv4. */

/* Minimum buffer size for socketAddresGetString and socketAddressFormatIp.
 * The +6 bytes is for a delimiter and unsigned short port number. The +2
 * bytes for square brackets.
 */
#define SOCKET_ADDRESS_STRING_SIZE		(IPV6_STRING_LENGTH+8)

/**
 * @param addr
 *	A struct sockaddr pointer.
 *
 * @param flags
 *	The flags argument is formed by OR'ing the following values:
 *	SOCKET_ADDRESS_WITH_PORT, SOCKET_ADDRESS_WITH_BRACKETS,
 *	SOCKET_ADDRESS_AS_FULL, SOCKET_ADDRESS_AS_IPV4.
 *
 * @param buffer
 *	A buffer to hold the C string.
 *
 * @param size
 *	The size of the buffer. Should be at least SOCKET_ADDRESS_STRING_SIZE.
 *	It may need to be longer when using unix domain sockets.
 *
 * @retrun
 *	The length of the formatted address, excluding the terminating null
 *	byte if the buffer were of infinite size. If the return value is
 *	greater than or equal to the buffer size, then the contents of the
 *	buffer are truncated.
 */
extern long socketAddressFormatIp(const struct sockaddr *sa, int flags, char *buffer, size_t size);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @return
 *	A pointer to an allocated C string, containing the format IPv4 or
 *	IPv6 address, plus port specifier. Its the caller's responsibility
 *	to free() this string.
 */
extern char *socketAddressToString(SocketAddress *addr);

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @param buffer
 *	A buffer to hold the C string of the host name. If host name was
 *	not found, the IP address returned.
 *
 * @param size
 *	The size of the buffer.
 *
 * @retrun
 *	The length of the host name, excluding the terminating null byte if
 *	the buffer were of infinite size. If the return value is greater than
 *	or equal to the buffer size, then the contents of the buffer are
 *	truncated.
 */
extern long socketAddressGetName(SocketAddress *addr, char *buffer, long size);

/**
 * @param a
 *	A SocketAddress pointer.
 *
 * @param b
 *	A SocketAddress pointer.
 *
 * @return
 *	True if the address family, port, and IP are equal.
 */
extern int socketAddressEqual(SocketAddress *a, SocketAddress *b);

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
 */
extern int socketOpenClient(const char *host, unsigned port, long timeout, SocketAddress **out_addr, Socket2 **out_sock);

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

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_socket_h__ */
