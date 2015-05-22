/*
 * socketAddress.c
 *
 * Socket Portability API
 *
 * Copyright 2001, 2011 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_SEMAPHORE_H
# include <semaphore.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** Socket Address
 ***********************************************************************/

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @return
 *	The size of the SocketAddress structure.
 */
size_t
socketAddressLength(SocketAddress *addr)
{
	if (addr == NULL) {
		errno = EFAULT;
		return 0;
	}

	switch (addr->sa.sa_family) {
	case AF_INET:
		return sizeof (struct sockaddr_in);
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		return sizeof (struct sockaddr_in6);
#endif
#ifdef HAVE_STRUCT_SOCKADDR_UN
	case AF_UNIX:
		return sizeof (struct sockaddr_un);
#endif
	}

	return 0;
}

SocketAddress *
socketAddressNew(const char *host, unsigned port)
{
	char *stop;
	SocketAddress *addr;
	long length = 0, value;
	unsigned char ipv6[IPV6_BYTE_SIZE];

	if (host == NULL)
		return NULL;

	if (*host != '/' && (length = parseIPv6(host, ipv6)) == 0)
		return NULL;

	if ((addr = calloc(1, sizeof (*addr))) == NULL)
		return NULL;

#ifdef HAVE_STRUCT_SOCKADDR_UN
	if (*host == '/') {
		addr->un.sun_family = AF_UNIX;
		(void) TextCopy(addr->un.sun_path, sizeof (addr->un.sun_path), (char *) host);
# ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
		addr->un.sun_len = sizeof (struct sockaddr_un);
# endif
		return addr;
	}
#endif
	if (host[length] == ':') {
		value = (unsigned short) strtol(host+length+1, &stop, 10);
		if (host+length+1 < stop)
			port = value;
	}

	/* Determine the address family and ... */
	if (isReservedIPv6(ipv6, IS_IP_V6)
	/* ... be sure to distinguish between ::0 and 0.0.0.0. */
	&& strncmp(host, "0.0.0.0", sizeof ("0.0.0.0")-1) != 0) {
#ifdef HAVE_STRUCT_SOCKADDR_IN6
		addr->in6.sin6_family = AF_INET6;
		addr->in6.sin6_port = htons(port);
# ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
		addr->sa.sa_len = sizeof (struct sockaddr_in6);
# endif
		memcpy(&addr->in6.sin6_addr, ipv6, sizeof (ipv6));
#else
		/* IPv6 address specified on a system that doesn't support IPv6. */
		free(addr);
		addr = NULL;
#endif
	} else {
		addr->in.sin_family = AF_INET;
		addr->in.sin_port = htons(port);
# ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
		addr->sa.sa_len = sizeof (struct sockaddr_in);
# endif
		memcpy(&addr->in.sin_addr, ipv6+sizeof (ipv6)-IPV4_BYTE_SIZE, IPV4_BYTE_SIZE);
	}

	return addr;
}

/**
 * @param host
 *	A unix domain socket or internet host[:port]. Note that the colon
 *	delimiter can actually be any punctuation delimiter, particularly
 *	when host is an IPv6 address, for example "2001:DB8::beef,25".
 *
 *	See RFC 2732 "Format for Literal IPv6 Addresses in URL's" where
 *	an IPv6 address with a port is specified as "[2001:0DB8::beef]:25".
 *
 *	The following variants are currently legal:
 *
 *		[2001:0DB8::beef]:25
 *		2001:0DB8::beef,25
 *		2001:0DB8::beef
 *		[123.45.67.89]:25
 *		123.45.67.89:25
 *		123.45.67.89
 *		[host.name.example]:25
 *		host.name.example:25
 *		host.name.example
 *
 * @param port
 *	If the port is not specified as part of the host argument, then
 *	use this value.
 *
 * @return
 *	A pointer to a SocketAddress structure. It is the caller's
 *	responsibility to free() this pointer when done.
 */
