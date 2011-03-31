/*
 * socket.c
 *
 * Socket Portability API
 *
 * Copyright 2001, 2008 by Anthony Howe. All rights reserved.
 */

#define ALLOW_ZERO_LENGTH_SEND

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
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/MailSpan.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/sys/process.h>

static int debug;

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
void
socketSetDebug(int level)
{
	debug = level;
}

/**
 * Initialise the socket subsystem.
 */
int
socketInit(void)
{
	int rc = 0;
	static int initialised = 0;

	if (!initialised && (rc = pdqInit()) == 0)
		initialised = 1;

	if (0 < debug)
		syslog(LOG_DEBUG, "socketInit() rc=%d", rc);

	return rc;
}

/**
 * We're finished with the socket subsystem.
 */
void
socketFini(void)
{
	pdqFini();
}

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	Zero (0) for no error, else an errno code number and the error
 *	status of the socket is reset to zero. If there was a problem
 *	fetching the the error status, SOCKET_ERROR is returned.
 */
int
socketGetError(Socket2 *s)
{
	int so_error;
	socklen_t socklen;

	if (s != NULL) {
		socklen = sizeof (so_error);
		if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void *) &so_error, &socklen) == 0)
			return so_error;
	}

	return SOCKET_ERROR;
}

/*
 * Defined in Dns.c instead of here. We need Dns for the socket2
 * API, but you don't need the socket2 API to use Dns with sockets.
 * This avoids duplication of a very annoying function.
 */
extern int socketTimeoutIO(SOCKET fd, long timeout, int is_input);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	True if there is input waiting to be read in the read buffer.
 */
int
socketHasBufferedInput(Socket2 *s)
{
 	if (s == NULL) {
		errno = EFAULT;
		return 0;
	}

	/* Is there data already waiting in the read buffer? */
	return s->readOffset < s->readLength;
}

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
int
socketHasInput(Socket2 *s, long timeout)
{
	int rc = 0;

 	if (s == NULL) {
		errno = EFAULT;
		goto error0;
	}

	/* Is there data already waiting in the read buffer? */
	if (s->readOffset < s->readLength) {
		rc = 1;
		goto error0;
	}

	rc = socketTimeoutIO(s->fd, timeout, 1);
error0:
	if (1 < debug) {
		syslog(
			LOG_ERR, "socketHasInput(%lx, %ld) s.fd=%d readOffset=%d readLength=%d rc=%d",
			(long) s, timeout, s == NULL ? -1 : s->fd, s->readOffset, s->readLength, rc
		);
	}

	return rc;
}

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
int
socketCanSend(Socket2 *s, long timeout)
{
	int rc = 0;

 	if (s == NULL) {
		errno = EFAULT;
		goto error0;
	}

	rc = socketTimeoutIO(s->fd, timeout, 0);
error0:
	if (1 < debug)
		syslog(LOG_ERR, "socketCanSend(%lx, %ld) s.fd=%d rc=%d", (long) s, timeout, s == NULL ? -1 : s->fd, rc);

	return rc;
}

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param flag
 *	If true, the socket is set non-blocking; otherwise to blocking.
 *	Remember that setting this affects both socket input and output.
 *
 * @retrun
 *	Zero (0) on success, otherwise SOCKET_ERROR on error.
 */
int
socketSetNonBlocking(Socket2 *s, int flag)
{
	int rc;
	long flags;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

#if defined(__WIN32__)
	flags = flag;
	rc = ioctlsocket(s->fd, FIONBIO, (unsigned long *) &flags);
#else
	flags = (long) fcntl(s->fd, F_GETFL);

	if (flag)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	rc = fcntl(s->fd, F_SETFL, flags);
#endif
	if (rc == 0)
		s->isNonBlocking = flag;

	return rc == 0 ? 0 : SOCKET_ERROR;
}

