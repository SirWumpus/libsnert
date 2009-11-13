/*
 * where-are-you.c/1.0
 *
 * Copyright 2004 by Anthony Howe. All rights reserved.
 *
 *
 * Description
 * -----------
 *
 * 	where-are-you port
 *
 * A client that broadcasts a "where are you" UDP message on the local
 * subnet to the given port. If a server responds, writes to standard
 * output the server's IP address.
 *
 * Build for Windows using Borland C 5.5
 * -------------------------------------
 *
 *	bcc32 -owhere-are-you where-are-you.c
 *
 *
 * Build for Unix using GCC
 * ------------------------
 *
 *	gcc -02 -o where-are-you where-are-you.c
 */

/***********************************************************************
 *** Leave this header alone. Its generated from the configure script.
 ***********************************************************************/


/***********************************************************************
 *** You can change the stuff below if the configure script doesn't work.
 ***********************************************************************/

#ifndef MAX_BROADCAST_ATTEMPTS
#define MAX_BROADCAST_ATTEMPTS		60
#endif

#ifndef DEFAULT_TIMEOUT
#define DEFAULT_TIMEOUT			3000
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef __APPLE__
#define __unix__	1
#endif

#ifdef __unix__
# include <sys/time.h>
#endif

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define _NAME		"where-are-you"
#define _VERSION	_NAME "/1.0"

#ifndef IPV4_BYTE_LENGTH
#define IPV4_BYTE_LENGTH	4
#endif

#ifndef IPV6_BIT_LENGTH
#define IPV6_BIT_LENGTH		128
#endif

#ifndef IPV6_BYTE_LENGTH
#define IPV6_BYTE_LENGTH	16
#endif

#ifndef IPV6_STRING_LENGTH
/* Space for a full-size IPv6 string; 8 groups of 4 character hex
 * words (16-bits) separated by colons and terminating NULL byte.
 */
#define IPV6_STRING_LENGTH	(IPV6_BIT_LENGTH/16*5)
#endif

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static FILE *LOG;
static int debug;
static char *log_file = NULL;
static char *server_port = NULL;
static char where_are_you[] = "Where are you?";

static char *usage =
"\033[1musage: " _NAME " [-v][-l log] port\033[0m\n"
"\n"
"-l log\t\tlog file to create or stderr (default none)\n"
"-v\t\tverbose debug messages\n"
"port\t\tthe port number to broadcast for\n"
"\n"
"\033[1m" _VERSION " Copyright 2004 by Anthony Howe. All rights reserved.\033[0m\n"
;

void appLogV(char *fmt, va_list args);
void appLog(char *fmt, ...);

#ifdef __WIN32__
/***********************************************************************
 *** Windows
 ***********************************************************************/

#include <windows.h>
#include <winsock2.h>

#if defined(__BORLANDC__)
# include <io.h>
extern long getpid(void);
extern unsigned int sleep(unsigned int);
#endif

typedef int socklen_t;

SOCKET server;

typedef HANDLE pthread_mutex_t;
typedef long pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER	0

int
pthread_mutex_init(pthread_mutex_t *handle, const pthread_mutexattr_t *attr)
{
	if ((*(volatile pthread_mutex_t *) handle = CreateMutex(NULL, FALSE, NULL)) == (HANDLE) 0)
		return -1;

	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *handle)
{
	if (*(volatile pthread_mutex_t *)handle == PTHREAD_MUTEX_INITIALIZER) {
		if (pthread_mutex_init(handle, NULL))
			return -1;
	}

	switch (WaitForSingleObject(*(volatile pthread_mutex_t *) handle, INFINITE)) {
	case WAIT_TIMEOUT:
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		return 0;
	case WAIT_FAILED:
		return -1;
	}

	return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *handle)
{
	if (ReleaseMutex(*(volatile pthread_mutex_t *) handle) == 0)
		return -1;

	return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *handle)
{
	if (*(volatile pthread_mutex_t *)handle != (HANDLE) 0)
		CloseHandle(*(volatile pthread_mutex_t *) handle);

	return 0;
}

