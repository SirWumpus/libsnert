/*
 * socket3.c
 *
 * Socket Portability API version 3
 *
 * Copyright 2001, 2011 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#undef HAVE_KQUEUE
#undef HAVE_EPOLL_CREATE

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/util/Text.h>

#if defined(HAVE_KQUEUE)
# define KQUEUE_READ		EVFILT_READ
# define KQUEUE_WRITE		EVFILT_WRITE
#endif
#if defined(HAVE_EPOLL_CREATE)
# define EPOLL_READ		(EPOLLIN | EPOLLHUP | EPOLLERR)
# define EPOLL_WRITE		(EPOLLOUT | EPOLLHUP | EPOLLERR)
#endif
#if defined(HAVE_POLL)
# define POLL_READ		(POLLIN | POLLHUP | POLLERR | POLLNVAL)
# define POLL_WRITE		(POLLOUT | POLLHUP | POLLERR | POLLNVAL)
#endif

typedef union {
	struct ip_mreq mreq;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	struct ipv6_mreq mreq6;
#endif
} SocketMulticast;

/**
 * Initialise the socket subsystem.
 */
int
socket3_init(void)
{
	int rc;
	static int initialised = 0;

	if (!initialised && (rc = pdqInit()) == 0) {
#if defined(HAVE_KQUEUE)
		socket3_wait_fn = socket3_wait_kqueue;
#elif defined(HAVE_EPOLL_CREATE)
		socket3_wait_fn = socket3_wait_epoll;
#elif defined(HAVE_POLL)
		socket3_wait_fn = socket3_wait_poll;
#elif defined(HAVE_SELECT)
		socket3_wait_fn = socket3_wait_select;
#endif
		initialised = 1;
	}

	return rc;
}

/**
 * We're finished with the socket subsystem.
 */
void
socket3_fini(void)
{
	pdqFini();
}

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @return
 *	Zero (0) for no error, else an errno code number and the error
 *	status of the socket is reset to zero. If there was a problem
 *	fetching the the error status, SOCKET_ERROR is returned.
 */
int
socket3_get_error(SOCKET fd)
{
	int so_error;
	socklen_t socklen;

	socklen = sizeof (so_error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *) &so_error, &socklen) == 0)
		return so_error;

	return SOCKET_ERROR;
}

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param flag
 *	If true, the socket is set non-blocking; otherwise to blocking.
 *	Remember that setting this affects both socket input and output.
 *
 * @retrun
 *	Zero (0) on success, otherwise SOCKET_ERROR on error.
 */
int
socket3_set_nonblocking(SOCKET fd, int flag)
{
	int rc;
	long flags;

#if defined(__WIN32__)
	flags = flag;
	rc = ioctlsocket(fd, FIONBIO, (unsigned long *) &flags);
#else
	flags = (long) fcntl(fd, F_GETFL);

	if (flag)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	rc = fcntl(fd, F_SETFL, flags);
#endif
	return rc;
}

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
SOCKET
socket3_open(SocketAddress *addr, int isStream)
{
	SOCKET fd;
	int so_type;

	so_type = isStream ? SOCK_STREAM : SOCK_DGRAM;

#ifdef NOT_YET
	fd = WSASocket(
		addr->sa.sa_family, so_type, 0, NULL, 0,
		WSA_FLAG_MULTIPOINT_C_LEAF|WSA_FLAG_MULTIPOINT_D_LEAF
	);
#else
	/* Note that sa_family can be AF_INET, AF_INET6, and AF_UNIX.
	 * When AF_UNIX is the family you don't want to specify a
	 * protocol argument.
	 */
	fd = socket(addr->sa.sa_family, so_type, 0);
#endif
	return fd;
}

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
int
socket3_bind(SOCKET fd, SocketAddress *addr)
{
	int rc;
	socklen_t socklen;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = addr->sa.sa_len;
#else
	socklen = socketAddressLength(addr);
#endif
	rc = bind(fd, /*(const struct sockaddr *) */ (void *) &addr->sa, socklen);
	UPDATE_ERRNO;

	return rc;
}

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
int
socket3_set_nagle(SOCKET fd, int flag)
{
#ifdef TCP_NODELAY
	flag = !flag;
	return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof (flag));