Socket2 *
socketFdOpen(SOCKET fd)
{
	socklen_t socklen;
	Socket2 *s = NULL;

	if (fd != INVALID_SOCKET && (s = malloc(sizeof (*s))) != NULL) {
		socklen = sizeof (s->address);
		memset(&s->address, 0, sizeof (s->address));
		if (getpeername(fd, &s->address.sa, &socklen)) {
			if (getsockname(fd, &s->address.sa, &socklen))
				memset(&s->address.sa, 0, sizeof (s->address));
		}
		s->isNonBlocking = 0;
		s->readOffset = 0;
		s->readLength = 0;
		s->readTimeout = -1;
		s->fd = fd;
	}

	return s;
}

void
socketFdClose(Socket2 *s)
{
	if (s != NULL) {
		/* Close without shutdown of connection. */
		closesocket(s->fd);
		free(s);
	}
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
 *	A Socket2 pointer. Its the caller's responsibility to
 *	pass this pointer to socketClose() when done.
 */
Socket2 *
socketOpen(SocketAddress *addr, int isStream)
{
	SOCKET fd;
	Socket2 *s;
	int so_type;

	so_type = isStream ? SOCK_STREAM : SOCK_DGRAM;

#ifdef NOT_USED
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
	if ((s = socketFdOpen(fd)) != NULL) {
		s->address = *addr;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
		s->address.sa.sa_len = socketAddressLength(addr);
#endif
	}

	if (0 < debug) {
		syslog(
			LOG_DEBUG, "socketOpen(%lx, %d) s=%lx s.fd=%d",
			(long) addr, isStream, (long) s, s == NULL ? -1 : s->fd
		);
	}

	return s;
}

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
int
socketBind(Socket2 *s, SocketAddress *addr)
{
	int rc;
	socklen_t socklen;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = addr->sa.sa_len;
#else
	socklen = socketAddressLength(addr);
#endif

	rc = bind(s->fd, /*(const struct sockaddr *) */ (void *) &addr->sa, socklen);
	UPDATE_ERRNO;

	return rc;
}

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
int
socketSetKeepAlive(Socket2 *s, int flag)
{
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	socketFdSetKeepAlive(s->fd, flag, -1, -1, -1);

	return 0;
}

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
int
socketSetNagle(Socket2 *s, int flag)
{
#ifdef TCP_NODELAY
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	flag = !flag;
	return setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof (flag));
#else
	return 0;
#endif
}

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
int
socketSetLinger(Socket2 *s, int seconds)
{
#ifdef SO_LINGER
	struct linger setlinger = { 1, 0 };

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

#if defined(__WIN32__)
	setlinger.l_onoff = (u_short) (0 < seconds);
	setlinger.l_linger = (u_short) seconds;
#else
	setlinger.l_onoff = 0 < seconds;
	setlinger.l_linger = seconds;
#endif

	return setsockopt(s->fd, SOL_SOCKET, SO_LINGER, (char *) &setlinger, sizeof (setlinger));
#else
	return 0;
#endif
}

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
int
socketSetReuse(Socket2 *s, int flag)
{
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

#if defined(SO_REUSEPORT)
	return setsockopt(s->fd, SOL_SOCKET, SO_REUSEPORT, (char *) &flag, sizeof (flag));
#elif defined(SO_REUSEADDR)
	return setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (char *) &flag, sizeof (flag));
#else
	return 0;
#endif
}

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
 *	with the server. Zero or negative value for the system specific
 *	timeout.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
int
socketClient(Socket2 *s, long timeout)
{
	int rc = SOCKET_ERROR;
	socklen_t socklen;

	if (0 < debug)
		syslog(LOG_DEBUG, "enter socketClient(%lx, %ld) s.fd=%d", (long) s, timeout, s == NULL ? -1 : s->fd);

	if (s == NULL) {
		errno = EFAULT;
		goto error0;
	}

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = s->address.sa.sa_len;
#else
	socklen = socketAddressLength(&s->address);
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
	if (socketSetNonBlocking(s, 1))
		goto error0;

	errno = 0;
	(void) connect(s->fd, (struct sockaddr *) &s->address, socklen);
	UPDATE_ERRNO;

	switch (errno) {
	case EAGAIN:
#if defined(EAGAIN) && defined(EWOULDBLOCK) && EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
	case EINPROGRESS:
		if (!socketCanSend(s, timeout))
			goto error1;

		/* Resets the socket's copy of the error code. */
		if (socketGetError(s))
			goto error1;

		/*@fallthrough@*/
	case 0:
		rc = 0;
	}
error1:
	(void) socketSetNonBlocking(s, 0);
error0:
	if (0 < debug)
		syslog(LOG_DEBUG, "exit  socketClient(%lx, %ld) s.fd=%d errno=%d rc=%d", (long) s, timeout, s == NULL ? -1 : s->fd, errno, rc);

	return rc;
}

