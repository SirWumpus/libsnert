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
# ifdef __linux__
#  /* See Linux man setresgid */
#  define _GNU_SOURCE
# endif
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/time62.h>

/***********************************************************************
 ***
 ***********************************************************************/

#undef OLD_THREAD_MODEL

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

#ifndef SERVER_NEW_THREADS
#define SERVER_NEW_THREADS		10
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

#define SERVER_FILE_LINENO		__FILE__, __LINE__

#if defined(__MINGW32__)
# ifndef SIGQUIT
#  define SIGQUIT			3
# endif
# ifndef SIGKILL
#  define SIGKILL			9
# endif
#endif /* __MINGW32__ */

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct server Server;
typedef struct server_session Session;
typedef struct server_list ServerList;
typedef int (*ServerHook)(Server *server);
typedef int (*SessionHook)(Session *session);
typedef int (*ServerListHook)(ServerList *list);

typedef struct {
	ServerListHook list_empty;
} ServerListHooks;

typedef struct server_queue_data {
	struct server_queue_data *prev;
	struct server_queue_data *next;
	void *data;
} ServerListData;

struct server_list {
	/* Private state. */
	unsigned length;
	ServerListData *tail;
	ServerListData *head;
	pthread_mutex_t mutex;
	pthread_cond_t cv;

	/* Public */
	ServerListHooks	hook;
};

typedef struct {
	unsigned id;
	Server *server;
	pthread_t thread;
#ifdef __WIN32__
	HANDLE kill_event;
#endif
	ServerListData *node;
} ServerWorker;

typedef struct {
	unsigned level;
	unsigned valgrind;
} ServerDebug;

typedef struct {
	const char *interfaces;		/* semi-colon separated list of IP addresses */
	unsigned min_threads;
	unsigned max_threads;
	unsigned new_threads;
	unsigned queue_size;		/* server socket queue size */
	unsigned accept_to;		/* accept timeout */
	unsigned read_to;		/* read timeout */
	unsigned port;			/* default port, if not specified in interfaces */
} ServerOptions;

typedef struct {
#ifdef OLD_THREAD_MODEL
	ServerHook server_connect;	/* serverCheckThreadPool, connections_mutex locked */
	ServerHook server_disconnect;	/* sessionFinish, connections_mutex locked */
#endif
	SessionHook session_create;	/* sessionCreate */
#ifdef OLD_THREAD_MODEL
	SessionHook session_accept;	/* sessionAccept, accept_mutex locked */
#endif
	SessionHook session_process;	/* serverChild */
#ifdef OLD_THREAD_MODEL
	SessionHook session_finish;	/* serverChild */
#endif
	SessionHook session_free;	/* sessionFree */
} ServerHooks;

typedef struct {
	Socket2 *socket;
	char name[DOMAIN_STRING_LENGTH+1];
} ServerInterface;

struct server_session {
	/* Private state. */
	Session *prev;
	Session *next;
	ServerInterface *iface;
	pthread_t thread;
#if defined(__WIN32__)
	HANDLE kill_event;
#endif
	/* Public data. */
	void *data;			/* Application specific session data. */
	char id[20];			/* Session ID suitable for logging. */
	time_t start;			/* Time session was started. */
	Server *server;
	Socket2 *client;
	char address[IPV6_STRING_LENGTH];
	unsigned char ipv6[IPV6_BYTE_LENGTH];
	char if_addr[IPV6_STRING_LENGTH+8];
};

struct server {
	/* Private state. */
	Vector interfaces;		/* Vector of ServerInterface pointers. */
	SOCKET *interfaces_fd;		/* Used for timeouts */
	SOCKET *interfaces_ready;	/* Used for timeouts */

	volatile int running;

#ifdef OLD_THREAD_MODEL
	Session *head;

	volatile unsigned threads;
	volatile unsigned connections;
	pthread_mutex_t accept_mutex;
	pthread_mutex_t connections_mutex;
#else
	ServerList workers;		/* Active worker threads. */
	ServerList workers_idle;	/* Pool of worker threads to process sessions. */
	ServerList sessions_queued;	/* Client sessions queued by accept thread. */
	pthread_t accept_thread;
#endif
	pthread_attr_t thread_attr;

#ifdef HAVE_PTHREAD_COND_INIT
	pthread_cond_t slow_quit_cv;
	pthread_mutex_t slow_quit_mutex;
#endif
	/* Public data. */
	void *data;			/* Application specific server data. */
	unsigned id;
	ServerHooks hook;		/* Application call-back hooks. */
	ServerDebug debug;
	ServerOptions option;		/* Set options prior to serverRun */
};

extern Server *serverCreate(const char *address_list, unsigned default_port);
extern int serverSetStackSize(Server *server, size_t stack_size);
extern int serverStart(Server *server);
extern int sessionIsTerminated(Session *session);
extern void serverStop(Server *server, int slow_quit);
extern void serverFree(void *_server);

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

typedef struct {
#ifdef __unix__
	sigset_t signal_set;
#endif
#ifdef __WIN32__
	HANDLE signal_thread_event;
#endif
} ServerSignals;

extern int serverSignalsInit(ServerSignals *signals, const char *name);
extern int serverSignalsLoop(ServerSignals *signals);
extern void serverSignalsFini(ServerSignals *signals);

extern int serverListInit(ServerList *list);
extern void serverListFini(ServerList *list);
extern void *serverListRemove(ServerList *list, ServerListData *node);
extern ServerListData *serverListEnqueue(ServerList *list, void *data);
extern void *serverListDequeue(ServerList *list);
extern unsigned serverListLength(ServerList *list);
extern int serverListIsEmpty(ServerList *list);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_net_server_h__ */

