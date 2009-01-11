/*
 * here-i-am.c/1.0
 *
 * Copyright 2004 by Anthony Howe. All rights reserved.
 *
 *
 * Description
 * -----------
 *
 * 	here-i-am port response
 *
 * A server that waits for broadcasted "where are you" UDP messages
 * from clients to the given port. When a message is recieved the server
 * responds to the client with the given response message.
 *
 * Build for Windows using Borland C 5.5
 * -------------------------------------
 *
 *	bcc32 -ohere-i-am here-i-am.c
 *
 *
 * Build for Unix using GCC
 * ------------------------
 *
 *	gcc -02 -o here-i-am here-i-am.c
 */

/***********************************************************************
 *** Leave this header alone. Its generated from the configure script.
 ***********************************************************************/


/***********************************************************************
 *** You can change the stuff below if the configure script doesn't work.
 ***********************************************************************/


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

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define _NAME		"here-i-am"
#define _VERSION	"here-i-am/1.0"

#define IPV6_LENGTH	((128/16)*4+(128/16)-1)

#define WORKSPACE				\
	char packet[512];			\
	long packet_length;			\
	struct sockaddr_in addr;		\
	socklen_t addr_length;			\
	char ip_string[IPV6_LENGTH+1]

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static char where_are_you[] = "Where are you?";
static char here_i_am[] = "Here I am.";

static FILE *LOG;
static int debug;
static char *user_id = NULL;
static char *group_id = NULL;
static char *log_file = NULL;
static char *pid_file = _NAME ".pid";
static char *server_port = NULL;
static char *server_response = here_i_am;

static char *usage =
"\033[1musage: " _NAME " [-v][-g group][-l log][-m message][-u user] port\033[0m\n"
"\n"
"-g group\trun as this group\n"
"-l log\t\tlog file to create or stderr (default none)\n"
"-m message\tthe response message to send\n"
"-u user\t\trun as this user\n"
"-v\t\tverbose debug messages\n"
"port\t\tthe port number to listen on\n"
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
# include <dir.h>
# include <sys/locking.h>
extern long getpid(void);
#endif

# define UPDATE_ERRNO	if (errno == 0)	errno = WSAGetLastError()

typedef int socklen_t;

int
ThreadCreate(void *(*fn)(void *), void *data)
{
	DWORD id;

	return -(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) fn, data, 0, &id) == 0);
}

# define SHUT_RD	SD_RECEIVE
# define SHUT_WR	SD_SEND
# define SHUT_RDWR	SD_BOTH

SOCKET server;

typedef struct {
	WORKSPACE;
} Connection;

char *
GetErrorMessage(DWORD code)
{
	int length;
	char *error;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &error, 0, NULL
	);

	for (length = strlen(error)-1; 0 <= length && isspace(error[length]); length--)
		error[length] = '\0';

	return error;
}

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

#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#if defined(__CYGWIN__)
# include <io.h>
#endif

#define UPDATE_ERRNO

typedef int SOCKET;

#ifdef __SUNOS__
typedef size_t socklen_t;
#endif

#define INVALID_SOCKET	(-1)
#define closesocket	close

SOCKET server;

typedef struct {
	WORKSPACE;
} Connection;

int
ThreadCreate(void *(*fn)(void *), void *data)
{
	int rc;
	pthread_t thread;

	rc = pthread_create(&thread, (pthread_attr_t *) 0, fn, data);
#ifndef NDEBUG
	appLog("[%d] ThreadCreate(fn=%lx, data=%lx) rc=%d thread=%lx", getpid(), (long) fn, (long) data, rc, thread);
#endif
	return rc;
}

#endif

/***********************************************************************
 *** Common
 ***********************************************************************/

static pthread_mutex_t theMutex;

#ifndef HAVE_INET_NTOP
void
getIp4Octets(unsigned long ip, int *a, int *b, int *c, int *d)
{
	*a = (ip >> 24) & 0xff;
	*b = (ip >> 16) & 0xff;
	*c = (ip >> 8 ) & 0xff;
	*d = (ip      ) & 0xff;
}

const char *
my_inet_ntop(int af, const void *in_addr, char *buffer, size_t size)
{
	int a, b, c, d;

	getIp4Octets(ntohl(((struct in_addr *) in_addr)->s_addr), &a, &b, &c, &d);
	(void) snprintf(buffer, size, "%u.%u.%u.%u", a,b,c,d);

	return buffer;
}
#else
#define my_inet_ntop			inet_ntop
#endif /* HAVE_INET_NTOP */

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

	now = time((time_t *) 0);
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

void
signalExit(int signum)
{
	signal(signum, SIG_IGN);
	appLog("[%d] signal %d received, program exit", getpid(), signum);
	exit(0);
}

