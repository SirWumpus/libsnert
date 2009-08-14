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
typedef struct server_list ServerList;
typedef struct server_worker ServerWorker;
typedef struct server_session ServerSession;

typedef int (*ServerHook)(Server *server);
typedef int (*ServerListHook)(ServerList *list);
typedef int (*ServerWorkerHook)(ServerWorker *worker);
typedef int (*ServerSessionHook)(ServerSession *session);

typedef struct {
	ServerListHook list_empty;
} ServerListHooks;

typedef struct server_list_node {
	struct server_list_node *prev;
	struct server_list_node *next;
	void *data;
} ServerListNode;

struct server_list {
	/* Private state. */
	unsigned length;
	ServerListNode *tail;
	ServerListNode *head;
	pthread_mutex_t mutex;
	pthread_cond_t cv;

	/* Public */
	ServerListHooks	hook;
};

struct server_worker {
	/* Private */
	unsigned id;
	pthread_t thread;
#ifdef __WIN32__
	HANDLE kill_event;
#endif
	/* Public */
	void *data;
	Server *server;
	ServerListNode *node;
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
	unsigned new_threads;			/* aka spare_threads */
	unsigned queue_size;			/* server socket queue size */
	unsigned accept_to;			/* accept timeout */
	unsigned read_to;			/* read timeout */
	unsigned port;				/* default port, if not specified in interfaces */
} ServerOptions;

typedef struct {
	ServerWorkerHook worker_create;		/* serverAccept, serverWorkerCreate */
	ServerWorkerHook worker_free;		/* serverWorker, serverWorkerFree */
	ServerSessionHook session_create;	/* serverAccept, sessionCreate	*/
	ServerSessionHook session_accept;	/* serverAccept, sessionAccept	*/
	ServerSessionHook session_process;	/* serverWorker			*/
	ServerSessionHook session_free;		/* serverWorker, sessionFree	*/
} ServerHooks;

typedef struct {
	Socket2 *socket;
	char name[DOMAIN_STRING_LENGTH+1];
} ServerInterface;

struct server_session {
	/* Private state. */
	ServerInterface *iface;
	pthread_t thread;
#if defined(__WIN32__)
	HANDLE kill_event;
#endif
	/* Public data. */
	void *data;			/* Application specific session data. */
	unsigned id;			/* Session ID of session */
	char id_log[20];		/* Session ID suitable for logging. */
	time_t start;			/* Time session was started. */
	Server *server;
	Socket2 *client;
	ServerWorker *worker;
	unsigned char ipv6[IPV6_BYTE_LENGTH];
	char address[SOCKET_ADDRESS_STRING_SIZE];
	char if_addr[SOCKET_ADDRESS_STRING_SIZE];
};

struct server {
	/* Private state. */
	Vector interfaces;		/* Vector of ServerInterface pointers. */
	SOCKET *interfaces_fd;		/* Used for timeouts */
	SOCKET *interfaces_ready;	/* Used for timeouts */

	volatile int running;

	ServerList workers;		/* Active worker threads. */
	ServerList workers_idle;	/* Pool of worker threads to process sessions. */
	ServerList sessions_queued;	/* Client sessions queued by accept thread. */
	pthread_t accept_thread;
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
extern void serverFree(void *_server);

extern int serverInit(Server *server, const char *address_list, unsigned default_port);
extern void serverFini(Server *server);

extern int serverSetStackSize(Server *server, size_t stack_size);
extern int serverStart(Server *server);
extern int sessionIsTerminated(ServerSession *session);
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
extern void *serverListRemove(ServerList *list, ServerListNode *node);
extern ServerListNode *serverListEnqueue(ServerList *list, void *data);
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

