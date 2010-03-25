/*
 * socketsink.c
 *
 * Copyright 2010 by Anthony Howe.  All rights reserved.
 */

#define FDS_GROWTH			50
#define SOCKET_SINK_SOCKET		"/tmp/socketsink"
#define SMTP_PIPELINING_TIMEOUT		500
#define SMTP_SLOW_REPLY_SIZE		1
#define SMTP_SLOW_REPLY_DELAY		0, 300000000

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#endif

#include <sys/uio.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

static const char usage[] =
"usage: socketsink [-d][-l facility][-t ttl]\n"
"\n"
"-d\t\tdisable daemon; run in foreground, log to standard error\n"
"-l facility\tauth, cron, daemon, ftp, lpr, mail, news, uucp, user, \n"
"\t\tlocal0, ... local7; default daemon\n"
"-t ttl\t\tdisconnect connections after this many seconds\n"
"\n"
"Creates a local stream socket, " SOCKET_SINK_SOCKET ", that other local server\n"
"processes can connect to and pass in open file descriptors to be tar\n"
"pitted. The daemon holds the connections open, discarding any input,\n"
"and sending no replies until the connected client finally disconnects.\n"
"All logging is written to the user log. Signals INT, TERM, and QUIT\n"
"will terminate the daemon.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

#define ECHO_PORT		7
#define DISCARD_PORT		9
#define DAYTIME_PORT		13
#define CHARGEN_PORT		19
#define SMTP_PORT		25

struct mapping {
	int code;
	char *name;
};

static struct mapping logFacilityMap[] = {
	{ LOG_AUTH,		"auth"		},
	{ LOG_CRON,		"cron" 		},
	{ LOG_DAEMON,		"daemon" 	},
	{ LOG_FTP,		"ftp" 		},
	{ LOG_LPR,		"lpr"		},
	{ LOG_MAIL,		"mail"		},
	{ LOG_NEWS,		"news"		},
	{ LOG_UUCP,		"uucp"		},
	{ LOG_USER,		"user"		},
	{ LOG_LOCAL0,		"local0"	},
	{ LOG_LOCAL1,		"local1"	},
	{ LOG_LOCAL2,		"local2"	},
	{ LOG_LOCAL3,		"local3"	},
	{ LOG_LOCAL4,		"local4"	},
	{ LOG_LOCAL5,		"local5"	},
	{ LOG_LOCAL6,		"local6"	},
	{ LOG_LOCAL7,		"local7"	},
	{ 0, 			NULL 		}
};

typedef struct fd_data fd_data;
typedef ssize_t (*service_fn)(int, fd_data *);

struct fd_data {
	int service;
	int state;
	char id_log[20];
	Socket2 *socket;
	time_t stamp;
	service_fn fn;
	void *data;
};

int running;
int fds_size;
int fds_length;
fd_data *fds_data;
struct pollfd *fds;
int daemon_mode = 1;
int log_facility = LOG_DAEMON;
int poll_timeout_ms = INFTIM;
long disconnect_timeout = (long) (~0UL >> 1);

static const char log_internal[] = "%s(%d): %s (%d)";
#define __F_L__			   __FILE__, __LINE__

#undef syslog

void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}
void
signal_exit(int signum)
{
	running = 0;
}

static int
name_to_code(struct mapping *map, const char *name)
{
	for ( ; map->name != NULL; map++) {
		if (TextInsensitiveCompare(name, map->name) == 0)
			return map->code;
	}

	return -1;
}

int
recv_fd(SOCKET unix_stream_socket, int *port, int *state, char token[20])
{
	SOCKET fd = -1;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec service_protocol;
	unsigned char buf[CMSG_SPACE(sizeof (int))], service[2 * sizeof (uint16_t) + 20];

	service_protocol.iov_base = &service;
	service_protocol.iov_len = sizeof (service);
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &service_protocol;
	msg.msg_iovlen = 1;
	msg.msg_controllen = CMSG_LEN(sizeof (int));
	msg.msg_control = buf;

	if (port != NULL)
		*port = 0;
	if (state != NULL)
		*state = 0;
	if (token != NULL)
		*token = '\0';

	if (recvmsg(unix_stream_socket, &msg, 0) == -1) {
		syslog(LOG_ERR, "recv_fd: %s (%d)", strerror(errno), errno);
		goto error0;
	}

	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC)) {
		syslog(LOG_ERR, "recv_fd: control message truncated");
		goto error0;
	}

	if ((cmsg = CMSG_FIRSTHDR(&msg)) != NULL
	&&  cmsg->cmsg_len == CMSG_LEN(sizeof (int))
	&&  cmsg->cmsg_level == SOL_SOCKET
	&&  cmsg->cmsg_type == SCM_RIGHTS) {
		fd = *(int *) CMSG_DATA(cmsg);

		if (port != NULL)
			*port = (int) *(uint16_t *)&service[0];
		if (state != NULL)
			*state = (int) *(int16_t *)&service[sizeof (uint16_t)];
		if (token != NULL) {
			(void) TextCopy(token, 20, &service[2*sizeof (uint16_t)]);
			token[19] = '\0';
		}
	}