extern SocketAddress *socketAddressNew(const char *host, unsigned port);

static Socket2 *
socketBasicConnect(const char *host, unsigned port, long timeout)
{
	Socket2 *s;
	SocketAddress *addr;

	if ((addr = socketAddressNew(host, port)) == NULL)
		return NULL;

	s = socketOpen(addr, 1);
	free(addr);

	if (socketClient(s, timeout)) {
		socketClose(s);
		return NULL;
	}

	return s;
}

/**
 * A convenience function that combines the steps for socketAddressCreate()
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
Socket2 *
socketConnect(const char *host, unsigned port, long timeout)
{
	int span;
	Socket2 *s;
	char *name;
	PDQ_rr *list, *rr, *a_rr;

	/* Simple case of IP address or local domain path? */
	if ((s = socketBasicConnect(host, port, timeout)) != NULL)
		return s;

	/* We have a host[:port] where the host might be multi-homed. */
	if ((span = spanHost(host, 1)) <= 0)
		return NULL;

	/* Find the optional port. */
	if (host[span] == ':') {
		char *stop;
		long value = (unsigned short) strtol(host+span+1, &stop, 10);
		if (host+span+1 < stop)
			port = value;
	}

	/* Duplicate the host to strip off the port. */
	if ((name = TextDupN(host, span)) == NULL)
		return NULL;

	/* Look up the A/AAAA record(s). */
	list = pdqFetch5A(PDQ_CLASS_IN, name);
	list = pdqListKeepType(list, PDQ_KEEP_5A|PDQ_KEEP_CNAME);

	/* Walk the list of A/AAAA records, following CNAME as needed. */
	s = NULL;
	host = name;
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
		if ((s = socketBasicConnect(((PDQ_AAAA *) a_rr)->address.string.value, port, timeout)) != NULL)
			break;

		rr = a_rr;
	}

	pdqListFree(list);
	free(name);

	return s;
}

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
int
socketOpenClient(const char *host, unsigned port, long timeout, SocketAddress **out_addr, Socket2 **out_sock)
{
#ifdef OLD_CRAP_VERSION
	SocketAddress *addr;

	if (out_sock == NULL) {
		errno = EFAULT;
		goto error0;
	}

	if ((addr = socketAddressCreate(host, port)) == NULL)
		goto error1;

	if ((*out_sock = socketOpen(addr, 1)) == NULL)
		goto error2;

	if (socketClient(*out_sock, timeout))
		goto error3;

	if (out_addr != NULL)
		*out_addr = addr;
	else
		free(addr);

	return 0;
error3:
	socketClose(*out_sock);
error2:
	free(addr);
error1:
	*out_sock = NULL;
error0:
	return SOCKET_ERROR;
#else
	if (out_sock == NULL)
		return SOCKET_ERROR;

	if ((*out_sock = socketConnect(host, port, timeout)) == NULL)
		return SOCKET_ERROR;

	if (out_addr != NULL)
		*out_addr = NULL;

	return 0;
#endif
}

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
int
socketServer(Socket2 *s, int queue_size)
{
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	if (socketBind(s, &s->address) == SOCKET_ERROR)
		return SOCKET_ERROR;

	if (listen(s->fd, queue_size) < 0)
		return SOCKET_ERROR;

	return 0;
}

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
Socket2 *
socketAccept(Socket2 *s)
{
	Socket2 *c = NULL;
	socklen_t socklen;

	if (s == NULL) {
		errno = EFAULT;
		goto error0;
	}

	if ((c = calloc(1, sizeof (*c))) == NULL)
		goto error0;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = s->address.sa.sa_len;
#else
	socklen = socketAddressLength(&s->address);
#endif
	do {
		errno = 0;
	} while ((c->fd = accept(s->fd, (struct sockaddr *) &c->address, &socklen)) == INVALID_SOCKET && errno == EINTR);

	if (c->fd == INVALID_SOCKET) {
		free(c);
		c = NULL;
	}
error0:
	if (0 < debug) {
		syslog(
			LOG_DEBUG, "socketAccept(%lx) s.fd=%d c=%lx c.fd=%d errno=%d",
			(long) s, s == NULL ? -1 : s->fd,
			(long) c, c == NULL ? -1 : c->fd,
			errno
		);
	}

	return c;
}

