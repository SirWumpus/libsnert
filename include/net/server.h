/*
 * server.h
 *
 * Threaded Server API
 *
 * Copyright 2008, 2009 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_net_server_h__
#define __com_snert_lib_net_server_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef NDEBUG
# define NVALGRIND
#endif
#include <org/valgrind/valgrind.h>
#include <org/valgrind/memcheck.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# undef _GNU_SOURCE
# define _GNU_SOURCE
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/time62.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifndef SERVER_STACK_SIZE
# define SERVER_STACK_SIZE		(64 * 1024)
# if SERVER_STACK_SIZE < PTHREAD_STACK_MIN
#  undef SERVER_STACK_SIZE
#  define SERVER_STACK_SIZE		PTHREAD_STACK_MIN
# endif
#endif

#ifndef SERVER_MIN_THREADS
#define SERVER_MIN_THREADS		10
#endif

#ifndef SERVER_SPARE_THREADS
#define SERVER_SPARE_THREADS		10
#endif

#ifndef SERVER_MAX_THREADS
#define SERVER_MAX_THREADS		100
#endif

#ifndef SERVER_QUEUE_SIZE
#define SERVER_QUEUE_SIZE		10
#endif

#ifndef SERVER_ACCEPT_TO
#define SERVER_ACCEPT_TO		10000
#endif

#ifndef SERVER_READ_TO
#define SERVER_READ_TO			30000
#endif

#ifndef SERVER_LINE_WRAP
#define SERVER_LINE_WRAP		72
#endif

#ifndef SERVER_STOP_TIMEOUT
#define SERVER_STOP_TIMEOUT		10
#endif

#define SERVER_FILE_LINENO		__FILE__, __LINE__

#if defined(__MINGW32__)
# ifndef SIGQUIT
#  define SIGQUIT			3
# endif
# ifndef SIGKILL
#  define SIGKILL			9
# endif
#endif /* __MINGW32__ */

#define SOCKET_SET_NAGLE(s, f)		0

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct server Server;
typedef struct server_worker ServerWorker;
typedef struct server_session ServerSession;

typedef int (*ServerHook)(Server *server);
typedef int (*ServerWorkerHook)(ServerWorker *worker);
typedef int (*ServerSessionHook)(ServerSession *session);

struct server_worker {
	/* Private */
	unsigned id;
	ListItem node;
	pthread_t thread;
	volatile int running;
#ifdef __WIN32__
	HANDLE kill_event;
#endif
	/* Public */
	void *data;
	Server *server;
	ServerSession *session;
};

typedef struct {
	unsigned level;
	unsigned valgrind;
} ServerDebug;

typedef struct {
	const char *interfaces;			/* semi-colon separated list of IP addresses (copy) */
	unsigned min_threads;
	unsigned max_threads;
	unsigned spare_threads;			/* aka spare_threads */
	unsigned queue_size;			/* server socket queue size */
	unsigned accept_to;			/* accept timeout */
	unsigned read_to;			/* read timeout */
	unsigned port;				/* default port, if not specified in interfaces */
} ServerOptions;

typedef struct {
	ServerHook server_start;		/* serverStart */
	ServerHook server_stop;			/* serverStop */
	ServerWorkerHook worker_create;		/* serverAccept > serverWorkerCreate */
	ServerWorkerHook worker_cancel;		/* serverStop   > serverWorkerCancel */
	ServerWorkerHook worker_free;		/* serverWorker > serverWorkerFree */
	ServerSessionHook session_create;	/* serverAccept > sessionCreate	*/
	ServerSessionHook session_accept;	/* serverAccept > sessionAccept	*/
	ServerSessionHook session_process;	/* serverWorker			*/
	ServerSessionHook session_free;		/* serverWorker > sessionFree	*/
} ServerHooks;

typedef struct {
	Socket2 *socket;
	char name[DOMAIN_SIZE];
} ServerInterface;

struct server_session {
	/* Private state. */
	ListItem node;
	pthread_t thread;
	ServerInterface *iface;
#if defined(__WIN32__)
	HANDLE kill_event;
#endif
	/* Public data. */
	void *data;			/* Application specific session data. */
	char id_log[20];		/* Session ID suitable for logging. */
	unsigned short id;		/* Session ID of session */
	time_t start;			/* Time session was started. */
	Server *server;
	Socket2 *client;
	ServerWorker *worker;
	unsigned char ipv6[IPV6_BYTE_SIZE];
	char address[SOCKET_ADDRESS_STRING_SIZE];
	char if_addr[SOCKET_ADDRESS_STRING_SIZE];
};

struct server {
	/* Private state. */
	Vector interfaces;		/* Vector of ServerInterface pointers. */
	SOCKET *interfaces_fd;		/* Used for timeouts */
	SOCKET *interfaces_ready;	/* Used for timeouts */

	volatile int running;

	Queue workers;			/* All worker threads. */
	unsigned workers_active;	/* workers.mutex used to control access. */

	Queue sessions_queued;		/* Client sessions queued by accept thread. */
	pthread_t accept_thread;
	pthread_attr_t thread_attr;

	/* Public data. */
	void *data;			/* Application specific server data. */
	unsigned id;
	ServerHooks hook;		/* Application call-back hooks. */
	ServerDebug debug;
	ServerOptions option;		/* Set options prior to serverRun */
};

extern Server *serverCreate(const char *address_list, unsigned default_port);
extern void serverFree(void *_server);

extern int serverInit(Server *server, const char *address_list, unsigned default_port);
extern void serverFini(Server *server);

extern int serverSetStackSize(Server *server, size_t stack_size);
extern int serverStart(Server *server);
extern int serverWorkerIsTerminated(ServerWorker *worker);
extern int serverSessionIsTerminated(ServerSession *session);
extern void serverStop(Server *server, int slow_quit);

/**
 * Defined by the application and is called by Windows via ServiceMain()
 * handler setup by winServiceStart(). Can be called by main() when
 * starting in application console (non-daemon) mode.
 *
 * @return
 *	Either EXIT_SUCCESS or EXIT_FAILURE.
 */
extern int serverMain(void);

/**
 * Defined by the application and is called by Windows via ServiceMain()
 * handler setup by winServiceStart(). Can be called by main() when
 * starting in application console (non-daemon) mode.
 *
 * @param argc
 *	Number of string arguments in argv.
 *
 * @param argv
 *	An array of pointers to C strings to command line arguments.
 *	Argument 0 is the command / executable name. Array is NULL
 *	terminated.
 */
extern void serverOptions(int argc, char **argv);

/**
 */
extern void printVar(int columns, const char *name, const char *value);

/*
 * To be called by pthread_atfork handlers.
 */
extern void serverAtForkPrepare(Server *server);
extern void serverAtForkParent(Server *server);
extern void serverAtForkChild(Server *server);

#ifdef __WIN32__
typedef enum {
	SIGNAL_QUIT = 0,
	SIGNAL_TERM = 1,
	SIGNAL_LENGTH = 2
} ServerSignal;
#endif

typedef void (*ServerSignalHook)(int signum);

typedef struct {
#ifdef __unix__
	sigset_t signal_set;
#endif
#ifdef __WIN32__
	HANDLE signal_event[SIGNAL_LENGTH];
#endif
	ServerSignalHook sig_hup;
} ServerSignals;

extern int serverSignalsInit(ServerSignals *signals);
extern int serverSignalsLoop(ServerSignals *signals);
extern void serverSignalsFini(ServerSignals *signals);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_net_server_h__ */

