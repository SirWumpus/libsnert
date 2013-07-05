/*
 * server.c
 *
 * Threaded Server API
 *
 * Copyright 2008, 2013 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if defined(HAVE_FCNTL_H)
# include <fcntl.h>
#endif

#if defined(__MINGW32__)
# define HAVE_PTHREAD_CREATE
# undef HAVE_SIGSET_T
#else
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif /* __MINGW32__ */

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/sys/pid.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/net/server.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/timer.h>

#ifdef __WIN32__
# include <windows.h>
# include <sddl.h>
#endif

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

static const char log_oom[] = "out of memory %s(%d)";
static const char log_init[] = "init error %s(%d): %s (%d)";
static const char log_internal[] = "internal error %s(%d): %s (%d)";
static const char log_buffer[] = "buffer overflow %s(%d)";

/***********************************************************************
 *** Support Routines
 ***********************************************************************/

void
printVar(int columns, const char *name, const char *value)
{
	int length;
	Vector list;
	const char **args;

	if (columns <= 0)
		printf("%s=\"%s\"\n",  name, value);
	else if ((list = TextSplit(value, " \t", 0)) != NULL && 0 < VectorLength(list)) {
		args = (const char **) VectorBase(list);

		length = printf("%s=\"'%s'", name, *args);
		for (args++; *args != NULL; args++) {
			/* Line wrap. */
			if (columns <= length + strlen(*args) + 4) {
				(void) printf("\n\t");
				length = 8;
			}
			length += printf(" '%s'", *args);
		}
		if (columns <= length + 1) {
			(void) printf("\n");
		}
		(void) printf("\"\n");

		VectorDestroy(list);
	}
}

/***********************************************************************
 *** Unix Signal Handling
 ***********************************************************************/

#ifdef __unix__
/*
 * Set up an event loop to wait and act on SIGPIPE, SIGHUP, SIGINT,
 * SIGQUIT, and SIGTERM. The main server thread and all other child
 * threads will ignore them. This way we can do more interesting
 * things than are possible in a typical signal handler.
 */