/**
 * Shutdown the socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param shut
 *	Shutdown the read (SHUT_RD), write (SHUT_WR), or both (SHUT_RDWR)
 *	directions of the socket.
 *
 * @return
 *	Zero for success, otherwise SOCKET_ERROR on error and errno set.
 */
int
socketShutdown(Socket2 *s, int shut)
{
	int so_type;
	socklen_t socklen;

	if (s == NULL)
		return SOCKET_ERROR;

	socklen = sizeof (so_type);
	if (getsockopt(s->fd, SOL_SOCKET, SO_TYPE, (void *) &so_type, &socklen) || so_type != SOCK_STREAM)
		return SOCKET_ERROR;

	return shutdown(s->fd, shut);
}


/**
 * Shutdown and close a socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 */
void
socketClose(Socket2 *s)
{
	if (0 < debug)
		syslog(LOG_DEBUG, "socketClose(%lx) s.fd=%d", (long) s, s == NULL ? -1 : s->fd);

	if (s != NULL) {
		socketShutdown(s, SHUT_WR);
		socketFdClose(s);
	}
}

void
socketFdSetKeepAlive(SOCKET fd, int flag, int idle, int interval, int count)
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

long
socketFdWriteTo(SOCKET fd, unsigned char *buffer, long size, SocketAddress *to)
{
	long sent, offset;
	socklen_t socklen;

	errno = 0;

	if (buffer == NULL || size <= 0)
		return 0;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = to == NULL ? 0 : to->sa.sa_len;
#else
	socklen = socketAddressLength(to);
#endif
	for (offset = 0; offset < size; offset += sent) {
		if (to == NULL)
			sent = send(fd, buffer+offset, size-offset, 0);
		else
			sent = sendto(fd, buffer+offset, size-offset, 0, (const struct sockaddr *) to, socklen);

		if (sent < 0) {
			UPDATE_ERRNO;
			if (!ERRNO_EQ_EAGAIN) {
				if (offset == 0)
					offset = SOCKET_ERROR;
				break;
			}
			sent = 0;
			nap(1, 0);
		}
	}

 	return offset;
}

/**
 * Write buffer through a connectionless socket to the specified destination.
 * Note that its possible to send a zero length packet if the underlying
 * socket implementation supports it.
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
long
socketWriteTo(Socket2 *s, unsigned char *buffer, long size, SocketAddress *to)
{
	if (s == NULL || to == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socketFdWriteTo(s->fd, buffer, size, to);
}

/**
 * Write buffer through a connection oriented socket to the pre-established
 * destination. Note that its possible to send a zero length packet if the
 * underlying socket implementation supports it.
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
long
socketWrite(Socket2 *s, unsigned char *buffer, long size)
{
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

 	return socketFdWriteTo(s->fd, buffer, size, NULL);
}

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
 *	The origin of the input. Can be NULL if not required.
 *
 * @return
 *	Return the number of bytes read or SOCKET_ERROR.
 */
long
socketReadFrom(Socket2 *s, unsigned char *buffer, long size, SocketAddress *from)
{
	long nbytes;
	socklen_t socklen;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	if (buffer == NULL || size <= 0)
		return 0;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	socklen = s->address.sa.sa_len;
#else
	socklen = socketAddressLength(&s->address);
#endif

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
	nbytes = read(s->fd, buffer, size, 0, (struct sockaddr *) from, &socklen);
#else
	nbytes = recvfrom(s->fd, buffer, size, 0, (struct sockaddr *) from, &socklen);
#endif
	UPDATE_ERRNO;

#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	if (0 <= nbytes && from != NULL)
		from->sa.sa_len = socklen;
#endif
	return nbytes;
}