SocketAddress *
socketAddressCreate(const char *host, unsigned port)
{
	int length;
	SocketAddress *addr;
	unsigned short value;
	unsigned char ipv6[IPV6_BYTE_SIZE];

	if (host == NULL) {
		errno = EFAULT;
		goto error0;
	}

	if ((addr = calloc(1, sizeof (*addr))) == NULL)
		goto error0;

#ifdef HAVE_STRUCT_SOCKADDR_UN
	if (*host == '/') {
		(void) TextCopy(addr->un.sun_path, sizeof (addr->un.sun_path), (char *) host);
# ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
		addr->un.sun_len = sizeof (struct sockaddr_un);
# endif
		addr->un.sun_family = AF_UNIX;
		return addr;
	}
#endif
	if ((length = parseIPv6(host, ipv6)) == 0) {
		char *name;
		PDQ_rr *list, *rr;

		if (*host == '[')
			host++;

		if ((length = spanDomain(host, 0)) == 0)
			goto error1;

		if ((name = malloc(length+1)) == NULL)
			goto error1;
		(void) TextCopy(name, length+1, (char *) host);

		if (host[length] == ']')
			length++;

		list = pdqFetch5A(PDQ_CLASS_IN, name);

		for (rr = list; rr != NULL; rr = rr->next) {
			if (rr->section == PDQ_SECTION_QUERY)
				continue;
			if ((rr->type == PDQ_TYPE_A || rr->type == PDQ_TYPE_AAAA)) {
				memcpy(ipv6, ((PDQ_AAAA *) rr)->address.ip.value, IPV6_BYTE_SIZE);
				break;
			}
		}

		pdqListFree(list);
		free(name);

		if (rr == NULL)
			goto error1;
	}

	if (ispunct(host[length]) || isspace(host[length])) {
		char *stop;

		value = (unsigned short) strtol(host+length+1, &stop, 10);
		if (host+length+1 < stop)
			port = value;
	}

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	if (isReservedIPv6(ipv6, IS_IP_V6)
	&& strncmp(host, "0.0.0.0", sizeof ("0.0.0.0")-1) != 0) {
		addr->in6.sin6_family = AF_INET6;
		memcpy(&addr->in6.sin6_addr, ipv6, sizeof (ipv6));
	} else
#endif
	{
		addr->in.sin_family = AF_INET;
		memcpy(
			&addr->in.sin_addr,
			ipv6+sizeof (ipv6)-IPV4_BYTE_SIZE,
			IPV4_BYTE_SIZE
		);
	}

	(void) socketAddressSetPort(addr, port);

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	addr->sa.sa_len = socketAddressLength(addr);
#endif
	return addr;
error1:
	free(addr);
error0:
	return NULL;
}

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @param flags
 *	The flags argument is formed by OR'ing the following values:
 *	SOCKET_ADDRESS_WITH_PORT, SOCKET_ADDRESS_WITH_BRACKETS.
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
long
socketAddressGetString(SocketAddress *addr, int flags, char *buffer, size_t size)
{
	return socketAddressFormatIp(&addr->sa, flags, buffer, size);
}

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
 *
 * @note
 *	This implementation is thread safe.
 */