#else
	return 0;
#endif
}

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
int
socket3_set_linger(SOCKET fd, int seconds)
{
#ifdef SO_LINGER
	struct linger setlinger = { 1, 0 };
# if defined(__WIN32__)
	setlinger.l_linger = (u_short) seconds;
# else
	setlinger.l_linger = seconds;
# endif
	return setsockopt(fd, SOL_SOCKET, SO_LINGER, (char *) &setlinger, sizeof (setlinger));
#else
	return 0;
#endif
}

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
int
socket3_set_reuse(SOCKET fd, int flag)
{
#if defined(SO_REUSEPORT)
	return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (char *) &flag, sizeof (flag));
#elif defined(SO_REUSEADDR)
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof (flag));
#else
	return 0;
#endif
}

/**
 * Establish a TCP connection with the destination address.
 *
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a connection oriented socket.
 *
 * @param timeout
 *	A timeout value in milliseconds to wait for the socket connection
 *	with the server. Zero or negative value for the system specific
 *	timeout.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
int
socket3_client(SOCKET fd, SocketAddress *addr, long timeout)
{
	int rc = SOCKET_ERROR;
	socklen_t socklen;

	if (fd < 0 || addr == NULL)
		goto error0;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = addr->sa.sa_len;
#else
	socklen = socketAddressLength(addr);
#endif
	if (timeout <= 0) {
		/* The "libreral" connect technique relies on the implementation
		 * of connect() in being restartable for a blocking socket.
		 *
		 * http://www.eleves.ens.fr:8080/home/madore/computers/connect-intr.html
		 *
		 * This of course is not the SUS definition and non-portable.
		 *
		 * SUS also states that a blocking socket "shall block for up to
		 * an unspecified timeout interval until the connection is
		 * established". So rather than rely on an unspecified timeout
		 * internal, use one I know about.
		 */
		timeout = SOCKET_CONNECT_TIMEOUT;
	}

	/* man connect
	 *
	 * EINPROGRESS
	 * 	The  socket  is  non-blocking  and the connection cannot be com-
	 * 	pleted immediately.  It is possible to select(2) or poll(2)  for
	 * 	completion  by  selecting  the  socket for writing. After select
	 * 	indicates writability, use getsockopt(2) to  read  the  SO_ERROR
	 * 	option  at  level  SOL_SOCKET  to determine whether connect com-
	 * 	pleted  successfully  (SO_ERROR  is  zero)   or   unsuccessfully
	 * 	(SO_ERROR  is one of the usual error codes listed here, explain-
	 * 	ing the reason for the failure).
	 */
	if (socket3_set_nonblocking(fd, 1))
		goto error0;

	errno = 0;
	(void) connect(fd, (struct sockaddr *) addr, socklen);
	UPDATE_ERRNO;

	switch (errno) {
	case EAGAIN:
#if defined(EAGAIN) && defined(EWOULDBLOCK) && EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
	case EINPROGRESS:
		(void) socket3_get_error(fd);
		if (socket3_can_send(fd, timeout) != 0)
			goto error1;

		/* Resets the socket's copy of the error code. */
		if (socket3_get_error(fd))
			goto error1;

		/*@fallthrough@*/
	case 0:
		rc = 0;
	}
error1:
	(void) socket3_set_nonblocking(fd, 0);
error0:
	return rc;
}

static SOCKET
socket3_basic_connect(const char *host, unsigned port, long timeout)
{
	SOCKET fd;
	SocketAddress *addr;

	if ((addr = socketAddressNew(host, port)) == NULL)
		return SOCKET_ERROR;

	fd = socket3_open(addr, 1);

	if (socket3_client(fd, addr, timeout)) {
		socket3_close(fd);
		fd = SOCKET_ERROR;
	}

	free(addr);

	return fd;
}