/**
 * Read in a chunk of input from a connection oriented socket.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param buffer
 *	A buffer to save input to.
 *
 * @param size
 *	The size of the buffer to fill.
 *
 * @return
 *	Return the number of bytes read, SOCKET_EOF, or SOCKET_ERROR.
 */
long
socketRead(Socket2 *s, unsigned char *buffer, long size)
{
	long nbytes;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	if (buffer == NULL || size <= 0)
		return 0;

	/* Do we have bytes still in the read buffer? */
	if (s->readOffset < s->readLength) {
		long readSize = s->readLength - s->readOffset;
		nbytes = readSize < size ? readSize : size;
		(void) memcpy(buffer, s->readBuffer + s->readOffset, nbytes);
		s->readOffset += nbytes;
	} else {
#ifdef __minix
		nbytes = read(s->fd, buffer, size, 0);
#else
		nbytes = recv(s->fd, buffer, size, 0);
#endif
		UPDATE_ERRNO;
	}

	return nbytes;
}

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
long
socketGetTimeout(Socket2 *s)
{
	if (s == NULL) {
		errno = EFAULT;
		return -1;
	}

	return s->readTimeout;
}

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
void
socketSetTimeout(Socket2 *s, long timeout)
{
	if (s == NULL)
		errno = EFAULT;
	else
		s->readTimeout = timeout;
}

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
long
socketPeek(Socket2 *s, unsigned char *buffer, long size)
{
	long length, bytes;

	errno = 0;
	length = 0;

	if (socketHasBufferedInput(s)) {
		bytes = s->readLength - s->readOffset;
		length = size < bytes ? size : bytes;
		memcpy(buffer, s->readBuffer+s->readOffset, length);
	}

	if (length < size) {
		bytes = size - length;
		if ((bytes = recv(s->fd, buffer+length, bytes, MSG_PEEK)) < 0 && length <= 0)
			return SOCKET_ERROR;
		length += bytes;
	}

	return length;
}

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @return
 *	Examine the next octet in the read buffer or socket input queue
 *	without removing the octet. Otherwise SOCKET_ERROR if the buffer
 *	is empty or other error.
 */
int
socketPeekByte(Socket2 *s)
{
	unsigned char octet;

	return socketPeek(s, &octet, sizeof (octet)) <= 0 ? SOCKET_ERROR : octet;
}

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
long
socketReadLine2(Socket2 *s, char *line, long size, int keep_nl)
{
	TIMER_DECLARE(mark);
	long i = SOCKET_ERROR;

	if (s == NULL || line == NULL) {
		errno = EFAULT;
		goto error0;
	}

	if (size <= 0) {
		errno = EINVAL;
		goto error0;
	}

	/* Leave room in the buffer for a terminating null byte. */
	line[--size] = '\0';

	if (0 < debug)
		TIMER_START(mark);

	for (i = 0; i < size; ) {
		/* Keep the buffer null terminated in case we exit prematurely,
		 * because of an error or EOF condition. "Where ever you go
		 * there you are." - Buckaroo Bonzai
		 */
		line[i] = '\0';

		/* Get a new chunk of data off the wire? */
		if (s->readLength <= s->readOffset) {
			/* Non-blocking reads will be more efficient, trying
			 * to read more bytes at a time. When blocking reads
			 * are used, fallback on a byte at a time reads. I
			 * choose not to set non-blocking mode at the top of
			 * the function, leaving it up to the application
			 * design to set it up before hand.
			 */
			if (s->isNonBlocking && !socketHasInput(s, s->readTimeout)) {
				if (i <= 0) {
					/* Only timeout if no input has been
					 * read, otherwise return what we have
					 * thus far.
					 */
					errno = ETIMEDOUT;
					i = SOCKET_ERROR;
				}
				break;
			}

			s->readLength = socketRead(s, s->readBuffer, s->isNonBlocking ? sizeof s->readBuffer : 1);

			if (s->readLength < 0) {
				if (ERRNO_EQ_EAGAIN || errno == EINTR) {
					if (0 < debug) {
						TIMER_DIFF(mark);
						if (TIMER_GE_CONST(diff_mark, 1, 0))
							syslog(LOG_WARN, "socketReadLine() spinning more than 1 second: %s (%d)", strerror(errno), errno);
					}
					errno = 0;
					nap(1, 0);
					continue;
				}

				/* Read error. */
				i = SOCKET_ERROR;
				break;
			}

			if (s->readLength == 0) {
				if (i <= 0) {
					errno = ENOTCONN;
					i = SOCKET_EOF;
				}

				/* Buffer underflow, EOF before newline.
				 * Return partial line.
				 */
				break;
			}

			s->readOffset = 0;
		}

		/* Copy from our read buffer into the line buffer. */
		line[i] = (char) s->readBuffer[s->readOffset++];

		if (line[i] == '\n') {
			/* Newline found. */
			i += keep_nl;
			line[i] = '\0';

			if (!keep_nl && 0 < i && line[i-1] == '\r')
				line[--i] = '\0';

			break;
		}

		/* This is not part of the loop control for a reason; avoids
		 * the need to decrement the index before the continue after
		 * EAGAIN in order to stay put.
		 */
		i++;
	}
error0:
	if (1 < debug) {
		syslog(
			LOG_DEBUG, "socketReadLine(%lx, %lx, %ld) s.fd=%d bytes=%ld",
			(long) s, (long) line, size, s == NULL ? -1 : s->fd, i
		);
	}

	return i;
}

