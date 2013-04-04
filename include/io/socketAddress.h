/*
 * socketAddress.h
 *
 * Copyright 2001, 2011 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_socketAddress_h__
#define __com_snert_lib_io_socketAddress_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

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

/* Similar to socketAddressCreate(), but only for IP or domain sockets. */
extern SocketAddress *socketAddressNew(const char *host, unsigned port);

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
 *	An IPV6_BYTE_SIZE buffer in which to copy of an IPv4 or IPv6 address.
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
#define SOCKET_ADDRESS_STRING_SIZE		(IPV6_STRING_SIZE+8)

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
 *	A SocketAddress pointer.
 *
 * @return
 *	True if the address is a local interface.
 */
extern int socketAddressIsLocal(SocketAddress *addr);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_socketAddress_h__ */