long
socketAddressGetName(SocketAddress *addr, char *buffer, long size)
{
#if defined(HAVE_SEM_T)
	static sem_t sem;
	static int sem_ready;

	if (!sem_ready) {
		sem_ready = 1;
		(void) sem_init(&sem, 0, 1);
	}
#endif
	if (addr == NULL) {
		errno = EFAULT;
		return 0;
	}

	/* If the address is ::0 or 0.0.0.0, we need to lookup
	 * our host name in a different manner.
	 */
	if (
#ifdef HAVE_STRUCT_SOCKADDR_IN6
		isReservedIPv6((unsigned char *) &addr->in6.sin6_addr, IS_IP_THIS_HOST) ||
#endif
		isReservedIPv4((unsigned char *) &addr->in.sin_addr, IS_IP_THIS_HOST)
	) {
#ifdef OLD_VERSION
/* Forgotten why I did it this way for "this host" 0.0.0.0 or ::0. */
		long length;
		struct hostent *host;

		if (gethostname(buffer, size) < 0)
			return 0;
#if defined(HAVE_SEM_T)
		if (sem_wait(&sem))
			return 0;
#endif
#ifdef HAVE_GETHOSTBYNAME2
		host = gethostbyname2(buffer, addr->sa.sa_family);
#else
		host = gethostbyname(buffer);
#endif
		if (host == NULL) {
			/* NO_DATA when name is valid, but has no address
			 * we continue to use the name. Typically this means
			 * /etc/hosts or DNS is missing either IPv4 or IPv6
			 * address for this name.
			 */
			length = (h_errno == NO_DATA ? (long) strlen(buffer) : 0);
		} else {
			length = TextCopy(buffer, size, host->h_name);
		}
#if defined(HAVE_SEM_T)
		(void) sem_post(&sem);
#endif
		return length;
#else
		/* Address is ::0 or 0.0.0.0, so just use the
		 * machine's assigned name. This means that
		 * this name and a subsequent PTR lookup might
		 * not match.
		 */
		return gethostname(buffer, size) ? 0 : (long) strlen(buffer);
#endif /* OLD_VERSION */
	}

	/* Otherwise we have an binary IP address in network order. */

#if defined(ENABLE_PDQ)
/* The PDQ API is thread safe. */
{
	int length;
	PDQ_rr *list, *rr;

       	if ((length = socketAddressGetString(addr, SOCKET_ADDRESS_WITH_BRACKETS, buffer, size)) <= 0)
       		return 0;

# ifdef HAVE_STRUCT_SOCKADDR_UN
	if (addr->sa.sa_family == AF_UNIX)
		return length;
# endif
	if ((list = pdqFetch(PDQ_CLASS_IN, PDQ_TYPE_PTR, buffer, NULL)) != NULL) {
		for (rr = list; rr != NULL; rr = rr->next) {
			if (rr->section == PDQ_SECTION_QUERY)
				continue;
			if (rr->type == PDQ_TYPE_PTR) {
				length = TextCopy(buffer, size, ((PDQ_PTR *) rr)->host.string.value);
				break;
			}
		}
		pdqListFree(list);
	}

	return length;
}
#elif defined(HAVE_GETNAMEINFO)
{
	int error;
	socklen_t socklen;

# if defined(HAVE_SEM_T)
	if (sem_wait(&sem))
		return 0;
# endif
# ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = addr->sa.sa_len;
# else
	socklen = socketAddressLength(addr);
# endif
	error = getnameinfo(&addr->sa, socklen, buffer, size, NULL, 0, NI_NAMEREQD);
# if defined(HAVE_SEM_T)
	(void) sem_post(&sem);
# endif
        return error == 0
        	? strlen(buffer)
        	: socketAddressGetString(addr, SOCKET_ADDRESS_WITH_BRACKETS, buffer, size)
        ;
}
#elif defined(HAVE_GETHOSTBYADDR)
{
	socklen_t socklen;
	struct hostent *host;

# if defined(HAVE_SEM_T)
	if (sem_wait(&sem))
		return 0;
# endif
# ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = addr->sa.sa_len;
# else
	socklen = socketAddressLength(addr);
# endif
	switch (addr->sa.sa_family) {
	case AF_INET:
		host = gethostbyaddr(&addr->in.sin_addr, socklen, addr->sa.sa_family);
		break;
# ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		host = gethostbyaddr(&addr->in6.sin6_addr, socklen, addr->sa.sa_family);
		break;
# endif
	}
# if defined(HAVE_SEM_T)
	(void) sem_post(&sem);
# endif
	if (host == NULL)
	       	return socketAddressGetString(addr, SOCKET_ADDRESS_WITH_BRACKETS, buffer, size);

	return TextCopy(buffer, size, host->h_name);
}
#else
# error "require getnameinfo, or gethostbyaddr"
#endif
}

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @param flags
 *	The flags argument is formed by OR'ing the following values:
 *	SOCKET_ADDRESS_AS_IPV4
 *
 * @param ipv6
 *	A buffer to save a copy of the IPv4 or IPv6 address in IPv6
 *	network byte order.
 *
 * @retrun
 *	Return 0 on success; otherwise SOCKET_ERROR if the address is not IPv4 or IPv6.
 */