/**
 * Read in a line of at most size-1 bytes from the socket, stopping on
 * a newline (LF) or when the buffer is full.
 *
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param line
 *	A buffer to save a line of input to. The ASCII LF byte and any
 *	preceeding ASCII CR are always removed. The buffer is always
 *	'\0' terminated.
 *
 * @param size
 *	The size of the line buffer.
 *
 * @return
 *	Return the number of bytes read, SOCKET_EOF, or SOCKET_ERROR.
 */
long
socketReadLine(Socket2 *s, char *line, long size)
{
	return socketReadLine2(s, line, size, 0);
}

#ifdef TEST
extern void DnsFini(void);

int
main(int argc, char **argv)
{
	socketInit();
	socketFini();

	return 0;
}
#endif

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
int
socketMulticast(Socket2 *s, SocketAddress *group, int join)
{
	int rc = SOCKET_ERROR;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

#ifdef NOT_USED
	if (join) {
		SOCKET fd = WSAJoinLeaf(
			s->fd, (const struct sockaddr *) &group->in.sin_addr,
			sizeof (group->in), NULL, NULL, NULL, NULL, JL_BOTH
		);

		UPDATE_ERRNO;

		rc = fd == INVALID_SOCKET ? SOCKET_ERROR : 0;
	}
#else
	switch (group->sa.sa_family) {
		int option;
		SocketMulticast mr;

	case AF_INET:
		option = join ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP;
		mr.mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		mr.mreq.imr_multiaddr = group->in.sin_addr;
		rc = setsockopt(s->fd, IPPROTO_IP, option, (void *) &mr, sizeof (mr.mreq));
		UPDATE_ERRNO;
		break;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		option = join ? IPV6_JOIN_GROUP : IPV6_LEAVE_GROUP;
		mr.mreq6.ipv6mr_multiaddr = group->in6.sin6_addr;
		mr.mreq6.ipv6mr_interface = 0;
		rc = setsockopt(s->fd, IPPROTO_IPV6, option, (void *) &mr, sizeof (mr.mreq6));
		UPDATE_ERRNO;
		break;
#endif
	}
#endif

	return rc;
}

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
int
socketMulticastLoopback(Socket2 *s, int flag)
{
#ifdef IP_MULTICAST_LOOP
	int rc;
	char byte = flag;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	rc = setsockopt(s->fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *) &byte, sizeof (byte));
	UPDATE_ERRNO;
	return rc;
#else
	return 0;
#endif
}

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
int
socketMulticastTTL(Socket2 *s, int ttl)
{
#ifdef IP_MULTICAST_TTL
	int rc;

	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	rc = setsockopt(s->fd, IPPROTO_IP, IP_MULTICAST_TTL, (char *) &ttl, sizeof (ttl));
	UPDATE_ERRNO;
	return rc;
#else
	return 0;
#endif
}