#else
/***********************************************************************
 *** POSIX
 ***********************************************************************/

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#if defined(__CYGWIN__)
# include <io.h>
#endif

typedef int SOCKET;

#ifdef __sun__
typedef size_t socklen_t;
#endif

#define INVALID_SOCKET	(-1)
#define closesocket	close

SOCKET server;

#endif

/***********************************************************************
 *** Common
 ***********************************************************************/

static pthread_mutex_t theMutex;

/**
 * @param ip
 *	An IP address in network byte order.
 *
 * @param ip_length
 *	The length of the IP address, which is either IPV4_BYTE_LENGTH (4)
 *	or IPV6_BYTE_LENGTH (16).
 *
 * @param compact
 *	If true and the ip argument is an IPv6 address, then the compact
 *	IPv6 address form will be written into buffer. Otherwise the full
 *	IP address is written to buffer.
 *
 * @param buffer
 *	The buffer for the IP address string. The buffer is always null
 *	terminated.
 *
 * @param size
 *	The size of the buffer, which should be at least IPV6_STRING_LENGTH.
 *
 * @return
 *	The length of the formatted address, excluding the terminating null
 *	byte if the buffer were of infinite size. If the return value is
 *	greater than or equal to the buffer size, then the contents of the
 *	buffer are truncated.
 */
long
formatIP(unsigned char *ip, int ip_length, int compact, char *buffer, long size)
{
	int i, z;
	long length;

	if (ip == NULL || buffer == NULL) {
		errno = EFAULT;
		return 0;
	}

	if (ip_length == IPV4_BYTE_LENGTH)
		return snprintf(buffer, size, "%d.%d.%d.%d", ip[0],ip[1],ip[2],ip[3]);

	if (ip_length != IPV6_BYTE_LENGTH)
		return 0;

	for (z = 0; z < IPV6_BYTE_LENGTH; z++)
		if (ip[z] != 0)
			break;

	length = 0;

	if (compact && 4 <= z) {
		length += snprintf(buffer, size, "::");
		compact = 0;
	}

	for (i = z; i < IPV6_BYTE_LENGTH; i += 2) {
		if (compact) {
			for (z = i; z < IPV6_BYTE_LENGTH; z++)
				if (ip[z] != 0)
					break;
			if (4 <= z - i) {
				length += snprintf(buffer+length, size-length, "::");
				compact = 0;
				i = z-2;
				continue;
			}
		}

		length += snprintf(buffer+length, size-length, "%x:", (ip[i] << 8) | ip[i+1]);
	}

	/* Remove trailing colon. */
	if (0 < length && length < size)
		buffer[--length] = '\0';

	return length;
}


void
atExitCleanUp(void)
{
	(void) pthread_mutex_unlock(&theMutex);
	(void) pthread_mutex_destroy(&theMutex);
#if defined(__WIN32__)
	WSACleanup();
#endif
}

void
appLogV(char *fmt, va_list args)
{
	time_t now;
	struct tm *local;
	char stamp[30]; /* yyyy-mm-dd HH:MM:SS (length 21) */

	if (LOG == NULL)
		return;

	(void) pthread_mutex_lock(&theMutex);

	now = time(NULL);
	local = localtime(&now);
	(void) strftime(stamp, sizeof stamp-1, "%d %b %Y %H:%M:%S ", local);
	fprintf(LOG, stamp);
	vfprintf(LOG, fmt, args);
	fprintf(LOG, "\n");

	(void) pthread_mutex_unlock(&theMutex);
}

void
appLog(char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	appLogV(fmt, args);
	va_end(args);
}