error0:
	syslog(LOG_DEBUG, "recv_fd=%d service=%hu state=%hd token=%s", fd, port == NULL ? 0 : *port, state == NULL ? 0 : *state, TextEmpty(token));

	return fd;
}

int
add_fd(SOCKET fd)
{
	int i;
	fd_data *grow_data;
	struct pollfd *grow_fds;

	/* Find a free slot. */
	for (i = 1; i < fds_length; i++) {
		if (fds[i].fd == -1)
			break;
	}

	if (fds_length <= i) {
		if (fds_size <= fds_length) {
			if ((grow_fds = realloc(fds, (fds_size+FDS_GROWTH) * sizeof (*grow_fds))) == NULL) {
				syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
				return -1;
			}

			if ((grow_data = realloc(fds_data, (fds_size+FDS_GROWTH) * sizeof (*grow_data))) == NULL) {
				/* fds and fds_data don't grow in sync, then we have a problem. */
				syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
				exit(EX_SOFTWARE);
			}

			/* Clear the extra space just allocated,
			 * but care not to wipe the existing data.
			 */
			memset(&grow_data[fds_length], 0, FDS_GROWTH * sizeof (*grow_data));

			fds = grow_fds;
			fds_data = grow_data;
			fds_size += FDS_GROWTH;
			syslog(LOG_DEBUG, "fds_size=%d", fds_size);
		}

		fds_length++;
	}

	memset(&fds_data[i], 0, sizeof (*fds_data));
	(void) time(&fds_data[i].stamp);
	fds[i].events = POLLIN;
	fds[i].revents = 0;
	fds[i].fd = fd;

	return i;
}

void
close_fd(int index)
{
	long age;
	time_t now;
	struct pollfd *pfd = &fds[index];
	fd_data *data = &fds_data[index];
	char address[SOCKET_ADDRESS_STRING_SIZE];

	(void) time(&now);
	age = (long) difftime(now, fds_data[index].stamp);

	(void) socketAddressGetString(
		&data->socket->address,
		SOCKET_ADDRESS_AS_IPV4|SOCKET_ADDRESS_WITH_BRACKETS|SOCKET_ADDRESS_WITH_PORT,
		address, sizeof (address)
	);
	syslog(
		LOG_INFO, "%s closing sink fd=%d %s age=%ld%s revents=0x%X",
		data->id_log, pfd->fd, address, age,
		(disconnect_timeout <= age) ? " (timeout)" : "",
		pfd->revents
	);

	free(data->socket);
	data->socket = NULL;
	data->stamp = 0;

	closesocket(pfd->fd);
	pfd->revents = 0;
	pfd->events = 0;
	pfd->fd = -1;
}

void
tcp_keepalive_fd(SOCKET fd, int idle, int interval, int count)
{
#ifdef TCP_KEEPIDLE
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (char *) &idle, sizeof (idle)))
		syslog(LOG_WARNING, "setting fd=%d TCP_KEEPIDLE=%d failed", fd, idle);
#endif
#ifdef TCP_KEEPINTVL
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (char *) &interval, sizeof (interval)))
		syslog(LOG_WARNING, "setting fd=%d TCP_KEEPINTVL=%d failed", fd, interval);
#endif
#ifdef TCP_KEEPCNT
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (char *) &count, sizeof (count)))
		syslog(LOG_WARNING, "setting fd=%d TCP_KEEPCNT=%d failed", fd, count);
#endif
}