/**
 * A convenience function that combines the steps for socketAddressNew()
 * socket3_open() and socket3_client() into one function call. This version
 * handles multi-homed hosts.
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
SOCKET
socket3_connect(const char *host, unsigned port, long timeout)
{
	int span;
	SOCKET fd;
	char *name;
	PDQ_rr *list, *rr, *a_rr;

	/* Simple case of IP address or local domain path? */
	if (0 <= (fd = socket3_basic_connect(host, port, timeout)))
		return fd;

	/* We have a host[:port] where the host might be multi-homed. */
	if ((span = spanHost(host, 1)) <= 0)
		return SOCKET_ERROR;

	/* Find the optional port. */
	if (host[span] == ':') {
		char *stop;
		long value = (unsigned short) strtol(host+span+1, &stop, 10);
		if (host+span+1 < stop)
			port = value;
	}

	/* Duplicate the host to strip off the port. */
	if ((name = strdup(host)) == NULL)
		return SOCKET_ERROR;
	name[span] = '\0';

	/* Look up the A/AAAA record(s). */
	list = pdqFetch5A(PDQ_CLASS_IN, name);
	list = pdqListKeepType(list, PDQ_KEEP_5A|PDQ_KEEP_CNAME);

	/* Walk the list of A/AAAA records, following CNAME as needed. */
	host = name;
	fd = SOCKET_ERROR;
	for (rr = list; rr != NULL; rr = rr->next) {
		if (rr->section == PDQ_SECTION_QUERY)
			continue;

		a_rr = pdqListFindName(rr, PDQ_CLASS_IN, PDQ_TYPE_5A, host);
		if (PDQ_RR_IS_NOT_VALID(a_rr))
			continue;

		/* The A/AAAA recorded found might have a different
		 * host name as a result of CNAME redirections.
		 * Remember this for subsequent iterations.
		 */
		host = a_rr->name.string.value;

		/* Now try to connect to this IP address. */
		if (0 <= (fd = socket3_basic_connect(((PDQ_AAAA *) a_rr)->address.string.value, port, timeout)))
			break;

		rr = a_rr;
	}

	pdqListFree(list);
	free(name);

	return fd;
}

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
SOCKET
socket3_server(SocketAddress *addr, int is_stream, int queue_size)
{
	SOCKET fd;

	if ((fd = socket3_open(addr, is_stream)) != SOCKET_ERROR) {
		if (socket3_bind(fd, addr) == SOCKET_ERROR || listen(fd, queue_size) < 0) {
			socket3_close(fd);
			fd = SOCKET_ERROR;
		}
	}

	return fd;
}

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
SOCKET
socket3_accept(SOCKET fd, SocketAddress *addrp)
{
	SOCKET client;
	socklen_t socklen;
	SocketAddress addr;

	socklen = sizeof (addr);
	client = accept(fd, (struct sockaddr *) &addr, &socklen);

	if (addrp != NULL)
		*addrp = addr;

	return client;
}

void
socket3_close(SOCKET fd)
{
	socket3_shutdown(fd, SHUT_WR);
	closesocket(fd);
}

