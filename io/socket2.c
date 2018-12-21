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
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/sys/process.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

static int debug;

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
void
socketSetDebug(int level)
{
	debug = level;
	socket3_set_debug(level);
}

/**
 * Initialise the socket subsystem.
 */
int
socketInit(void)
{
	return socket3_init();
}

/**
 * We're finished with the socket subsystem.
 */
void
socketFini(void)
{
	socket3_fini();
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
	return socket3_get_error(socketGetFd(s));
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

	rc = socket3_has_input(socketGetFd(s), timeout);
error0:
	if (1 < debug) {
		syslog(
			LOG_ERR, "socketHasInput(%lx, %ld) s.fd=%d readOffset=%d readLength=%d rc=%d errno=%d",
			(long) s, timeout, s == NULL ? -1 : socketGetFd(s),
			s->readOffset, s->readLength, rc, errno
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

	rc = socket3_can_send(socketGetFd(s), timeout);
error0:
	if (1 < debug) {
		syslog(LOG_ERR, "socketCanSend(%lx, %ld) s.fd=%d rc=%d errno=%d",
			(long) s, timeout, s == NULL ? -1 : socketGetFd(s), rc, errno
		);
	}

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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_set_nonblocking(socketGetFd(s), flag);
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

	fd = socket3_open(addr, isStream);
	if ((s = socketFdOpen(fd)) != NULL) {
		s->address = *addr;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
		s->address.sa.sa_len = socketAddressLength(addr);
#endif
	}

	if (0 < debug) {
		syslog(
			LOG_DEBUG, "socketOpen(%lx, %d) s=%lx s.fd=%d",
			(long) addr, isStream, (long) s, s == NULL ? -1 : socketGetFd(s)
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_bind(socketGetFd(s), addr);
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

	socket3_set_keep_alive(socketGetFd(s), flag, -1, -1, -1);

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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_set_nagle(socketGetFd(s), flag);
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_set_linger(socketGetFd(s), seconds);
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

	return socket3_set_reuse(socketGetFd(s), flag);
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_client(socketGetFd(s), &s->address, timeout);
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
	SOCKET fd;

	if ((fd = socket3_connect(host, port, timeout)) < 0)
		return NULL;

	return socketFdOpen(fd);
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
	if (out_sock == NULL)
		return SOCKET_ERROR;

	if ((*out_sock = socketConnect(host, port, timeout)) == NULL)
		return SOCKET_ERROR;

	if (out_addr != NULL)
		*out_addr = NULL;

	return 0;
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

	if (listen(socketGetFd(s), queue_size) < 0)
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
	} while ((c->fd = accept(socketGetFd(s), (struct sockaddr *) &c->address, &socklen)) == INVALID_SOCKET && errno == EINTR);

	if (c->fd == INVALID_SOCKET) {
		free(c);
		c = NULL;
	}
error0:
	if (0 < debug) {
		syslog(
			LOG_DEBUG, "socketAccept(%lx) s.fd=%d c=%lx c.fd=%d errno=%d",
			(long) s, s == NULL ? -1 : socketGetFd(s),
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
	if (s == NULL)
		return SOCKET_ERROR;
	return socket3_shutdown(socketGetFd(s), shut);
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
		syslog(LOG_DEBUG, "socketClose(%lx) s.fd=%d", (long) s, s == NULL ? -1 : socketGetFd(s));

	if (s != NULL) {
		socket3_close(socketGetFd(s));
		free(s);
	}
}

void
socketFdSetKeepAlive(SOCKET fd, int flag, int idle, int interval, int count)
{
	socket3_set_keep_alive(fd, flag, idle, interval, count);
}

long
socketFdWriteTo(SOCKET fd, unsigned char *buffer, long size, SocketAddress *to)
{
	return socket3_write(fd, buffer, size, to);
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

	return socket3_write(socketGetFd(s), buffer, size, to);
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
 	return socket3_write(socketGetFd(s), buffer, size, NULL);
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_read(socketGetFd(s), buffer, size, from);
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
		nbytes = socket3_read(socketGetFd(s), buffer, size, NULL);
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
		if ((bytes = socket3_peek(socketGetFd(s), buffer+length, bytes, NULL)) < 0 && length <= 0)
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
	long offset;
	unsigned char *nl;

	errno = 0;
	if (s == NULL || line == NULL || size < 1) {
		return SOCKET_ERROR;
	}

	size--;

	for (offset = 0; offset < size; ) {
		if (s->readLength <= s->readOffset) {
			if (!socket3_has_input(socketGetFd(s), s->readTimeout)) {
				if (0 < offset)
					break;
				return SOCKET_ERROR;
			}

			/* Peek at a chunk of data. */
			s->readLength = socket3_peek(socketGetFd(s), s->readBuffer, sizeof (s->readBuffer)-1, NULL);
			if (s->readLength < 0) {
				if (IS_EAGAIN(errno) || errno == EINTR) {
					errno = 0;
					nap(1, 0);
					continue;
				}
				return SOCKET_ERROR;
			}

			/* Find length of line. */
			s->readBuffer[s->readLength] = 0;
			if ((nl = (unsigned char *)strchr((char *)s->readBuffer, '\n')) != NULL)
				s->readLength = nl - s->readBuffer + 1;

			/* Read only the line. */
			s->readLength = socket3_read(socketGetFd(s), s->readBuffer, s->readLength, NULL);
			if (s->readLength < 0) {
				if (IS_EAGAIN(errno) || errno == EINTR) {
					errno = 0;
					nap(1, 0);
					continue;
				}
				return SOCKET_ERROR;
			}
			if (s->readLength == 0) {
				/* EOF with partial line read? */
				if (0 < offset)
					break;
				if (1 < debug)
					syslog(LOG_WARNING, "socketReadLine2() zero-length read errno=%d", errno);
				errno = ENOTCONN;
				return SOCKET_EOF;
			}

			/* Keep the buffer null terminated. */
			s->readBuffer[s->readLength] = 0;
			s->readOffset = 0;
		}

		/* Copy from read buffer into the line buffer. */
		line[offset++] = (char) s->readBuffer[s->readOffset++];

		if (line[offset-1] == '\n') {
			if (!keep_nl) {
				offset--;
				if (line[offset-1] == '\r')
					offset--;
			}
			break;
		}
	}

	line[offset] = '\0';

	return offset;
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_multicast(socketGetFd(s), group, join);
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_multicast_loopback(socketGetFd(s), s->address.sa.sa_family, flag);
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
	if (s == NULL) {
		errno = EFAULT;
		return SOCKET_ERROR;
	}

	return socket3_multicast_ttl(socketGetFd(s), s->address.sa.sa_family, ttl);
}