ssize_t
discard_echo_input(SOCKET fd, fd_data *data)
{
	char buffer[128], *name;
	ssize_t in_bytes, out_bytes, count;

	name = data->service == ECHO_PORT ? "echo" : "discard";

	for (count = 0; count < 512; count += in_bytes) {
		in_bytes = recv(fd, buffer, sizeof (buffer)-1, MSG_DONTWAIT);
		if (in_bytes < 0) {
			if (0 < count)
				break;
			syslog(LOG_DEBUG, "%s %s fd=%d count=%ld: %s (%d)", data->id_log, name, fd, (long) count, strerror(errno), errno);
			return -1;
		}
		if (in_bytes == 0)
			break;

		buffer[in_bytes] = '\0';
		syslog(LOG_INFO, "%s %s fd=%d > %ld:%s", data->id_log, name, fd, (long) in_bytes, buffer);

		if (data->service == ECHO_PORT) {
			if ((out_bytes = socketFdWriteTo(fd, buffer, in_bytes, NULL)) != in_bytes) {
				syslog(LOG_DEBUG, "%s %s fd=%d count=%ld in=%ld out=%ld %s (%d)", data->id_log, name, fd, (long) count, (long) in_bytes, (long) out_bytes, errno == 0 ? "" : strerror(errno), errno);
				return 0;
			}
			syslog(LOG_INFO, "%s %s fd=%d < %ld:%s", data->id_log, name, fd, (long) out_bytes, buffer);
		}
	}

	syslog(LOG_DEBUG, "%s %s fd=%d count=%ld", data->id_log, name, fd, (long) count);

	return count;
}

#include <com/snert/lib/mail/limits.h>

#ifdef ENABLE_SLOW_REPLY
void
nap(unsigned seconds, unsigned nanoseconds)
{
#if defined(__WIN32__)
	Sleep(seconds * 1000 + nanoseconds / 1000000);
#elif defined (HAVE_NANOSLEEP)
{
	struct timespec ts0, ts1, *sleep_time, *unslept_time, *tmp;

	sleep_time = &ts0;
	unslept_time = &ts1;
	ts0.tv_sec = seconds;
	ts0.tv_nsec = nanoseconds;

	while (nanosleep(sleep_time, unslept_time)) {
		tmp = sleep_time;
		sleep_time = unslept_time;
		unslept_time = tmp;
	}
}
#else
{
	unsigned unslept;

	while (0 < (unslept = sleep(seconds)))
		seconds = unslept;
}
#endif
}

ssize_t
slow_send(fd_data *data, unsigned char *buffer, long length)
{
	SOCKET fd;
	ssize_t sent;
	long n, offset;
	unsigned char peek[128];

	/* Detect pipelining: for each line of output check if the client
	 * has started sending input before the reply has been completed.
	 */

	/* Ignore any subsequent input that follows the QUIT command
	 * as something in the TCP shutdown sequence appears to trigger
	 * this test.
	 *
	 * Also due to a stupid bug in some brain dead versions of
	 * Microsoft Exchange, we have to ignore pipeline input that
	 * might immediately follow the DATA command.
	 *
	 * A related issue to this would be bad SMTP implementations
	 * that simply assume a 354 response will always be sent and
	 * proceed to pipeline the content. Their assumption is broken
	 * since it's perfectly reasonable to perform some additional
	 * tests at DATA and return 4xy or 5xy response instead of 354.
	 */
	if (TextInsensitiveStartsWith(buffer, "354") < 0
	&&  TextInsensitiveStartsWith(buffer, "221") < 0
	&&  socketHasInput(data->socket, SMTP_PIPELINING_TIMEOUT)) {
		n = socketPeek(data->socket, peek, sizeof (peek)-1);

		/* A disconnect appears as a zero length packet and should
		 * not be considered pipelined input. Also ignore pipelined
		 * newlines.
		 */
		if (0 < n && *peek != 0x0D && *peek != 0x0A) {
			peek[n] = '\0';
			syslog(LOG_INFO, "%s pipeline input=%ld:%s", data->id_log, n, peek);
		}
	}

	errno = 0;
	fd = socketGetFd(data->socket);

	/* Implement slow output of SMTP reply. */
	for (offset = 0; offset < length; offset += sent) {
		/* When the amount remaining is within SMTP_SLOW_REPLY_SIZE
		 * plus the size CRLF, then send N bytes in order not to
		 * have an orphan CRLF on the next cycle. This is to deal
		 * with stupid PIX firewalls.
		 */
		n = length-offset;
		if (SMTP_SLOW_REPLY_SIZE + 2 < n)
			n = SMTP_SLOW_REPLY_SIZE;

		if ((sent = send(fd, buffer+offset, n, 0)) < 0) {
			UPDATE_ERRNO;
			if (!ERRNO_EQ_EAGAIN) {
				/* While trying to send a reply back to the client,
				 * the server had an I/O error. Typical cause is
				 * "broken pipe", ie. the connection with the client
				 * was lost, most likely due to the client dropping
				 * the connection. A lot of spamware reacts on the
				 * first digit of the response, dropping the connection
				 * as soon as it gets a 4xy or 5xy indication and
				 * ignoring the rest.
				 *
				 * Sometimes the client might disconnect during the
				 * welcome banner, because of delays. A lot of spamware
				 * is impatient and will drop the connection as a result.
				 */
				if (offset == 0) {
					syslog(LOG_ERR, "%s fd=%d I/O error", data->id_log, fd);
					return -1;
				}
				break;
			}
			sent = 0;
		}

		nap(SMTP_SLOW_REPLY_DELAY);
	}

	return offset;
}
#endif