void *
worker(void *data)
{
	long nbytes;
	Connection *conn;

	conn = (Connection *) data;

	my_inet_ntop(conn->addr.sin_family, &conn->addr.sin_addr, conn->ip_string, sizeof (conn->ip_string));

	if (conn->packet_length < 0) {
		appLog("[%d] received a negative packet (%ld) from [%s]", getpid(), conn->packet_length, conn->ip_string);
		goto error0;
	}

	if (*conn->packet != '?' && strncmp(conn->packet, where_are_you, conn->packet_length) != 0) {
		appLog("[%d] an unknown packet received from [%s]", getpid(), conn->ip_string);
		goto error0;
	}

	appLog("[%d] packet={%.512s} from=[%s]", getpid(), conn->packet, conn->ip_string);

	(void) strncpy(conn->packet, server_response, sizeof (conn->packet));
	conn->packet_length = strlen(server_response);
	if ((long) sizeof (conn->packet) < conn->packet_length)
		conn->packet_length = sizeof (conn->packet);

	(void) pthread_mutex_lock(&theMutex);

	nbytes = sendto(
		server, conn->packet, conn->packet_length, 0,
		(const struct sockaddr *) &conn->addr, conn->addr_length
	);

	(void) pthread_mutex_unlock(&theMutex);

	if (nbytes < 0)
		appLog("[%d] error sending response to [%s]: %s (%d)", getpid(), conn->ip_string, strerror(errno), errno);
error0:
	free(conn);

	return NULL;
}

int
main(int argc, char **argv)
{
	FILE *fp;
	Connection *conn;
	int i, argi, on = 1;
	struct sockaddr_in addr;

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
		case 'm':
			server_response = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'u':
			user_id = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'g':
			group_id = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
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
	memset(&addr, 0, sizeof (addr));
	i = (int) strtol(server_port, NULL, 10);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((unsigned short) i);
	addr.sin_family = AF_INET;

	if ((server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
		UPDATE_ERRNO;
		appLog("[%d] failed to create socket: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}

#ifdef SET_BROADCAST
	if (setsockopt(server, SOL_SOCKET, SO_BROADCAST, (char *) &on, sizeof (on))) {
		UPDATE_ERRNO;
		appLog("[%d] failed to set socket broadcast option: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}
#endif
	if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof (on))) {
		UPDATE_ERRNO;
		appLog("[%d] failed to set socket re-use address option: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}

	if (bind(server, /*(const struct sockaddr *) */ (void *) &addr, (socklen_t) sizeof (addr)) < 0) {
		UPDATE_ERRNO;
		appLog("[%d] failed to bind to port: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}
#if defined(__unix__)
{
	struct group *gr;
	struct passwd *pw;

	if (getuid() == 0) {
		if (group_id != NULL) {
			if ((gr = getgrnam(group_id)) == NULL) {
				appLog("[%d] group \"%s\" not found\n", getpid(), group_id);
				return 1;
			}
			(void) setgid(gr->gr_gid);
		}

		if (user_id != NULL) {
			if ((pw = getpwnam(user_id)) == NULL) {
				appLog("[%d] user \"%s\" not found\n", getpid(), user_id);
				return 1;
			}
			(void) setuid(pw->pw_uid);
		}
	}

	appLog("[%d] process uid=%d gid=%d", getpid(), getuid(), getgid());
# ifdef HANDLE_SIGNALS
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		appLog("[%d] failed to set SIGIPE handler", getpid());
		return 1;
	}
#  ifndef NDEBUG
	appLog("[%d] SIGPIPE ignored", getpid());
#  endif
	if (signal(SIGTERM, signalExit) == SIG_ERR) {
		appLog("[%d] failed to set SIGTERM handler: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}

#  ifndef NDEBUG
	appLog("[%d] SIGTERM set", getpid());
#  endif
	if (signal(SIGINT, signalExit) == SIG_ERR) {
		appLog("[%d] failed to set SIGINT handler: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}

#  ifndef NDEBUG
	appLog("[%d] SIGINT set", getpid());
#  endif
#  ifdef MASK_SIGNALS_FROM_THREAD
{
	sigset_t set;

	(void) sigemptyset(&set);
	(void) sigaddset(&set, SIGINT);
	(void) sigaddset(&set, SIGTERM);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL)) {
		appLog("[%d] failed to mask SIGTERM from other threads: %s (%d)", getpid(), strerror(errno), errno);
		return 1;
	}
}
#  endif
# endif
}
#endif
	/* Create this in the state directory. */
	if ((fp = fopen(pid_file, "w")) == NULL) {
		appLog("[%d] failed to create pid file \"%s\": %s (%d)", getpid(), pid_file, strerror(errno), errno);
		return 1;
	}

	(void) fprintf(fp, "%d\n", getpid());
	(void) fclose(fp);
#ifndef NDEBUG
	(void) appLog("[%d] pid file '%s' created", getpid(), pid_file);
#endif
	appLog("[%d] listening on port %s", getpid(), server_port);

	/* Use "kill" to terminate the parent server process. */
	while ((conn = calloc(1, sizeof (*conn))) != NULL) {
		/* Wait for a request. */
		errno = 0;
		conn->addr_length = sizeof (conn->addr);
		conn->packet_length = recvfrom(
			server, conn->packet, sizeof (conn->packet), 0,
			(struct sockaddr *) &conn->addr, &conn->addr_length
		);

		if (conn->packet_length < 0) {
			UPDATE_ERRNO;
			appLog("[%d] socket read error: %s (%d)", getpid(), strerror(errno), errno);
			free(conn);
		}

		else if (ThreadCreate(worker, conn)) {
			/* Thread failed to start, abandon the client. */
			appLog("[%d] failed to create thread: %s, (%d)", getpid(), strerror(errno), errno);
			free(conn);
		}
	}

	if (errno == ENOMEM)
		(void) appLog("[%d] server out of memory\n", getpid());
	(void) unlink(pid_file);

	return 1;
}