/**
 * Shutdown the socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
 *
 * @param shut
 *	Shutdown the read (SHUT_RD), write (SHUT_WR), or both (SHUT_RDWR)
 *	directions of the socket.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
int
socket3_shutdown(SOCKET fd, int shut)
{
	int so_type;
	socklen_t socklen;

	socklen = sizeof (so_type);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, (void *) &so_type, &socklen) || so_type != SOCK_STREAM)
		return SOCKET_ERROR;

	return shutdown(fd, shut);
}

void
socket3_set_keep_alive(SOCKET fd, int flag, int idle, int interval, int count)
{
#ifdef SO_KEEPALIVE
	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char *) &flag, sizeof (flag)))
		syslog(LOG_WARNING, "setting fd=%d SO_KEEPALIVE=%d failed", fd, flag);
#endif
#ifdef TCP_KEEPIDLE
	if (0 < idle && setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (char *) &idle, sizeof (idle)))
		syslog(LOG_WARNING, "setting fd=%d TCP_KEEPIDLE=%d failed", fd, idle);
#endif
#ifdef TCP_KEEPINTVL
	if (0 < interval && setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (char *) &interval, sizeof (interval)))
		syslog(LOG_WARNING, "setting fd=%d TCP_KEEPINTVL=%d failed", fd, interval);
#endif
#ifdef TCP_KEEPCNT
	if (0 < count && setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (char *) &count, sizeof (count)))
		syslog(LOG_WARNING, "setting fd=%d TCP_KEEPCNT=%d failed", fd, count);
#endif
}

/**
 * Write buffer through a connectionless socket to the specified destination.
 * Note that its possible to send a zero length packet if the underlying
 * socket implementation supports it.
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
 *
 * @return
 *	The number of bytes written or SOCKET_ERROR.
 */
long
socket3_write(SOCKET fd, unsigned char *buffer, long size, SocketAddress *to)
{
	long sent;
	socklen_t socklen;

	if (buffer == NULL || size <= 0)
		return 0;

	errno = 0;
	socklen = socketAddressLength(to);

#if defined(HAVE_ISATTY)
	if (isatty(fd))
		sent = write(fd, buffer, size);
	else
#endif
	if (to == NULL)
		sent = send(fd, buffer, size, 0);
	else
		sent = sendto(fd, buffer, size, 0, (const struct sockaddr *) to, socklen);

 	return sent;
}

/**
 * Read in a chunk of input from a connectionless socket.
 *
 * @param fd
 *	A SOCKET returned by socket3_open().
 *
 * @param buffer
 *	A buffer to save input to.
 *
 * @param fdize
 *	The size of the buffer.
 *
 * @param from
 *	The origin of the input. Can be NULL if not required.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
long
socket3_read(SOCKET fd, unsigned char *buffer, long size, SocketAddress *from)
{
	long nbytes;
	socklen_t socklen;

	if (buffer == NULL || size <= 0)
		return 0;

	errno = 0;
	socklen = from == NULL ? 0 : sizeof (*from);

/* On Windows, a portable program has to use recv()/send() to
 * read/write sockets, since read()/write() typically only do
 * file I/O via ReadFile()/WriteFile().
 *
 * Using read()/write() for socket I/O is typical of Unix, but
 * only for connected sockets. At some point they will call
 * either recv()/send() or recvfrom()/sendto() to do the actual
 * network I/O.
 *
 * Minix 3.1.2 on the other hand has placed all the network I/O
 * logic for connected (TCP) sockets into read()/write(). recv()
 * is a cover for recvfrom() that only handles UDP currently.
 */
#ifdef __minix
	if (from == NULL)
		nbytes = read(fd, buffer, size, 0);
	else
		nbytes = read(fd, buffer, size, 0, (struct sockaddr *) from, &socklen);
#else
#if defined(HAVE_ISATTY)
	if (isatty(fd))
		nbytes = read(fd, buffer, size);
	else
#endif
	if (from == NULL)
		nbytes = recv(fd, buffer, size, 0);
	else
		nbytes = recvfrom(fd, buffer, size, 0, (struct sockaddr *) from, &socklen);
#endif
	UPDATE_ERRNO;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	if (0 <= nbytes && from != NULL)
		from->sa.sa_len = socklen;
#endif
	return nbytes;
}

