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

#if defined(__WIN32__) || defined(__CYGWIN__)
# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif

/* IPv6 support such as getaddrinfo, freeaddrinfo, getnameinfo
 * only available in Windows XP or later.
 */
# define WINVER		0x0501

# include <ws2tcpip.h>	/* includes winsock2.h -> windows.h */
# include <Iphlpapi.h>

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
# undef UPDATE_ERRNO
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
# define IS_EAGAIN(e)			((e) == EAGAIN)
#else
# define IS_EAGAIN(e)			((e) == EAGAIN || (e) == EWOULDBLOCK)
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
# include <poll.h>
#elif defined(HAVE_SELECT)
#else
# error " No suitable IO Event API"
#endif

#ifndef SSL_DIR
# if defined(__OpenBSD__)
#  define SSL_DIR		"/etc/ssl"
# elif defined(__NetBSD__)
#  define SSL_DIR		"/etc/openssl"
# endif
#endif

#ifdef SSL_DIR
# ifndef CERT_DIR
#  define CERT_DIR		SSL_DIR "/certs"
# endif
# ifndef CA_CHAIN
#  define CA_CHAIN		SSL_DIR "/cert.pem"
# endif
# ifndef DH_PEM
#  define DH_PEM		SSL_DIR "/dh.pem"
# endif
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
extern int socket3_init_tls(void);

/**
 * @param cert_pem
 *	A client or server certificate file in PEM format.
 *	Can be NULL for client applications.
 *
 * @param key_pem
 *	The client or server's private key file in PEM format.
 *	Can be NULL for client applications.
 *
 * @param key_pass
 *	The private key password string, if required; otherwise NULL.
 *
 * @note
 *	For client applications key_pem and cert_pem are only required
 *	for bi-literal certificate exchanges. Typically this is not
 *	required, for example in HTTPS client applications.
 */
extern int socket3_set_cert_key(const char *cert_pem, const char *key_pem, const char *key_pass);
extern int socket3_set_cert_key_chain(const char *key_cert_pem, const char *key_pass);

/**
 * @param cert_dir
 *	A directory path containing individual CA certificates
 *	in PEM format. Can be NULL.
 *
 * @param ca_chain
 *	A collection of CA root certificates as a PEM formatted
 *	chain file. Can be NULL.
 *
 * @note
 *	At least one of cert_dir or ca_chain must be specified
 *	in order to find CA root certificates used for validation.
 */
extern int socket3_set_ca_certs(const char *cert_dir, const char *ca_chain);

/**
 * @param dh_pem
 *	Set the Diffie-Hellman parameter file in PEM format.
 *	Used only with SSL/TLS servers.
 *
 */
extern int socket3_set_server_dh(const char *dh_pem);

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
 *	Zero (0) for a client connection; one (1) for a server connection;
 *	two (2) for a server connection requiring a client certificate.
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

#define SOCKET3_CLIENT_TLS		0
#define SOCKET3_SERVER_TLS		1
#define SOCKET3_SERVER_CLIENT_TLS	2

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @return
 * 	Zero if the shutdown handshake was successful; otherwise
 *	-1 on error.
 *
 * @note
 *	Only used to end encrypted communcations, while keeping
 *	the socket open for further open communications.
 */
extern int socket3_end_tls(SOCKET fd);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @return
 *	Zero (0) no peer certificate, 1 failed validation,
 *	2 passed validation.
 */
extern int socket3_get_valid_tls(SOCKET fd);

/**
 */
extern int socket3_get_cipher_tls(SOCKET fd, char *buffer, size_t size);

#define SOCKET_CIPHER_STRING_SIZE	64

/**
 */
extern int socket3_get_issuer_tls(SOCKET fd, char *buffer, size_t size);
extern int socket3_get_subject_tls(SOCKET fd, char *buffer, size_t size);

#define SOCKET_INFO_STRING_SIZE		256

/**
 */
extern void socket3_get_error_tls(SOCKET fd, char *buffer, size_t size);

#define SOCKET_ERROR_STRING_SIZE	128

/**
 */
extern int socket3_set_sess_id_ctx(SOCKET fd, unsigned char *id, size_t length);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @return
 *	True if the connection is encrypted.
 */
extern int socket3_is_tls(SOCKET fd);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @return
 *	True if the peer certificate passed validation.
 *
 * @note
 *	socket3_is_cn_tls() performs this check as part of
 *	checking the common name (CN) of a certificate.
 */
extern int socket3_is_peer_ok(SOCKET fd);

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param expect_cn
 *	A C string of the expected common name to match. The string
 *	can be a glob-like pattern, see TextFind().
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

extern int socket3_shutdown_fd(SOCKET fd, int shut);
extern int socket3_shutdown_tls(SOCKET fd, int shut);
extern int (*socket3_shutdown_hook)(SOCKET fd, int shut);

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
 * @oaram family
 *	The protocol / address family (AF_INET, AF_INET6) of the socket.
 *
 * @param flag
 *	True to enable multicast loopback (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_multicast_loopback(SOCKET fd, int family, int flag);

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a multicast socket previously joined by socketMulticast().
 *
 * @oaram family
 *	The protocol / address family (AF_INET, AF_INET6) of the socket.
 *
 * @param ttl
 *	The multicast TTL to assign.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
extern int socket3_multicast_ttl(SOCKET fd, int family, int ttl);

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

extern int socket3_wait_fd(SOCKET fd, long timeout, unsigned rw_flags);
extern int socket3_wait_tls(SOCKET fd, long timeout, unsigned rw_flags);
extern int (*socket3_wait_hook)(SOCKET fd, long timeout, unsigned rw_flags);

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

#define socket3_has_input(fd, ms)	(socket3_wait(fd, ms, SOCKET_WAIT_READ) == 0)
#define socket3_can_send(fd, ms)	(socket3_wait(fd, ms, SOCKET_WAIT_WRITE) == 0)

/**
 * @param name
 *	A C string specifying the I/O wait function name. Possible values
 *	are: kqueue, epoll, poll, select. Specifying NULL sets the default
 *	suitable for the operating system.
 *
 * @return
 *	Zero (0) on success; otherwise SOCKET_ERROR.
 */
extern int socket3_wait_fn_set(const char *name);

extern int (*socket3_wait_fn)(SOCKET, long, unsigned);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_socket3_h__ */