ssize_t
smtp_input(SOCKET fd, fd_data *data)
{
	socklen_t socklen;
	SocketAddress address;
	long in_bytes, out_bytes, sent;
	char if_addr[DOMAIN_STRING_LENGTH];
	unsigned char buffer[SMTP_TEXT_LINE_LENGTH];
	static const char fmt_in[] = "%s smtp fd=%d > %ld:%s";
	static const char fmt_out[] = "%s smtp fd=%d < %ld:%s sent=%lu errno=%d";
	static const char msg_220[] = "220 %s ESMTP Welcome to Kuroi-Ana\r\n";
	static const char msg_221[] = "221 2.0.0 %s Closing connection\r\n";
	static const char msg_421[] = "421 4.3.2 %s Come back later, much later\r\n";
	static const char msg_503[] = "503 5.5.1 Command out of sequence\r\n";
	static const char msg_554[] = "554 5.4.0 \"No soup for you!\"\r\n";
	static const char msg_250[] = "250 2.0.0 OK\r\n";
	static const char msg_354[] = "354 Enter mail, end with \".\" on a line by itself\r\n";
	static const char msg_451[] = "451 4.7.1 \"No soup for you!\"\r\n";

	switch (data->state) {
	case 200: case 400: case 500: case 0:
		/* Send initial banner. */
		in_bytes = 1;
		break;

	default:
		if ((in_bytes = socketReadLine2(data->socket, buffer, sizeof (buffer), 1)) <= 0) {
			syslog(LOG_INFO, "%s smtp fd=%d terminated (%d)", data->id_log, fd, errno);
			return 0;
		}

		syslog(LOG_INFO, fmt_in, data->id_log, fd, in_bytes, buffer);

		if (0 < TextInsensitiveStartsWith(buffer, "QUIT")) {
			data->state = 221;
			in_bytes = 0;
		} else if (0 < TextInsensitiveStartsWith(buffer, "DATA")) {
			data->fn = discard_echo_input;
			data->service = 9;
			data->state = 354;
			return in_bytes;
		} else if (data->state == 300 && strcmp(buffer, ".\r\n") == 0) {
			data->state = 451;
		}
		break;
	}

	*if_addr = '\0';
	socklen = sizeof (address);
	if (getsockname(fd, (struct sockaddr *) &address, &socklen) == 0)
		(void) socketAddressGetName(&address, if_addr, sizeof (if_addr));

	switch (data->state) {
	case 200:
		/* Postive welcome banner, accept all commands. */
		out_bytes = snprintf(buffer, sizeof (buffer), msg_220, if_addr);
		data->state = 250;
		break;
	case 250:
		out_bytes = snprintf(buffer, sizeof (buffer), msg_250);
		break;
	case 221:
		/* Postive welcome banner, accept all commands. */
		out_bytes = snprintf(buffer, sizeof (buffer), msg_221, if_addr);
		break;

	case 300:
		out_bytes = 0;
		break;
	case 354:
		out_bytes = snprintf(buffer, sizeof (buffer), msg_354);
		data->state = 300;
		break;

	default:
	case 400:
		data->state = 421;
		/*@fallthrough@*/
	case 421:
		/* Temp.fail welcome banner, temp.fail all commands. */
		out_bytes = snprintf(buffer, sizeof (buffer), msg_421, if_addr);
		break;

	case 451:
		out_bytes = snprintf(buffer, sizeof (buffer), msg_451);
		data->state = 250;
		break;

	case 500:
		/* Reject (no service) banner, reject all commands. */
		out_bytes = snprintf(buffer, sizeof (buffer), msg_554);
		data->state = 503;
		break;
	case 503:
		out_bytes = snprintf(buffer, sizeof (buffer), msg_503);
		break;
	}

	errno = 0;
#ifdef ENABLE_SLOW_REPLY
	sent = slow_send(data, buffer, out_bytes);
#else
	sent = socketWrite(data->socket, buffer, out_bytes);
#endif
	syslog(LOG_INFO, fmt_out, data->id_log, fd, out_bytes, buffer, sent, errno);

	return in_bytes;
}