/**
 * @param fd
 *	A SOCKET returned by socket3_open() or socket3_accept().
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
long
socket3_peek(SOCKET fd, unsigned char *buffer, long size)
{
	return recv(fd, buffer, size, MSG_PEEK);
}

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
int
socket3_multicast(SOCKET fd, SocketAddress *group, int join)
{
	int rc = SOCKET_ERROR;

	switch (group->sa.sa_family) {
		int option;
		SocketMulticast mr;

	case AF_INET:
		option = join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
		mr.mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		mr.mreq.imr_multiaddr = group->in.sin_addr;
		rc = setsockopt(fd, IPPROTO_IP, option, (void *) &mr, sizeof (mr.mreq));
		UPDATE_ERRNO;
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		option = join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP;
		mr.mreq6.ipv6mr_multiaddr = group->in6.sin6_addr;
		mr.mreq6.ipv6mr_interface = 0;
		rc = setsockopt(fd, IPPROTO_IPV6, option, (void *) &mr, sizeof (mr.mreq6));
		UPDATE_ERRNO;
		break;
#endif
	}

	return rc;
}

/**
 * @param fd
 *	A SOCKET returned by socket3_open(). This socket is assumed
 *	to be a multicast socket previously joined by socket3_multicast().
 *
 * @param flag
 *	True to enable multicast loopback (default), false to disable.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
int
socket3_multicast_loopback(SOCKET fd, int flag)
{
#ifdef IP_MULTICAST_LOOP
	int rc;
	char byte = flag;

	rc = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *) &byte, sizeof (byte));
	UPDATE_ERRNO;
	return rc;
#else
	return 0;
#endif
}

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
int
socket3_multicast_ttl(SOCKET fd, int ttl)
{
#ifdef IP_MULTICAST_TTL
	int rc = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (char *) &ttl, sizeof (ttl));
	UPDATE_ERRNO;
	return rc;
#else
	return 0;
#endif
}

#if defined(HAVE_KQUEUE)
int
socket3_wait_kqueue(SOCKET fd, long ms, unsigned rw_flags)
{
	int kq, error = 0;
	struct timespec ts;
	struct kevent event;

	if ((kq = kqueue()) < 0)
		return errno;

	event.ident = fd;
	event.filter = (rw_flags & SOCKET_WAIT_READ) ? EVFILT_READ : EVFILT_WRITE;
	event.flags = EV_ADD|EV_ENABLE;
	event.fflags = 0;
	event.data = 0;
	event.udata = (intptr_t) NULL;

	TIMER_SET_MS(&ts, ms);

	errno = 0;

	/* Wait for I/O or timeout. */
	switch (kevent(kq, &event, 1, &event, 1, ms < 0 ? NULL : &ts)) {
	default:
		error = errno;
		if (event.flags & EV_ERROR) {
			error = event.data;
		} else if (event.filter == EVFILT_READ) {
			if ((event.flags & EV_EOF) && event.data == 0)
				error = (event.fflags == 0) ? EPIPE : event.fflags;
		} else if (event.filter == EVFILT_WRITE) {
			if (event.flags & EV_EOF)
				error = (event.fflags == 0) ? EPIPE : event.fflags;
		}
		break;
	case 0:
		if (errno != EINTR)
			errno = ETIMEDOUT;
		/*@fallthrough@*/
	case -1:
		error = errno;
		break;
	}

	(void) close(kq);

	return error;
}
#endif
#if defined(HAVE_EPOLL_CREATE)
int
socket3_wait_epoll(SOCKET fd, long ms, unsigned rw_flags)
{
	SOCKET ev_fd;
	int error = 0;
	struct epoll_event event;

	if ((ev_fd = epoll_create(1)) < 0)
		return errno;

	if (ms < 0)
		ms = INFTIM;

	event.events = 0;
	event.data.ptr = NULL;

	if (rw_flags & SOCKET_WAIT_READ)
		event.events |= EPOLL_READ;
	if (rw_flags & SOCKET_WAIT_WRITE)
		event.events |= EPOLL_WRITE;

	if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fd, &event)) {
		error = errno;
		goto error1;
	}

	errno = 0;

	/* Wait for I/O or timeout. */
	switch (epoll_wait(ev_fd, &event, 1, ms)) {
	default:
		error = errno;
		if ((event.events & (EPOLLHUP|EPOLLIN)) == EPOLLHUP)
			error = EPIPE;
		else if (event.events & EPOLLERR)
			error = socket3_get_error(fd);
		else if (event.events & (EPOLLIN|EPOLLOUT))
			error = 0;
		break;
	case 0:
		if (errno != EINTR)
			errno = ETIMEDOUT;
		/*@fallthrough@*/
	case -1:
		error = errno;
		break;
	}