int
socketAddressGetIPv6(SocketAddress *addr, int flags, unsigned char ipv6[IPV6_BYTE_SIZE])
{
	memset(ipv6, 0, IPV6_BYTE_SIZE);

	if (addr == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	switch (addr->sa.sa_family) {
	case AF_INET:
		memcpy(ipv6+IPV6_OFFSET_IPV4, &addr->in.sin_addr, IPV4_BYTE_SIZE);
		break;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		if ((flags & SOCKET_ADDRESS_AS_IPV4)
		&& isReservedIPv6((unsigned char *) &addr->in6.sin6_addr, IS_IP_V4_MAPPED))
			memcpy(ipv6+IPV6_OFFSET_IPV4, (char *) &addr->in6.sin6_addr+IPV6_OFFSET_IPV4, IPV4_BYTE_SIZE);
		else
			memcpy(ipv6, &addr->in6.sin6_addr, IPV6_BYTE_SIZE);
		break;
#endif
	default:
		return SOCKET_ERROR;
	}

	return 0;
}

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
int
socketAddressEqual(SocketAddress *a, SocketAddress *b)
{
	if (a == NULL || b == NULL)
		return 0;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	if (a->sa.sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&a->in6.sin6_addr)) {
		SocketAddress a_in4;

		memcpy(&a_in4.in.sin_addr.s_addr, &a->in6.sin6_addr.s6_addr[IPV6_OFFSET_IPV4], IPV4_BYTE_SIZE);
		a_in4.in.sin_port = a->in6.sin6_port;
		a_in4.in.sin_family = AF_INET;
		a = &a_in4;
	}

	if (b->sa.sa_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&b->in6.sin6_addr)) {
		SocketAddress b_in4;

		memcpy(&b_in4.in.sin_addr.s_addr, &b->in6.sin6_addr.s6_addr[IPV6_OFFSET_IPV4], IPV4_BYTE_SIZE);
		b_in4.in.sin_port = b->in6.sin6_port;
		b_in4.in.sin_family = AF_INET;
		b = &b_in4;
	}
#endif

	if (a->sa.sa_family != b->sa.sa_family)
		return 0;

	switch (a->sa.sa_family) {
	case AF_INET:
		if (a->in.sin_port != b->in.sin_port)
			return 0;
		if (memcmp(&a->in.sin_addr, &b->in.sin_addr, IPV4_BYTE_SIZE) != 0)
			return 0;
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		if (a->in6.sin6_port != b->in6.sin6_port)
			return 0;
		if (memcmp(&a->in6.sin6_addr, &b->in6.sin6_addr, IPV6_BYTE_SIZE) != 0)
			return 0;
		break;
#endif
	}

	return 1;
}

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @return
 *	A pointer to an allocated C string, containing the format IPv4 or
 *	IPv6 address, plus port specifier. Its the caller's responsibility
 *	to free() this string.
 */
char *
socketAddressToString(SocketAddress *addr)
{
	char *string;

	if (addr == NULL) {
		errno = EFAULT;
		return NULL;
	}

	if ((string = malloc(SOCKET_ADDRESS_STRING_SIZE)) == NULL)
		return NULL;

	if (SOCKET_ADDRESS_STRING_SIZE <= socketAddressGetString(addr, SOCKET_ADDRESS_WITH_PORT, string, SOCKET_ADDRESS_STRING_SIZE)) {
		free(string);
		return NULL;
	}

	return string;
}

/**
 * @param addr
 *	A SocketAddress pointer.
 *
 * @return
 *	The SocketAddress port number, otherwise SOCKET_ERROR if not an
 *	Internet socket.
 */
int
socketAddressGetPort(SocketAddress *addr)
{
	if (addr == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	switch (addr->sa.sa_family) {
	case AF_INET:
		return (unsigned) ntohs(addr->in.sin_port);
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		return (unsigned) ntohs(addr->in6.sin6_port);
#endif
	}

	errno = EINVAL;
	return SOCKET_ERROR;
}

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
int
socketAddressSetPort(SocketAddress *addr, unsigned port)
{
	if (addr == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	switch (addr->sa.sa_family) {
	case AF_INET:
		addr->in.sin_port = htons(port);
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		addr->in6.sin6_port = htons(port);
		break;
#endif
	default:
		errno = EINVAL;
		return SOCKET_ERROR;
	}

	return 0;
}

/***********************************************************************
 *** END
 ***********************************************************************/