int
main(int argc, char **argv)
{
	long nbytes;
	fd_set readset;
	socklen_t addr_length;
	int i, rc, argi, on = 1;
	struct timeval timeout, ltimeout;
	struct sockaddr_in addr_send, addr_recv;
	char packet[512], ip_string[IPV6_STRING_LENGTH];

#if defined(__BORLANDC__)
	_fmode = O_BINARY;
#endif
#if defined(__BORLANDC__) || defined(__CYGWIN__)
	setmode(0, O_BINARY);
	setmode(1, O_BINARY);
#endif

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-')
			break;

		switch (argv[argi][1]) {
		case 'l':
			log_file = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'v':
			debug = 1;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return 2;
		}
	}

	if (argi+1 != argc) {
		fprintf(stderr, "missing arguments\n%s", usage);
		return 2;
	}

	server_port = argv[argi];

	if (log_file != NULL && *log_file != '\0') {
		if (strcmp(log_file, "stderr") == 0)
			LOG = stderr;
		else if ((LOG = fopen(log_file, "a")) != NULL)
			setvbuf(LOG, NULL, _IOLBF, 0);
		else
			return 1;
	}

	appLog("[%d] " _VERSION " Copyright 2004 by Anthony Howe.", getpid());
	appLog("[%d] All rights reserved.", getpid());

	if (atexit(atExitCleanUp)) {
		appLog("[%d] atexit() failed\n", getpid());
		return 1;
	}

	if (pthread_mutex_init(&theMutex, NULL)) {
		appLog("[%d] failed to initialise mutex", getpid());
		return 1;
	}
#if defined(__WIN32__)
{
/*
 * WinSock 2.2 provides a subset of the BSD Sockets API.
 */
 	int rc;
	WORD version;
	WSADATA wsaData;

	version = MAKEWORD(2, 2);
	if ((rc = WSAStartup(version, &wsaData)) != 0) {
		appLog("[%d] WSAStartup() failed: %d", getpid(), rc);
		return 1;
	}

	if (HIBYTE( wsaData.wVersion ) < 2 || LOBYTE( wsaData.wVersion ) < 2) {
		appLog("[%d] WinSock API must be version 2.2 or better.", getpid());
		return 1;
	}
}
#endif
	memset(&addr_send, 0, sizeof (addr_send));
	i = (int) strtol(server_port, NULL, 10);
	addr_send.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	addr_send.sin_port = htons((unsigned short) i);
	addr_send.sin_family = AF_INET;

	if ((server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		appLog("[%d] failed to create socket", getpid());
		return 1;
	}

	if (setsockopt(server, SOL_SOCKET, SO_BROADCAST, (char *) &on, sizeof (on))) {
		appLog("[%d] failed to set socket broadcast option", getpid());
		return 1;
	}

	appLog("[%d] broadcast for port %s", getpid(), server_port);

	rc = 1;
	timeout.tv_sec = DEFAULT_TIMEOUT / 1000L;
	timeout.tv_usec = (DEFAULT_TIMEOUT % 1000L) * 1000L;

	for (i = 0; i < MAX_BROADCAST_ATTEMPTS; i++) {
		nbytes = sendto(
			server, where_are_you, sizeof (where_are_you), 0,
			(const struct sockaddr *) &addr_send, sizeof (addr_send)
		);

		if (nbytes < 0) {
			appLog("[%d] socket broadcast error: %s (%d)", getpid(), strerror(errno), errno);
			sleep(timeout.tv_sec);
			continue;
		}

		FD_ZERO(&readset);
		FD_SET((unsigned) server, &readset);

		ltimeout = timeout;
		if (0 < (rc = select(server + 1, &readset, NULL, NULL, &ltimeout))) {
			addr_length = sizeof (addr_recv);
			nbytes = recvfrom(
				server, packet, sizeof (packet), 0,
				(struct sockaddr *) &addr_recv, &addr_length
			);

			if (nbytes < 0) {
				appLog("[%d] socket read error: %s (%d)", getpid(), strerror(errno), errno);
				if (errno == EFAULT || errno == EBADF)
					break;
				continue;
			}

			formatIP((unsigned char *) &addr_recv.sin_addr, IPV4_BYTE_LENGTH, 1, ip_string, sizeof (ip_string));
			printf("packet={%.*s} from=[%s] \n", (int) nbytes, packet, ip_string);
			rc = 0;
			break;
		}
	}

	close(server);

	return rc;
}