int
main(int argc, char **argv)
{
	long age;
	SOCKET fd;
	time_t now;
	fd_data *data;
	socklen_t slen;
	Socket2 *socket;
	SocketAddress client_address;
	SocketAddress *socket_address;
	int i, rc, index, connected, optval;
	char address[SOCKET_ADDRESS_STRING_SIZE];

	while ((i = getopt(argc, argv, "dl:t:")) != -1) {
		switch (i) {
		case 'd':
			daemon_mode = 0;
			break;

		case 'l':
			log_facility = name_to_code(logFacilityMap, optarg);
			break;

		case 't':
			disconnect_timeout = strtol(optarg, NULL, 10);
			poll_timeout_ms = disconnect_timeout * 1000;
			break;

		case '?':
			fputs(usage, stdout);
			return EX_USAGE;
		}
	}

	if (optind < argc) {
		fputs(usage, stdout);
		return EX_USAGE;
	}

	if (daemon_mode) {
		if (daemon(1, 1)) {
			fprintf(stderr, "daemon failed\n");
			return EX_SOFTWARE;
		}

		setlogmask(LOG_UPTO(LOG_DEBUG));
		openlog("socketsink", LOG_PID, log_facility);
	} else {
		LogOpen("(standard error)");
		LogSetLevel(LOG_PRI(LOG_DEBUG));
		LogSetProgramName("socketsink");
	}

	rc = EX_SOFTWARE;
	syslog(LOG_INFO, "socketsink %s", LIBSNERT_COPYRIGHT);

	/* Prevent SIGPIPE from terminating the process. */
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}

	/* Catch termination signals for a clean exit. */
	if (signal(SIGINT, signal_exit) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}
	if (signal(SIGQUIT, signal_exit) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}
	if (signal(SIGTERM, signal_exit) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}

	/* Prepare the poll table. */
	if ((fds = calloc(FDS_GROWTH, sizeof (*fds))) == NULL) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}

	if ((fds_data  = calloc(FDS_GROWTH, sizeof (*fds_data))) == NULL) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error1;
	}

	fds_length = 0;
	fds_size = FDS_GROWTH;
	for (i = 0; i < fds_size; i++)
		fds[i].fd = -1;

	/* Create the local socket. */
	if (socketInit()) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		fprintf(stderr, "socketInit: %s (%d)\n", strerror(errno), errno);
		goto error1;
	}
	if ((socket_address = socketAddressCreate(SOCKET_SINK_SOCKET, 0)) == NULL) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		fprintf(stderr, "address %s: %s (%d)\n", SOCKET_SINK_SOCKET, strerror(errno), errno);
		goto error1;
	}
	if ((socket = socketOpen(socket_address, 1)) == NULL) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		fprintf(stderr, "open %s: %s (%d)\n", SOCKET_SINK_SOCKET, strerror(errno), errno);
		goto error2;
	}
	if (socketServer(socket, 10)) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		fprintf(stderr, "server %s: %s (%d)\n", SOCKET_SINK_SOCKET, strerror(errno), errno);
		goto error3;
	}
	if (chmod(SOCKET_SINK_SOCKET, S_IRWXO|S_IRWXG|S_IRWXU)) {
		syslog(LOG_ERR, "%s error: %s (%d)", SOCKET_SINK_SOCKET, strerror(errno), errno);
		fprintf(stderr, "chmod %s: %s (%d)\n", SOCKET_SINK_SOCKET, strerror(errno), errno);
		goto error3;
	}

	(void) time(&fds_data[0].stamp);
	fds[0].fd = socketGetFd(socket);
	fds[0].events = POLLIN;
	fds_length++;

	syslog(LOG_INFO, "ready");

	/* Now wait for socket file descriptors transfers */
	for (running = 1; running; ) {
		if (poll(fds, fds_length, poll_timeout_ms) < 0)
			continue;

		(void) time(&now);

		/* Do we have a new client connection? */
		if (fds[0].revents & POLLIN) {
			fd = accept(fds[0].fd, &client_address.sa, &slen);

			if ((index = add_fd(fd)) == -1) {
				closesocket(fd);
			} else {
				syslog(LOG_INFO, "source fd=%d", fd);
				data = &fds_data[index];
				data->socket = socketFdOpen(fd);
				(void) time(&data->stamp);
				data->service = -1;
				data->state = -1;
				data->data = NULL;
				data->fn = NULL;
				fds[index].fd = fd;
			}
		}

		connected = 0;

		/* Check the remaining client connections and those being sinked. */
		for (i = 1; i < fds_length; i++) {
			age = (long) difftime(now, fds_data[i].stamp);

			/* Do we have a socket file descriptor transfer from a client? */
			if (fds_data[i].service == -1) {
				if (fds[i].revents == 0)
					continue;

				fds[i].revents = 0;
				if ((index = add_fd(-1)) == -1) {
					syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
					continue;
				}

				data = &fds_data[index];
				data->socket = NULL;
				data->data = NULL;

				if ((fd = recv_fd(fds[i].fd, &data->service, &data->state, data->id_log)) == -1) {
					close_fd(i);
					continue;
				}

				fds[index].fd = fd;

				/* Always allocate a Socket2 structure. */
				if ((data->socket = socketFdOpen(fd)) == NULL) {
					syslog(LOG_ERR, " %s fd=%d %s(%d): %s (%d)", data->id_log, fd, __F_L__, strerror(errno), errno);
					close_fd(i);
					continue;
				}

				(void) socketAddressGetString(&data->socket->address, SOCKET_ADDRESS_AS_IPV4|SOCKET_ADDRESS_WITH_BRACKETS|SOCKET_ADDRESS_WITH_PORT, address, sizeof (address));
				syslog(LOG_INFO, "%s sink fd=%d %s service=%u state=%d", data->id_log, fd, address, data->service, data->state);

				/* Set the socket I/O buffers to zero;
				 * not worried about speed.
				 */
				optval = 256;
				if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &optval, sizeof (optval)))
					syslog(LOG_WARNING, "%s setting fd=%d SO_SNDBUF=%d failed", data->id_log, fd, optval);
				optval = 256;
				if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &optval, sizeof (optval)))
					syslog(LOG_WARNING, "%s setting fd=%d SO_RCVBUF=%d failed", data->id_log, fd, optval);

				tcp_keepalive_fd(fd, 60, 20, 3);

				switch (data->service) {
				default:
					data->fn = discard_echo_input;
					break;
				case SMTP_PORT:
					data->fn = smtp_input;
					/* For the initial connection state, send banner. */
					switch (data->state) {
					case 200: case 400: case 500: case 0:
						if (smtp_input(fd, data) <= 0)
							close_fd(index);
					}
					break;
				}

				/* Don't double count if we just added
				 * to the end of the fds table.
				 */
				connected += index+1 < fds_length;
			}

			/* Close any file descriptors that disconnected. */
			else if (fds[i].revents & (POLLHUP|POLLERR|POLLNVAL))
				close_fd(i);

			/* Close any file descriptors that sent EOF. */
			else if (fds[i].revents != 0) {
				if (fds_data[i].fn != NULL && (*fds_data[i].fn)(fds[i].fd, &fds_data[i]) <= 0)
					close_fd(i);
				else
					connected++;
			}

			/* Close any file descriptors that exceed the time-to-live. */
			else if (disconnect_timeout <= age)
				close_fd(i);

			/* Otherwise count open file descriptors. */
			else if (fds[i].fd != -1)
				connected++;
		}

		/* Shorten the table's used length if we can for poll(). */
		for (i = fds_length; 0 < --i; ) {
			if (fds[i].fd != -1)
				break;
		}
		if (i+1 != fds_length)
			fds_length = i+1;

		syslog(LOG_INFO, "connected=%d", connected);
		poll_timeout_ms = connected <= 0 ? INFTIM : (disconnect_timeout * 1000);
	}

	syslog(LOG_INFO, "terminating...");
	rc = EXIT_SUCCESS;
error3:
	socketClose(socket);
	(void) unlink(SOCKET_SINK_SOCKET);
error2:
	free(socket_address);
error1:
	for (i = 1; i < fds_length; i++)
		close_fd(i);
	free(fds);
error0:
	fputc('\n', stdout);
	closelog();

	return rc;
}