error1:
	(void) close(ev_fd);

	return error;
}
#endif
#if defined(HAVE_POLL)
int
socket3_wait_poll(SOCKET fd, long ms, unsigned rw_flags)
{
	struct pollfd event;

	if (ms < 0)
		ms = INFTIM;

	event.fd = fd;
	event.events = 0;
	event.revents = 0;

	if (rw_flags & SOCKET_WAIT_READ)
		event.events |= POLL_READ;
	if (rw_flags & SOCKET_WAIT_WRITE)
		event.events |= POLL_WRITE;

	errno = 0;

	/* Wait for some I/O or timeout. */
	switch (poll(&event, 1, ms)) {
	default:
		if ((event.revents & (POLLHUP|POLLIN)) == POLLHUP)
			errno = EPIPE;
		else if (event.revents & POLLERR)
			errno = socket3_get_error(fd);
		else if (event.revents & (POLLIN|POLLOUT))
			errno = 0;
		break;
	case 0:
		if (errno != EINTR)
			errno = ETIMEDOUT;
		/*@fallthrough@*/
	case -1:
		break;
	}

	return errno;
}
#endif
#if defined(HAVE_SELECT)
int
socket3_wait_select(SOCKET fd, long ms, unsigned rw_flags)
{
	fd_set rd, wr, err;
	struct timeval tv, *to;

	timevalSetMs(&tv, ms);
	to = ms < 0 ? NULL : &tv;

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_ZERO(&err);

	if (rw_flags & SOCKET_WAIT_READ)
		FD_SET(fd, &rd);
	if (rw_flags & SOCKET_WAIT_WRITE)
		FD_SET(fd, &wr);

	if (select(2, &rd, &wr, &err, to) == 0)
		errno = ETIMEDOUT;
	else if (FD_ISSET(fd, &err))
		errno = socket3_get_error(fd);
	else
		errno = 0;

	return errno;
}
#endif

#if defined(HAVE_KQUEUE)
int (*socket3_wait_fn)(SOCKET, long, unsigned) = socket3_wait_kqueue;
#elif defined(HAVE_EPOLL_CREATE)
int (*socket3_wait_fn)(SOCKET, long, unsigned) = socket3_wait_epoll;
#elif defined(HAVE_POLL)
int (*socket3_wait_fn)(SOCKET, long, unsigned) = socket3_wait_poll;
#elif defined(HAVE_SELECT)
int (*socket3_wait_fn)(SOCKET, long, unsigned) = socket3_wait_select;
#else
# error "No suitable socket3_wait function."
#endif

int
socket3_wait(SOCKET fd, long ms, unsigned rw_flags)
{
	if (socket3_wait_fn == NULL) {
		errno = EIO;
		return 0;
	}

	return (*socket3_wait_fn)(fd, ms, rw_flags);
}


typedef struct {
	const char *name;
	int (*wait_fn)(SOCKET, long, unsigned);
} socket3_wait_mapping;

static socket3_wait_mapping wait_mapping[] = {
#if defined(HAVE_KQUEUE)
	{ "kqueue", socket3_wait_kqueue },
#endif
#if defined(HAVE_EPOLL_CREATE)
	{ "epoll", socket3_wait_epoll },
#endif
#if defined(HAVE_POLL)
	{ "poll", socket3_wait_poll },
#endif
#if defined(HAVE_SELECT)
	{ "select", socket3_wait_select },
#endif
	{ NULL, NULL }
};

void
socket3_wait_fn_set(const char *name)
{
	socket3_wait_mapping *mapping;

	for (mapping = wait_mapping; mapping->name != NULL; mapping++) {
		if (TextInsensitiveCompare(mapping->name, name) == 0) {
			socket3_wait_fn = mapping->wait_fn;
			break;
		}
	}
}