int
serverSignalsInit(ServerSignals *signals)
{
        (void) sigemptyset(&signals->signal_set);
# ifdef SIGHUP
        (void) sigaddset(&signals->signal_set, SIGHUP);
# endif
# ifdef SIGINT
        (void) sigaddset(&signals->signal_set, SIGINT);
# endif
# ifdef SIGPIPE
	(void) sigaddset(&signals->signal_set, SIGPIPE);
# endif
# ifdef SIGQUIT
	(void) sigaddset(&signals->signal_set, SIGQUIT);
# endif
# ifdef SIGTERM
	(void) sigaddset(&signals->signal_set, SIGTERM);
# endif
# ifdef SERVER_CATCH_ULIMIT_SIGNALS /* Block to be removed */
#  ifdef SIGXCPU
	(void) sigaddset(&signals->signal_set, SIGXCPU);
#  endif
#  ifdef SIGXFSZ
	(void) sigaddset(&signals->signal_set, SIGXFSZ);
#  endif
# endif /* SERVER_CATCH_ULIMIT_SIGNALS */

        if (pthread_sigmask(SIG_BLOCK, &signals->signal_set, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}

	return 0;
}

void
serverSignalsFini(ServerSignals *signals)
{
	(void) pthread_sigmask(SIG_UNBLOCK, &signals->signal_set, NULL);
}

int
serverSignalsLoop(ServerSignals *signals)
{
	int signal, running;

	for (running = 1; running; ) {
		signal = 0;
		if (sigwait(&signals->signal_set, &signal))
			continue;

		switch (signal) {
		case SIGINT:
		case SIGTERM:		/* Immediate termination */
		case SIGQUIT:		/* Slow quit, wait for sessions to complete. */
			running = 0;
			break;
# ifdef SIGPIPE
		case SIGPIPE:
			/* Silently ignore since we can get LOTS of
			 * these during the life of the server.
			 */
			break;
# endif
# ifdef SIGHUP
		case SIGHUP:
			if (signals->sig_hup) {
				(*signals->sig_hup)(signal);
				break;
			}
			/*@fallthrough@*/
# endif
# ifdef SERVER_CATCH_ULIMIT_SIGNALS /* Block to be removed */
#  ifdef SIGXCPU
		case SIGXCPU:
#  endif
#  ifdef SIGXFSZ
		case SIGXFSZ:
#  endif
# endif /* SERVER_CATCH_ULIMIT_SIGNALS */
			syslog(LOG_INFO, "signal %d ignored", signal);
			break;
		}
	}

	syslog(LOG_INFO, "signal %d received", signal);

	return signal;
}
#endif /* __unix__ */

/***********************************************************************
 *** Windows Signal Handling
 ***********************************************************************/

#ifdef __WIN32__

# ifdef ENABLE_SECURITY_DESCRIPTOR
/* Cygwin/Mingw do not define ConvertStringSecurityDescriptorToSecurityDescriptor().
 * This would allow for ./smtpf -quit by an admin. user. The current alternative is
 * to use the Windows service console or "net start smtp" and "net stop smtpf".
 */
static int
createMyDACL(SECURITY_ATTRIBUTES *sa)
{
	TCHAR * szSD =
	TEXT("D:")			/* Discretionary ACL */
	TEXT("(OD;OICI;GA;;;BG)")     	/* Deny access to built-in guests */
	TEXT("(OD;OICI;GA;;;AN)")     	/* Deny access to anonymous logon */
#  ifdef ALLOW_AUTH_USER
	TEXT("(OA;OICI;GRGWGX;;;AU)") 	/* Allow read/write/execute auth. users */
#  endif
	TEXT("(OA;OICI;GA;;;BA)");    	/* Allow full control to administrators. */

	if (sa == NULL)
		return 0;

	return ConvertStringSecurityDescriptorToSecurityDescriptor(
		szSD, SDDL_REVISION_1, &sa->lpSecurityDescriptor, NULL
	);
}
# endif

int
serverSignalsInit(ServerSignals *signals)
{
	int length;
	char event_name[SIGNAL_LENGTH][128];

	length = snprintf(event_name[SIGNAL_QUIT], sizeof (event_name[SIGNAL_QUIT]), "Global\\%ld-QUIT", GetCurrentProcessId());
	if (sizeof (event_name[SIGNAL_QUIT]) <= length)
		return -1;

	length = snprintf(event_name[SIGNAL_TERM], sizeof (event_name[SIGNAL_TERM]), "Global\\%ld-TERM", GetCurrentProcessId());
	if (sizeof (event_name[SIGNAL_TERM]) <= length)
		return -1;

# ifdef ENABLE_SECURITY_DESCRIPTOR
{
	SECURITY_ATTRIBUTES sa;

	sa.bInheritHandle = 0;
	sa.nLength = sizeof (sa);

	if (createMyDACL(&sa)) {
		signals->signal_event[SIGNAL_QUIT] = CreateEvent(&sa, 0, 0, event_name[SIGNAL_QUIT]);
		signals->signal_event[SIGNAL_TERM] = CreateEvent(&sa, 0, 0, event_name[SIGNAL_TERM]);
		LocalFree(sa.lpSecurityDescriptor);
	}
}
# else
	signals->signal_event[SIGNAL_QUIT] = CreateEvent(NULL, 0, 0, event_name[SIGNAL_QUIT]);
	signals->signal_event[SIGNAL_TERM] = CreateEvent(NULL, 0, 0, event_name[SIGNAL_TERM]);
# endif
	if (signals->signal_event[SIGNAL_QUIT] == NULL || signals->signal_event[SIGNAL_TERM] == NULL) {
		return -1;
	}

	return 0;
}

void
serverSignalsFini(ServerSignals *signals)
{
	ServerSignal i;

	for (i = 0; i < SIGNAL_LENGTH; i++) {
		if (signals->signal_event[i] != NULL)
			CloseHandle(signals->signal_event[i]);
	}
}

int
serverSignalsLoop(ServerSignals *signals)
{
	int signal;

	switch (WaitForMultipleObjects(SIGNAL_LENGTH, signals->signal_event, 0, INFINITE)) {
	case WAIT_OBJECT_0 + SIGNAL_QUIT: signal = SIGQUIT; break;
	case WAIT_OBJECT_0 + SIGNAL_TERM: signal = SIGTERM; break;
	default: signal = SIGABRT; break;
	}

	syslog(LOG_INFO, "signal %d received", signal);

	return signal;
}
#endif /* __WIN32__ */

/***********************************************************************
 *** Interfaces
 ***********************************************************************/

static void
interfaceFree(void *_interface)
{
	if (_interface != NULL) {
		socketClose(((ServerInterface *) _interface)->socket);
		free(_interface);
	}
}

static ServerInterface *
interfaceCreate(const char *if_name, unsigned port, int queue_size)
{
	int save_errno;
	SocketAddress *saddr;
	ServerInterface *iface;

#ifdef __unix__
	h_errno = 0;
#endif
	if ((iface = calloc(1, sizeof (*iface))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error0;
	}

	/* Create the server socket. */
	if ((saddr = socketAddressCreate(if_name, port)) == NULL)
		goto error1;

	iface->socket = socketOpen(saddr, 1);
	free(saddr);

	if (iface->socket == NULL)
		goto error1;

	(void) fileSetCloseOnExec(socketGetFd(iface->socket), 1);
	(void) socketSetNonBlocking(iface->socket, 1);
	(void) socketSetLinger(iface->socket, 0);
	(void) socketSetReuse(iface->socket, 1);
	(void) SOCKET_SET_NAGLE(iface->socket, 0);

	if (socketServer(iface->socket, (int) queue_size))
		goto error1;

	if (socketAddressGetName(&iface->socket->address, iface->name, sizeof (iface->name)) == 0)
		goto error1;

	syslog(LOG_INFO, "interface=%s ready", if_name);

	return iface;
error1:
#ifdef __unix__
	if (h_errno != 0)
		syslog(LOG_ERR, "interface=%s error: %s (%d)", if_name, hstrerror(h_errno), h_errno);
	else
#endif
		syslog(LOG_ERR, "interface=%s error: %s (%d)", if_name, strerror(errno), errno);
/*{LOG
An error occurred when @PACKAGE_NAME@ tried to bind to the socket.
The most likely cause of this is that something else is already
bound to the socket, like another MTA.
}*/
	save_errno = errno;
	interfaceFree(iface);
	errno = save_errno;
error0:
	return NULL;
}

/***********************************************************************
 *** ServerSession API
 ***********************************************************************/

static unsigned short session_counter = 0;

int
serverSessionIsTerminated(ServerSession *session)
{
#ifdef __WIN32__
	return WaitForSingleObject(session->kill_event, 0) == WAIT_OBJECT_0;
#else
	return 0;
#endif
}

static int
sessionAccept(ServerSession *session)
{
	if (0 < session->server->debug.level)
		syslog(LOG_DEBUG, "%s server-id=%u session-id=%u accept", session->id_log, session->server->id, session->id);
	VALGRIND_PRINTF("%s server-id=%u session-id=%u accept", session->id_log, session->server->id, session->id);

	assert(session->iface != NULL);
	assert(session->iface->socket != NULL);
	session->client = socketAccept(session->iface->socket);

	if (session->client == NULL) {
		syslog(LOG_ERR, "%s server-id=%u session-id=%u accept no socket", session->id_log, session->server->id, session->id);
		return -1;
	}

	if (session->server->hook.session_accept != NULL
	&& (*session->server->hook.session_accept)(session)) {
		syslog(LOG_ERR, "%s server-id=%u session-id=%u accept hook fail", session->id_log, session->server->id, session->id);
		return -1;
	}

	return 0;
}

static void
sessionStart(ServerSession *session)
{
	socklen_t slen;
	SocketAddress saddr;

	if (session->client == NULL)
		return;

	(void) SOCKET_SET_NAGLE(session->client, 0);
	(void) socketSetKeepAlive(session->client, 1);
	socketSetTimeout(session->client, session->server->option.read_to);

	/* SOCKET_ADDRESS_AS_IPV4 flag: Convert (normalise) IPv4-mapped-IPv6
	 * to IPV4-compatible-IPv6 address. This avoids isses when comparing
	 * binary IP addresses in network order.
	 */
	(void) socketAddressGetIPv6(&session->client->address, SOCKET_ADDRESS_AS_IPV4, session->ipv6);

	/* SOCKET_ADDRESS_AS_IPV4 flag: Convert IPv4-compatible-IPv6
	 * and IPv4-mapped-IPv6 to simple IPv4 dot notation.
	 */
	(void) socketAddressGetString(&session->client->address, SOCKET_ADDRESS_AS_IPV4, session->address, sizeof (session->address));

	/* Get the local address of the connection. This is required
	 * since a dual IPv6/IPv4 network stack will be bound to one
	 * interface, but handle both types of connections. This means
	 * it is impossible to pre-assign this in serverCreate and
	 * interfaceCreate.
	 */
	slen = sizeof (saddr);
	*session->if_addr = '\0';
	(void) getsockname(socketGetFd(session->client), &saddr.sa, &slen);
	(void) socketAddressFormatIp(&saddr.sa, SOCKET_ADDRESS_AS_IPV4, session->if_addr, sizeof (session->if_addr));
}

static void
sessionFinish(ServerSession *session)
{
	if (session->client != NULL) {
		socketClose(session->client);
		session->client = NULL;
	}
	session->iface = NULL;
}

static void
sessionFree(void *_session)
{
	ServerSession *session = _session;

	if (session != NULL) {
		if (session->server->hook.session_free != NULL)
			(void) (*session->server->hook.session_free)(session);
#ifdef __WIN32__
		CloseHandle(session->kill_event);
#endif
		sessionFinish(session);
		free(session);
	}
}

static ServerSession *
sessionCreate(Server *server)
{
	ServerSession *session;
	int length, cleanup = 1;

	if ((session = malloc(sizeof (*session))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error0;
	}

	PTHREAD_FREE_PUSH(session);
	MEMSET(session, 0, sizeof (*session));

	session->data = NULL;
	session->client = NULL;
	session->server = server;
	session->node.data = session;
	session->node.free = sessionFree;
	session->node.prev = session->node.next = NULL;
#ifdef __WIN32__
	session->kill_event = CreateEvent(NULL, 0, 0, NULL);
#endif

	/* Counter ID zero is reserved for server thread identification. */
	if (++session_counter == 0)
		session_counter = 1;

	/* The session-id is a message-id with cc=00, is composed of
	 *
	 *	ymd HMS ppppp sssss cc
	 *
	 * Since the value of sssss can roll over very quuickly on
	 * some systems, incorporating timestamp and process info
	 * in the session-id should facilitate log searches.
	 */
	session->id = session_counter;
	session->start = time(NULL);
	time62Encode(session->start, session->id_log);
	length = snprintf(
		session->id_log+TIME62_BUFFER_SIZE,
		sizeof (session->id_log)-TIME62_BUFFER_SIZE,
		"%05u%05u00", getpid(), session_counter
	);

	if (sizeof (session->id_log)-TIME62_BUFFER_SIZE <= length) {
		syslog(LOG_ERR, log_buffer, SERVER_FILE_LINENO);
		goto error1;
	}

	if (0 < session->server->debug.level)
		syslog(LOG_DEBUG, "%s server-id=%u session-id=%u create", session->id_log, session->server->id, session->id);
	VALGRIND_PRINTF("%s server-id=%u session-id=%u create", session->id_log, session->server->id, session->id);

	if (server->hook.session_create != NULL
	&& (*server->hook.session_create)(session)) {
		syslog(LOG_ERR, "%s server-id=%u session-id=%u create hook fail", session->id_log, session->server->id, session->id);
		goto error1;
	}

	cleanup = 0;
error1:
	PTHREAD_FREE_POP(cleanup);
error0:
	return session;
}

/***********************************************************************
 *** Server API
 ***********************************************************************/

static void serverWorkerFree(void *_worker);

void
serverFini(Server *server)
{
	if (server != NULL) {
#if defined(HAVE_PTHREAD_ATTR_INIT)
		(void) pthread_attr_destroy(&server->thread_attr);
#endif
		queueRemoveAll(&server->sessions_queued);
		queueFini(&server->sessions_queued);

		queueRemoveAll(&server->workers);
		queueFini(&server->workers);

		free((char *) server->option.interfaces);
		VectorDestroy(server->interfaces);
		free(server->interfaces_ready);
		free(server->interfaces_fd);
		socketFini();
	}
}

int
serverInit(Server *server, const char *interfaces, unsigned default_port)
{
	long i;
	Vector list;
	static unsigned count = 0;

	if (server == NULL || interfaces == NULL)
		goto error0;

	if ((server->option.interfaces = strdup(interfaces)) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error1;
	}

	if (queueInit(&server->workers)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if (queueInit(&server->sessions_queued)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}
#if defined(HAVE_PTHREAD_ATTR_INIT)
	if (pthread_attr_init(&server->thread_attr)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

# if defined(HAVE_PTHREAD_ATTR_SETSCOPE)
	(void) pthread_attr_setscope(&server->thread_attr, PTHREAD_SCOPE_SYSTEM);
# endif
#endif
	server->option.min_threads	= SERVER_MIN_THREADS;
	server->option.max_threads	= SERVER_MAX_THREADS;
	server->option.spare_threads	= SERVER_SPARE_THREADS;
	server->option.queue_size	= SERVER_QUEUE_SIZE;
	server->option.accept_to	= SERVER_ACCEPT_TO;
	server->option.read_to		= SERVER_READ_TO;
	server->option.port		= default_port;

	server->id = ++count;

	if (socketInit()) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if ((list = TextSplit(server->option.interfaces, ";, ", 0)) == NULL) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if ((server->interfaces = VectorCreate(VectorLength(list))) == NULL) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error2;
	}

	VectorSetDestroyEntry(server->interfaces, interfaceFree);

	if ((server->interfaces_fd = calloc(VectorLength(list), sizeof (*server->interfaces_fd))) == NULL) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error2;
	}

	if ((server->interfaces_ready = calloc(VectorLength(list), sizeof (*server->interfaces_fd))) == NULL) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error2;
	}

	for (i = 0; i < VectorLength(list); i++) {
		char *if_name;
		ServerInterface *iface;

		if ((if_name = VectorGet(list, i)) == NULL)
			continue;

		if ((iface = interfaceCreate(if_name, server->option.port, server->option.queue_size)) == NULL) {
			syslog(LOG_WARN, "interface=%s disabled", if_name);
/*{LOG
On some platforms it is possible to bind two separate sockets for
IPv6 ::0 and IPv4 0.0.0.0, both on the same port. On others
platforms binding a single socket to ::0 will also include 0.0.0.0
for the same port and so generate a warning that can be ignored.
Using lsof(1), fstat(1), and/or netsat(1) one should be able to
determine if it is an error due to another process being bound to
the same port and so corrected, or simply to be ignored and the
configuration adjusted to silence the warning in future.
}*/
			continue;
		}

		server->interfaces_fd[VectorLength(server->interfaces)] = socketGetFd(iface->socket);

		if (VectorAdd(server->interfaces, iface)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			goto error2;
		}
	}

	if (VectorLength(server->interfaces) <= 0) {
		syslog(LOG_ERR, "no matching interfaces \"%s\"", server->option.interfaces);
		goto error2;
	}

	VectorDestroy(list);

	return 0;
error2:
	VectorDestroy(list);
error1:
	serverFini(server);
error0:
	return -1;
}

void
serverFree(void *_server)
{
	if (_server != NULL) {
		serverFini(_server);
		free(_server);
	}
}

Server *
serverCreate(const char *interfaces, unsigned default_port)
{
	Server *server;

	if ((server = calloc(1, sizeof (*server))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
	} else if (serverInit(server, interfaces, default_port)) {
		free(server);
		server = NULL;
	}

	return server;
}

int
serverSetStackSize(Server *server, size_t stack_size)
{
#if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	if (server == NULL)
		return errno = EFAULT;
	if (stack_size < PTHREAD_STACK_MIN)
		stack_size = PTHREAD_STACK_MIN;
	return pthread_attr_setstacksize(&server->thread_attr, stack_size);
#else
	return 0;
#endif
}

void
serverAtForkPrepare(Server *server)
{
}

void
serverAtForkParent(Server *server)
{
}

void
serverAtForkChild(Server *server)
{
	queueFini(&server->sessions_queued);
	queueFini(&server->workers);
}

static unsigned worker_counter = 0;

int
serverWorkerIsTerminated(ServerWorker *worker)
{
#ifdef __WIN32__
	return WaitForSingleObject(worker->kill_event, 0) == WAIT_OBJECT_0;
#else
	return !worker->running;
#endif
}

static void
serverWorkerFree(void *_worker)
{
	ServerWorker *worker = (ServerWorker *) _worker;

	if (worker != NULL) {
		PTHREAD_DISABLE_CANCEL();
		if (0 < worker->server->debug.level)
			syslog(LOG_DEBUG, "server-id=%d worker-id=%u stopping... (%lx)", worker->server->id, worker->id, (unsigned long) _worker);

		queueRemove(&worker->server->workers, &worker->node);

		/* Free thread persistent application data. */
		if (worker->server->hook.worker_free != NULL)
			(void) (*worker->server->hook.worker_free)(worker);

#ifdef __WIN32__
		CloseHandle(worker->kill_event);
#endif
		if (0 < worker->server->debug.level) {
			syslog(LOG_DEBUG, "server-id=%d worker-id=%u stopped (%lx)", worker->server->id, worker->id, (unsigned long) _worker);
			assert(0 < worker->id);
		}

		MEMSET(worker, 0, sizeof (worker));
		free(worker);
		PTHREAD_RESTORE_CANCEL();
	}

}

static void *
serverWorker(void *_worker)
{
	unsigned sess_id;
	Server *server;
	ServerSession *session;
	ServerWorker *worker = (ServerWorker *) _worker;
	unsigned threads, active, idle, queued;

	pthread_cleanup_push(serverWorkerFree, worker);

	server = worker->server;

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u worker-id=%u running (%lx)", server->id, worker->id, (unsigned long) worker);

	for (worker->running = 1; server->running && worker->running; ) {
		session = queueDequeue(&server->sessions_queued)->data;

		if (serverWorkerIsTerminated(worker)) {
			sessionFree(session);
			break;
		}

		sess_id = session->id;
		if (0 < server->debug.level)
			syslog(LOG_DEBUG, "%s server-id=%u worker-id=%u session-id=%u dequeued", session->id_log, server->id, worker->id, sess_id);
		VALGRIND_PRINTF("%s server-id=%u worker-id=%u session-id=%u dequeued", session->id_log, server->id, worker->id, sess_id);

		/* Keep track of active worker threads. */
		PTHREAD_MUTEX_LOCK(&server->workers.mutex);
		server->workers_active++;
		PTHREAD_MUTEX_UNLOCK(&server->workers.mutex);

		worker->session = session;
		session->worker = worker;

		if (session != NULL && session->client != NULL) {
			sessionStart(session);
			if (server->hook.session_process != NULL)
				(void) (*server->hook.session_process)(session);
			sessionFinish(session);
		}

		sessionFree(session);
		worker->session = NULL;

		/* Do we have too many threads? */
		active = 0;
		PTHREAD_MUTEX_LOCK(&server->workers.mutex);
		active = --server->workers_active;
		if (active == 0)
			pthread_cond_broadcast(&server->workers.cv_less);
		PTHREAD_MUTEX_UNLOCK(&server->workers.mutex);
		queued = queueLength(&server->sessions_queued);
		threads = queueLength(&server->workers);
		idle = threads - active;

                if (0 < server->debug.level)
                        syslog(LOG_DEBUG, "server-id=%u active=%u idle=%u queued=%u", server->id, active, idle, queued);

		if (server->option.min_threads < threads && queued + server->option.spare_threads < idle) {
			if (0 < server->debug.level)
				syslog(LOG_DEBUG, "server-id=%u worker-id=%u exit", server->id, worker->id);
			break;
		}

		if (0 < server->debug.level)
			syslog(LOG_DEBUG, "server-id=%u worker-id=%u session-id=%u done", server->id, worker->id, sess_id);
		VALGRIND_PRINTF("server-id=%u worker-id=%u session-id=%u done", server->id, worker->id, sess_id);
	}

	pthread_cleanup_pop(1);

	if (0 < server->debug.valgrind)
		VALGRIND_DO_LEAK_CHECK;

	PTHREAD_END(NULL);
}

static int
serverWorkerCreate(Server *server)
{
	int cleanup = -1;
	ServerWorker *worker;

	if ((worker = calloc(1, sizeof (*worker))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error0;
	}

	PTHREAD_FREE_PUSH(worker);

	/* Counter ID zero is reserved for server thread identification. */
	if (++worker_counter == 0)
		worker_counter = 1;

	worker->id = worker_counter;
	worker->server = server;
	worker->node.data = worker;
	worker->node.free = NULL;
#ifdef __WIN32__
	worker->kill_event = CreateEvent(NULL, 0, 0, NULL);
#endif
	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u worker-id=%u start", server->id, worker->id);

	/* Create thread persistent data. */
	if (server->hook.worker_create != NULL
	&& (*server->hook.worker_create)(worker)) {
		syslog(LOG_ERR, "server-id=%u worker-id=%u create hook fail", server->id, worker->id);
		goto error1;
	}

	if (pthread_create(&worker->thread, &server->thread_attr, serverWorker, worker)) {
		syslog(LOG_ERR, "server-id=%u worker-id=%u thread create fail: %s (%d)", server->id, worker->id, strerror(errno), errno);
		goto error1;
	}

	if ((cleanup = pthread_detach(worker->thread)) == 0)
		queueEnqueue(&server->workers, &worker->node);
error1:
	PTHREAD_FREE_POP(cleanup);
error0:
	return cleanup;
}

static void *
serverAccept(void *_server)
{
	int i;
	ServerSession *session;
	Server *server = (Server *) _server;
	unsigned threads, active, idle, queued;

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u running", server->id);

	while (server->running) {
		if (socketTimeouts(server->interfaces_fd, server->interfaces_ready, server->interfaces->_length, server->option.accept_to, 1)) {
			pthread_testcancel();
			for (i = 0; i < server->interfaces->_length; i++) {
				if (0 < server->interfaces_ready[i]) {
					if ((session = sessionCreate(server)) != NULL) {
						session->iface = (ServerInterface *) VectorGet(server->interfaces, i);
						if (sessionAccept(session)) {
							sessionFree(session);
							continue;
						}
						queueEnqueue(&server->sessions_queued, &session->node);

						/* Do we have too few threads? */
						active = 0;
						PTHREAD_MUTEX_LOCK(&server->workers.mutex);
						active = server->workers_active;
						PTHREAD_MUTEX_UNLOCK(&server->workers.mutex);
						queued = queueLength(&server->sessions_queued);
						threads = queueLength(&server->workers);
						idle = threads - active;

						if (0 < server->debug.level)
							syslog(LOG_DEBUG, "%s server-id=%u session-id=%u enqueued; active=%u idle=%u queued=%u", session->id_log, server->id, session->id, active, idle, queued);

						if (idle < queued && threads < server->option.max_threads) {
							pthread_testcancel();
							if (serverWorkerCreate(server)) {
								continue;
							}
						}
					}
				}
			}
		}
	}

	PTHREAD_END(NULL);
}

int
serverStart(Server *server)
{
	if (server == NULL)
		return EFAULT;

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u start", server->id);
	VALGRIND_PRINTF("server-id=%u\n start", server->id);

	if (server->hook.server_start != NULL && (*server->hook.server_start)(server)) {
		syslog(LOG_ERR, "server-id=%u server start hook fail", server->id);
		return -1;
	}

	server->running = 1;

	if (pthread_create(&server->accept_thread, &server->thread_attr, serverAccept, server)) {
		syslog(LOG_ERR, "server-id=%u thread create fail: %s (%d)", server->id, strerror(errno), errno);
		return -1;
	}

	return pthread_detach(server->accept_thread);
}

static int
serverWorkerCancel(List *list, ListItem *node, void *data)
{
	ServerWorker *worker;
	Server *server = data;

	worker = node->data;
	worker->running = 0;

	if (server->hook.worker_cancel != NULL
	&& (*server->hook.worker_cancel)(worker)) {
		syslog(LOG_ERR, "server-id=%u worker-id=%u cancel hook fail", server->id, worker->id);
	}

#ifdef __WIN32__
	SetEvent(worker->kill_event);
#endif
#if defined(__unix__) &&  defined(HAVE_PTHREAD_CANCEL)
	(void) pthread_cancel(worker->thread);
# if defined(__linux__) && defined(HAVE_PTHREAD_KILL)
	/* Attempt to unblock IO, like epoll_wait(), within a
	 * thread that fail to implement cancellation points.
	 */
	(void) pthread_kill(worker->thread, SIGUSR1);

# endif
#endif
	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u worker-id=%u cancel (%lx, %lu)", server->id, worker->id, (unsigned long) worker, (unsigned long) worker->thread);

	return 0;
}

#if defined(__linux__) && defined(HAVE_PTHREAD_KILL)
static void
sig_noop(int signum)
{
	/* Do nothing */
}
#endif

void
serverStop(Server *server, int slow_quit)
{
	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u stopping...", server->id);

	if (server == NULL)
		return;

#if defined(__linux__) && defined(HAVE_PTHREAD_KILL)
	/* Setup no-op signal handler that can be raised in order to
	 * unblock epoll_wait, which is not a pthread cancellation point.
	 */
	signal(SIGUSR1, sig_noop);
#endif

	/* Stop accepting new sessions. */
	server->running = 0;

	/* Stop creating new threads. */
	server->option.min_threads = 0;
	server->option.max_threads = 0;
	server->option.spare_threads = 0;

	/* Stop the accept listener thread. */
#ifndef __WIN32__
	(void) pthread_cancel(server->accept_thread);
#endif
	(void) pthread_join(server->accept_thread, NULL);

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u accept stopped", server->id);

	if (slow_quit) {
		PTHREAD_MUTEX_LOCK(&server->workers.mutex);
		/* Wait for all the active workers to finish. */
		while (0 < server->workers_active) {
			syslog(LOG_INFO, "server-id=%u slow quit active=%u", server->id, server->workers_active);
			if (pthread_cond_wait(&server->workers.cv_less, &server->workers.mutex))
				break;
		}
		PTHREAD_MUTEX_UNLOCK(&server->workers.mutex);
	}

	/* Signal the remaining worker threads to stop. */
	if (0 < server->debug.level)
		syslog(LOG_INFO, "server-id=%u th=%lu cancel", server->id, (unsigned long) queueLength(&server->workers));

	queueWalk(&server->workers, serverWorkerCancel, server);
	queueWaitEmpty(&server->workers);

	if (server->hook.server_stop != NULL)
		(void) (*server->hook.server_stop)(server);

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u all stop", server->id);
}

#ifdef TEST
/***********************************************************************
 *** Mulitple Network Servers
 ***********************************************************************/

#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>

#define _NAME			"server"
#ifdef __WIN32__
# define PID_FILE		"./" _NAME ".pid"
#else
# define PID_FILE		"/var/run/" _NAME ".pid"
#endif
#define ECHO_PORT		7
#define DISCARD_PORT		9
#define DAYTIME_PORT		13
#define CHARGEN_PORT		19
#define SMTP_PORT		25
#define SINK_PORT		27
#define CONNECT_TIMEOUT		10000

int debug;
int server_quit;
int daemon_mode = 1;
char *windows_service;
ServerSignals signals;
char *pid_file = PID_FILE;
int min_threads = SERVER_MIN_THREADS;
int max_threads = SERVER_MAX_THREADS;
int spare_threads = SERVER_SPARE_THREADS;
int sink_service = DISCARD_PORT;
int sink_state = 0;

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

int log_facility = LOG_DAEMON;

static const char usage_msg[] =
"usage: " _NAME " [-dqv][-l facility][-m min][-M max][-P pidfile][-s spare]\n"
"              [-S port,state][-w add|remove]\n"
"\n"
"-d\t\tdisable daemon; run in foreground\n"
"-l facility\tauth, cron, daemon, ftp, lpr, mail, news, uucp, user, \n"
"\t\tlocal0, ... local7; default daemon\n"
"-m min\t\tmin. number of worker threads\n"
"-M max\t\tmax. number of worker threads\n"
"-P pidfile\twere the PID file lives, default " PID_FILE "\n"
"-q\t\t-q slow quit, -qq fast quit, -qqq restart, -qqqq restart-if\n"
"-s spare\tnumber of spare worker threads to maintain\n"
"-S port,state\tsink port behaviour 7=echo, 9=discard (default), 25=smtp;\n"
"\t\tfor port=25: state can be 200, 400 (default), or 500\n"
"-v\t\tverbose debugging\n"
"-w arg\t\tadd or remove Windows service\n"
"\n"
"Server API test tool; enables multiple threaded RFC servers: Echo, Discard,\n"
"Daytime, and Chargen for testing the server API threading model and design.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

static int
name_to_code(struct mapping *map, const char *name)
{
	for ( ; map->name != NULL; map++) {
		if (TextInsensitiveCompare(name, map->name) == 0)
			return map->code;
	}

	return -1;
}

#if ! defined(__MINGW32__)
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
#endif

int
reportAccept(ServerSession *session)
{
	syslog(LOG_INFO, "%s start interface=[%s] client=[%s]", session->id_log, session->if_addr, session->address);
	return 0;
}

int
reportFinish(ServerSession *session)
{
	syslog(LOG_INFO, "%s end interface=[%s] client=[%s]", session->id_log, session->if_addr, session->address);
	return 0;
}

int
echoProcess(ServerSession *session)
{
	long length;
	char buffer[256];

	reportAccept(session);

	syslog(LOG_INFO, "%s echo", session->id_log);
	while (socketHasInput(session->client, session->server->option.read_to)) {
		if ((length = socketReadLine2(session->client, buffer, sizeof (buffer), 1)) <= 0)
			break;
		if (serverSessionIsTerminated(session))
			break;
		syslog(LOG_INFO, "%s > %ld:%s", session->id_log, length, buffer);
		if (socketWrite(session->client, (unsigned char *) buffer, length) != length)
			break;
		syslog(LOG_INFO, "%s < %ld:%s", session->id_log, length, buffer);
	}

	return reportFinish(session);
}

int
discardProcess(ServerSession *session)
{
	long length;
	char buffer[256];

	reportAccept(session);

	syslog(LOG_INFO, "%s discard", session->id_log);
	while (socketHasInput(session->client, session->server->option.read_to)) {
		if ((length = socketReadLine2(session->client, buffer, sizeof (buffer), 1)) <= 0)
			break;
		if (serverSessionIsTerminated(session))
			break;
		syslog(LOG_INFO, "%s > %ld:%s", session->id_log, length, buffer);
	}

	return reportFinish(session);
}

int
daytimeProcess(ServerSession *session)
{
	int length;
	time_t now;
	char stamp[40];
	struct tm local;

	reportAccept(session);

	syslog(LOG_INFO, "%s daytime", session->id_log);
	(void) time(&now);
	(void) localtime_r(&now, &local);
	length = getRFC2821DateTime(&local, stamp, sizeof (stamp));
	(void) socketWrite(session->client, (unsigned char *) stamp, length);
	(void) socketWrite(session->client, (unsigned char *) "\r\n", 2);

	syslog(LOG_INFO, "%s < %d:%s", session->id_log, length, stamp);

	return reportFinish(session);
}

int
chargenProcess(ServerSession *session)
{
	unsigned offset;
	unsigned char printable[] =
	"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ "
	"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ ";

	reportAccept(session);

	syslog(LOG_INFO, "%s chargen", session->id_log);
	for (offset = 0; !serverSessionIsTerminated(session); offset = (offset+1) % 95) {
		if (socketWrite(session->client, printable+offset, 72) != 72)
			break;
		if (socketWrite(session->client, (unsigned char *) "\r\n", 2) != 2)
			break;
	}

	return reportFinish(session);
}

int
smtpProcess(ServerSession *session)
{
	long length;
	char buffer[512];

	reportAccept(session);

	syslog(LOG_INFO, "%s smtp", session->id_log);
	socketWrite(session->client, (unsigned char *) "421 server down for maintenance\r\n", sizeof ("421 server down for maintenance\r\n")-1);
	syslog(LOG_INFO, "%s < %lu:%s", session->id_log, (unsigned long) sizeof ("421 server down for maintenance\r\n")-1, "421 server down for maintenance\r\n");

	while (socketHasInput(session->client, session->server->option.read_to)) {
		if ((length = socketReadLine2(session->client, buffer, sizeof (buffer), 1)) <= 0)
			break;
		if (serverSessionIsTerminated(session)) {
			syslog(LOG_INFO, "%s terminated", session->id_log);
			break;
		}

		syslog(LOG_INFO, "%s > %lu:%s", session->id_log, length, buffer);

		if (0 < TextInsensitiveStartsWith(buffer, "QUIT")) {
			socketWrite(session->client, (unsigned char *) "221 closing connection\r\n", sizeof ("221 closing connection\r\n")-1);
			syslog(LOG_INFO, "%s < %lu:%s", session->id_log, (unsigned long) sizeof ("221 closing connection\r\n")-1, "221 closing connection\r\n");
			break;
		}

		socketWrite(session->client, (unsigned char *) "421 server down for maintenance\r\n", sizeof ("421 server down for maintenance\r\n")-1);
		syslog(LOG_INFO, "%s < %lu:%s", session->id_log, (unsigned long) sizeof ("421 server down for maintenance\r\n")-1, "421 server down for maintenance\r\n");
	}

	return reportFinish(session);
}

#ifdef HAVE_SENDMSG
#include <sys/uio.h>

int
sinkStart(Server *server)
{
	Socket2 *sink_socket;
	SocketAddress *sink_address;

	if (server == NULL)
		goto error0;

	server->data = NULL;

	if ((sink_address = socketAddressCreate("/tmp/socketsink", 0)) == NULL) {
		syslog(LOG_ERR, "sink: address error: %s (%d)", strerror(errno), errno);
		goto error0;
	}

	if ((sink_socket = socketOpen(sink_address, 1)) == NULL) {
		syslog(LOG_ERR, "sink: open error: %s (%d)", strerror(errno), errno);
		goto error1;
	}

	if (socketClient(sink_socket, CONNECT_TIMEOUT)) {
		syslog(LOG_ERR, "sink: connect error: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	server->data = sink_socket;
	free(sink_address);

	return 0;
error2:
	socketSetLinger(sink_socket, 0);
	socketClose(sink_socket);
	server->data = NULL;
error1:
	free(sink_address);
error0:
	return 0;
}

int
sinkStop(Server *server)
{
	if (server != NULL) {
		socketSetLinger(server->data, 0);
		socketClose(server->data);
		server->data = NULL;
	}

	return 0;
}

int
send_fd(int unix_stream_socket, int fd, unsigned short port, short state, char token[20])
{
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec service_protocol;
	unsigned char buf[CMSG_SPACE(sizeof (int))], service[2 * sizeof (uint16_t) + 20];

	service_protocol.iov_base = &service;
	service_protocol.iov_len = sizeof (service);
	memset(&msg, 0, sizeof(msg));
	msg.msg_control = buf;
	msg.msg_controllen = CMSG_LEN(sizeof (int));
	msg.msg_iov = &service_protocol;
	msg.msg_iovlen = 1;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof (int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = fd;

	*(uint16_t *) &service[0] = port;
	*(int16_t *) &service[sizeof (uint16_t)] = state;
	(void) TextCopy((char *) &service[2 * sizeof (uint16_t)], 20, token);

	return -(sendmsg(unix_stream_socket, &msg, 0) < 0);
}

int
sinkProcess(ServerSession *session)
{
	Socket2 *sink_socket;

	reportAccept(session);

	sink_socket = session->server->data;
	syslog(LOG_INFO, "%s sink fd=%d", session->id_log, socketGetFd(session->client));

	if (sink_socket == NULL
	|| send_fd(socketGetFd(sink_socket), socketGetFd(session->client), sink_service, sink_state, session->id_log)) {
		/* Assume socketsink daemon restarted, attempt reconnect. */
		socketClose(sink_socket);
		session->server->data = NULL;

		if (sinkStart(session->server)) {
			syslog(LOG_ERR, "sink: reconnect error: %s (%d)", strerror(errno), errno);
			goto error0;
		}

		sink_socket = session->server->data;
		if (sink_socket == NULL
		|| send_fd(socketGetFd(sink_socket), socketGetFd(session->client), sink_service, sink_state, session->id_log)) {
			syslog(LOG_ERR, "sink: send_fd error: %s (%d)", strerror(errno), errno);
			goto error0;
		}
	}

	socketClose(session->client);
	session->client = NULL;
error0:
	return reportFinish(session);
}

#endif /* HAVE_SENDMSG */

void
serverOptions(int argc, char **argv)
{
	int ch;
	char *stop;

	optind = 1;
	while ((ch = getopt(argc, argv, "dl:m:M:P:qs:S:vw:")) != -1) {
		switch (ch) {
		case 'l':
			log_facility = name_to_code(logFacilityMap, optarg);
			break;
		case 'm':
			min_threads = strtol(optarg, NULL, 10);
			break;

		case 'M':
			max_threads = strtol(optarg, NULL, 10);
			break;

		case 's':
			spare_threads = strtol(optarg, NULL, 10);
			break;

		case 'S':
			sink_service = (int) strtol(optarg, &stop, 10);
			if (*stop == ',')
				sink_state = (int) strtol(stop+1, NULL, 10);
			break;

		case 'd':
			daemon_mode = 0;
			break;

		case 'q':
			server_quit++;
			break;

		case 'v':
			debug++;
			break;

		case 'P':
			pid_file = optarg;
			break;

		case 'w':
			if (strcmp(optarg, "add") == 0 || strcmp(optarg, "remove") == 0) {
				windows_service = optarg;
				break;
			}
			/*@fallthrough@*/

		default:
			fprintf(stderr, usage_msg);
			exit(EX_USAGE);
		}
	}

	if (min_threads <= 0)
		min_threads = 1;
	if (max_threads <= 0)
		max_threads = 1;
	if (max_threads < min_threads)
		min_threads = max_threads;
}

typedef struct {
	Server *server;
	ServerHook start;
	ServerHook stop;
	ServerSessionHook process;
	const char *host;
	int port;
} ServerHandler;

ServerHandler services[] = {
#ifdef ECHO_PORT
	{ NULL, NULL, NULL, echoProcess, "[::0]:" QUOTE(ECHO_PORT) "; 0.0.0.0:" QUOTE(ECHO_PORT), ECHO_PORT },
#endif
#ifdef DISCARD_PORT
	{ NULL, NULL, NULL, discardProcess, "[::0]:" QUOTE(DISCARD_PORT) "; 0.0.0.0:" QUOTE(DISCARD_PORT), DISCARD_PORT },
#endif
#ifdef DAYTIME_PORT
	{ NULL, NULL, NULL, daytimeProcess, "[::0]:" QUOTE(DAYTIME_PORT) "; 0.0.0.0:" QUOTE(DAYTIME_PORT), DAYTIME_PORT },
#endif
#ifdef CHARGEN_PORT
	{ NULL, NULL, NULL, chargenProcess, "[::0]:" QUOTE(CHARGEN_PORT) "; 0.0.0.0:" QUOTE(CHARGEN_PORT), CHARGEN_PORT },
#endif
#ifdef SMTP_PORT
	{ NULL, NULL, NULL, smtpProcess, "[::0]:" QUOTE(SMTP_PORT) "; 0.0.0.0:" QUOTE(SMTP_PORT), SMTP_PORT },
#endif
#if defined(SINK_PORT) && defined(HAVE_SENDMSG)
	{ NULL, sinkStart, sinkStop, sinkProcess, "[::0]:" QUOTE(SINK_PORT) "; 0.0.0.0:" QUOTE(SINK_PORT), SINK_PORT },
#endif
	{ NULL, NULL, NULL, 0 }
};

int
serverMain(void)
{
	ServerHandler *service;
	int rc, signal = SIGTERM;

	rc = EXIT_FAILURE;

	if (pthreadInit())
		goto error0;

	if (serverSignalsInit(&signals))
		goto error1;

	for (service = services; service->process != NULL; service++) {
		if ((service->server = serverCreate(service->host, service->port)) == NULL)
			goto error2;

		service->server->option.spare_threads = spare_threads;
		service->server->option.min_threads = min_threads;
		service->server->option.max_threads = max_threads;
		service->server->debug.level = debug;
		service->server->hook.server_start = service->start;
		service->server->hook.server_stop = service->stop;
		service->server->hook.session_process = service->process;
		serverSetStackSize(service->server, SERVER_STACK_SIZE);
	}

	if (processDropPrivilages("nobody", "nobody", "/tmp", 0))
		goto error2;
#if defined(__linux__)
	(void) processDumpCore(1);
#endif

	for (service = services; service->process != NULL; service++) {
		if (serverStart(service->server))
			goto error3;
	}

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);
error3:
	for (service = services; service->process != NULL; service++) {
		serverStop(service->server, signal == SIGQUIT);
	}

	syslog(LOG_INFO, "signal %d, terminating process", signal);
	rc = EXIT_SUCCESS;
error2:
	for (service = services; service->process != NULL; service++) {
		serverFree(service->server);
	}

	serverSignalsFini(&signals);
error1:
	pthreadFini();
error0:
	syslog(LOG_INFO, "signal %d, terminated", signal);

	return rc;
}

# ifdef __unix__
/***********************************************************************
 *** Unix Daemon
 ***********************************************************************/

# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif

#include <com/snert/lib/sys/pid.h>

void
atExitCleanUp(void)
{
	(void) unlink(pid_file);
	closelog();
}

int
main(int argc, char **argv)
{
	LogSetProgramName(_NAME);
	serverOptions(argc, argv);

	switch (server_quit) {
	case 1:
		/* Slow quit	-q */
		exit(pidKill(pid_file, SIGQUIT) != 0);

	case 2:
		/* Quit now	-q -q */
		exit(pidKill(pid_file, SIGTERM) != 0);

	case 3:
	case 4:
		/* Restart	-q -q -q
		 * Restart-If	-q -q -q -q
		 */
		if (pidKill(pid_file, SIGTERM) && 3 < server_quit) {
			fprintf(stderr, "no previous instance running: %s (%d)\n", strerror(errno), errno);
			return EXIT_FAILURE;
		}

		sleep(2);
	}

	if (daemon_mode) {
		int pid_fd;

		if (daemon(1, 1)) {
			fprintf(stderr, "daemon failed\n");
			return EX_SOFTWARE;
		}

		openlog(_NAME, LOG_PID|LOG_NDELAY, log_facility);

		if (atexit(atExitCleanUp)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if (pidSave(pid_file)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if ((pid_fd = pidLock(pid_file)) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}
	} else {
		LogOpen("(standard error)");
	}

	return serverMain();
}
# endif /* __unix__ */

# ifdef __WIN32__

#  include <com/snert/lib/sys/winService.h>

/***********************************************************************
 *** Windows Logging
 ***********************************************************************/

static HANDLE eventLog;

void
ReportInit(void)
{
	eventLog = RegisterEventSource(NULL, _NAME);
}

void
ReportLogV(int type, char *fmt, va_list args)
{
	LPCTSTR strings[1];
	char message[1024];

	strings[0] = message;
	(void) vsnprintf(message, sizeof (message), fmt, args);

	ReportEvent(
		eventLog,	// handle of event source
		type,		// event type
		0,		// event category
		0,		// event ID
		NULL,		// current user's SID
		1,		// strings in lpszStrings
		0,		// no bytes of raw data
		strings,	// array of error strings
		NULL		// no raw data
	);
}

void
ReportLog(int type, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ReportLogV(type, fmt, args);
	va_end(args);
}

static DWORD strerror_tls = TLS_OUT_OF_INDEXES;
static const char unknown_error[] = "(unknown error)";

char *
strerror(int error_code)
{
	char *error_string;

	if (strerror_tls == TLS_OUT_OF_INDEXES) {
		strerror_tls = TlsAlloc();
		if (strerror_tls == TLS_OUT_OF_INDEXES)
			return (char *) unknown_error;
	}

	error_string = (char *) TlsGetValue(strerror_tls);
	LocalFree(error_string);

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &error_string, 0, NULL
	);

	if (!TlsSetValue(strerror_tls, error_string)) {
		LocalFree(error_string);
		return (char *) unknown_error;
	}

	return error_string;
}

void
freeThreadData(void)
{
	if (strerror_tls != TLS_OUT_OF_INDEXES) {
		char *error_string = (char *) TlsGetValue(strerror_tls);
		LocalFree(error_string);
	}
}

/***********************************************************************
 *** Windows Service
 ***********************************************************************/

int
main(int argc, char **argv)
{
	/* Get this now so we can use the event log. */
	ReportInit();

	serverOptions(argc, argv);

	if (0 < server_quit) {
		pid_t pid;
		int length;
		HANDLE signal_quit;
		char event_name[128];

		pid = pidLoad(pid_file);
		length = snprintf(event_name, sizeof (event_name), "Global\\%ld-%s", (long) pid, server_quit == 1 ? "QUIT" : "TERM");
		if (sizeof (event_name) <= length) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s pid file name too long", _NAME);
			return EX_SOFTWARE;
		}

		signal_quit = OpenEvent(EVENT_MODIFY_STATE , 0, event_name);
		if (signal_quit == NULL) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s quit error: %s (%d)", _NAME, strerror(errno), errno);
			return EX_OSERR;
		}

		SetEvent(signal_quit);
		CloseHandle(signal_quit);
		return EXIT_SUCCESS;
	}

	if (windows_service != NULL) {
		if (winServiceInstall(*windows_service == 'a', _NAME, NULL) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s %s error: %s (%d)", _NAME, windows_service, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

	openlog(_NAME, LOG_PID|LOG_NDELAY, log_facility);

	if (daemon_mode) {
		if (pidSave(pid_file)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if (pidLock(pid_file) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		winServiceSetSignals(&signals);

		if (winServiceStart(_NAME, argc, argv) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s start error: %s (%d)", _NAME, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

#ifdef NOT_USED
{
	long length;
	char *cwd, *backslash, *server_root, default_root[256];

	/* Get the absolute path of this executable and set the working
	 * directory to correspond to it so that we can find the options
	 * configuration file along side the executable, when running as
	 * a service. (I hate using the registry.)
	 */
	if ((length = GetModuleFileName(NULL, default_root, sizeof default_root)) == 0 || length == sizeof default_root) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to find default server root");
		return EXIT_FAILURE;
	}

	/* Strip off the executable filename, leaving its parent directory. */
	for (backslash = default_root+length; default_root < backslash && *backslash != '\\'; backslash--)
		;

	server_root = default_root;
	*backslash = '\0';

	/* Remember where we are in case we are running in application mode. */
	cwd = getcwd(NULL, 0);

	/* Change to the executable's directory for default configuration file. */
	if (chdir(server_root)) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to change directory to '%s': %s (%d)\n", server_root, strerror(errno), errno);
		exit(EX_OSERR);
	}

	if (cwd != NULL) {
		(void) chdir(cwd);
		free(cwd);
	}
}
#endif

	return serverMain();
}

# endif /* __WIN32__ */

#endif /* TEST */


