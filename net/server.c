/*
 * server.c
 *
 * Threaded Server API
 *
 * Copyright 2008, 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include <com/snert/lib/net/server.h>
#include <com/snert/lib/util/Text.h>

#ifdef __WIN32__
# include <windows.h>
# include <sddl.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

static const char log_oom[] = "out of memory %s(%d)";
static const char log_init[] = "init error %s(%d): %s (%d)";
static const char log_internal[] = "internal error %s(%d): %s (%d)";

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
 ***
 ***********************************************************************/

#ifdef __unix__
/*
 * Set up an event loop to wait and act on SIGPIPE, SIGHUP, SIGINT,
 * SIGQUIT, and SIGTERM. The main server thread and all other child
 * threads will ignore them. This way we can do more interesting
 * things than are possible in a typical signal handler.
 */
int
serverSignalsInit(ServerSignals *signals, const char *name)
{
#ifdef SIGPIPE
# ifdef HAVE_SIGACTION
{
	struct sigaction signal_ignore;

	signal_ignore.sa_flags = 0;
	signal_ignore.sa_handler = SIG_IGN;
	(void) sigemptyset(&signal_ignore.sa_mask);

	if (sigaction(SIGPIPE, &signal_ignore, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}
}
# else
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}
# endif
#endif
        (void) sigemptyset(&signals->signal_set);
# ifdef SIGHUP
        (void) sigaddset(&signals->signal_set, SIGHUP);
# endif
# ifdef SIGINT
        (void) sigaddset(&signals->signal_set, SIGINT);
# endif
# ifdef SIGQUIT
	(void) sigaddset(&signals->signal_set, SIGQUIT);
# endif
# ifdef SIGTERM
	(void) sigaddset(&signals->signal_set, SIGTERM);
# endif
# ifdef SIGALRM
	(void) sigaddset(&signals->signal_set, SIGALRM);
# endif
# ifdef SIGXCPU
	(void) sigaddset(&signals->signal_set, SIGXCPU);
# endif
# ifdef SIGXFSZ
	(void) sigaddset(&signals->signal_set, SIGXFSZ);
# endif
# ifdef SIGVTALRM
	(void) sigaddset(&signals->signal_set, SIGVTALRM);
# endif
        if (pthread_sigmask(SIG_BLOCK, &signals->signal_set, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}

	return 0;
}

void
serverSignalsFini(ServerSignals *signals)
{
#ifdef SIGPIPE
# ifdef HAVE_SIGACTION
{
	struct sigaction signal_default;

	signal_default.sa_flags = 0;
	signal_default.sa_handler = SIG_DFL;
	(void) sigemptyset(&signal_default.sa_mask);

	(void) sigaction(SIGPIPE, &signal_default, NULL);
}
# else
	(void) signal(SIGPIPE, SIG_DFL);
# endif
#endif
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

		case SIGHUP:
# ifdef SIGALRM
		case SIGALRM:
# endif
# ifdef SIGXCPU
		case SIGXCPU:
# endif
# ifdef SIGXFSZ
		case SIGXFSZ:
# endif
# ifdef SIGVTALRM
		case SIGVTALRM:
# endif
# ifdef SIGUSR1
		case SIGUSR1:
# endif
# ifdef SIGUSR2
		case SIGUSR2:
# endif
			syslog(LOG_INFO, "signal %d ignored", signal);
			break;
		}
	}

	return signal;
}
#endif /* __unix__ */

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef __WIN32__

# ifdef ENABLE_OPTION_QUIT
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
serverSignalsInit(ServerSignals *signals, const char *name)
{
	int length;
	char quit_event_name[128];

	length = snprintf(quit_event_name, sizeof (quit_event_name), "Global\\%s-quit", name);
	if (sizeof (quit_event_name) <= length)
		return -1;

# ifdef ENABLE_OPTION_QUIT
	SECURITY_ATTRIBUTES sa;

	sa.bInheritHandle = 0;
	sa.nLength = sizeof (sa);

	if (createMyDACL(&sa)) {
		signals->signal_thread_event = CreateEvent(&sa, 0, 0, quit_event_name);
		LocalFree(sa.lpSecurityDescriptor);
	}
# else
	signals->signal_thread_event = CreateEvent(NULL, 0, 0, quit_event_name);
# endif
	if (signals->signal_thread_event == NULL) {
		return -1;
	}

	return 0;
}

void
serverSignalsFini(ServerSignals *signals)
{
	CloseHandle(signals->signal_thread_event);
}

int
serverSignalsLoop(ServerSignals *signals)
{
	while (WaitForSingleObject(signals->signal_thread_event, INFINITE) != WAIT_OBJECT_0)
		;

	return SIGTERM;
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

#if defined(__unix__)
	(void) fcntl(socketGetFd(iface->socket), F_SETFD, FD_CLOEXEC);
#endif
	(void) socketSetNonBlocking(iface->socket, 1);
	(void) socketSetLinger(iface->socket, 0);
	(void) socketSetReuse(iface->socket, 1);
#ifdef DISABLE_NAGLE
	(void) socketSetNagle(iface->socket, 0);
#endif

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
 *** Session
 ***********************************************************************/

int
sessionIsTerminated(Session *session)
{
#ifdef __WIN32__
	return WaitForSingleObject(session->kill_event, 0) == WAIT_OBJECT_0;
#else
	return 0;
#endif
}

static void
sessionAccept(Session *session)
{
	socklen_t slen;
	SocketAddress saddr;
	static unsigned short counter = 0;

	/* Counter ID zero is reserved for server thread identification. */
	if (++counter == 0)
		counter = 1;

	/* The session-id is a message-id with cc=00, is composed of
	 *
	 *	ymd HMS ppppp sssss cc
	 *
	 * Since the value of sssss can roll over very quuickly on
	 * some systems, incorporating timestamp and process info
	 * in the session-id should facilitate log searches.
	 */
	session->start = time(NULL);
	time62Encode(session->start, session->id);
	snprintf(
		session->id+TIME62_BUFFER_SIZE,
		sizeof (session->id)-TIME62_BUFFER_SIZE,
		"%05u%05u00", getpid(), counter
	);

	/* We have the session ID now and start logging with it. */
	VALGRIND_PRINTF("session %s\n", session->id);
#ifdef DISABLE_NAGLE
	(void) socketSetNagle(session->client, 0);
#endif
	(void) socketSetLinger(session->client, 0);
#ifdef ENABLE_KEEPALIVE
	(void) socketSetKeepAlive(session->client, 1);
#endif
#if defined(__unix__)
	(void) fcntl(socketGetFd(session->client), F_SETFD, FD_CLOEXEC);
#endif
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

	if (session->server->hook.session_accept != NULL
	&& (*session->server->hook.session_accept)(session)) {
		socketClose(session->client);
		session->client = NULL;
		session->iface = NULL;
	}
}

void
sessionFinish(Session *session)
{
	(void) pthread_mutex_lock(&session->server->connections_mutex);

	session->server->connections--;

	if (session->server->hook.server_disconnect != NULL)
		(void) (*session->server->hook.server_disconnect)(session->server);

	(void) pthread_mutex_unlock(&session->server->connections_mutex);

	socketClose(session->client);
	session->client = NULL;
	session->iface = NULL;

	if (session->server->debug.level) {
		VALGRIND_PRINTF("sessionFinish\n");
		VALGRIND_DO_LEAK_CHECK;
	}
}

Session *
sessionCreate(Server *server)
{
	Session *session;

	if ((session = malloc(sizeof (*session))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error0;
	}

	session->prev = NULL;
	session->next = NULL;
	session->client = NULL;
	session->server = server;
	session->thread = pthread_self();
#ifdef __WIN32__
	session->kill_event = CreateEvent(NULL, 0, 0, NULL);
#endif
	if (pthread_mutex_lock(&server->connections_mutex)) {
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	/* Double link-list of sessions. See cmdClients(). */
	if (server->head != NULL) {
		session->prev = NULL;
		session->next = server->head;
	}
	server->head = session;
	if (session->prev != NULL)
		session->prev->next = session;
	if (session->next != NULL)
		session->next->prev = session;

	(void) pthread_mutex_unlock(&server->connections_mutex);

	if (server->hook.session_create != NULL
	&& (*server->hook.session_create)(session)) {
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	return session;
error1:
	free(session);
error0:
	return NULL;
}

void
sessionFree(void *_session)
{
	Session *session = _session;

	if (session != NULL) {
		(void) pthread_mutex_lock(&session->server->connections_mutex);
		session->server->threads--;

		/* Double link-list of sessions. */
		if (session->prev != NULL)
			session->prev->next = session->next;
		else
			session->server->head = session->next;
		if (session->next != NULL)
			session->next->prev = session->prev;

		(void) pthread_mutex_unlock(&session->server->connections_mutex);

		/* In case we are called because of pthread_cancel(),
		 * be sure to cleanup the current connection too.
		 */
		if (session->client != NULL)
			sessionFinish(session);

		if (session->server->hook.session_free != NULL)
			(void) (*session->server->hook.session_free)(session);

		free(session);

#if defined(HAVE_PTHREAD_COND_INIT)
		(void) pthread_cond_signal(&session->server->slow_quit_cv);
#endif
#ifdef __WIN32__
		CloseHandle(session->kill_event);
#endif
	}
}

/***********************************************************************
 *** Server
 ***********************************************************************/

void
serverFree(void *_server)
{
	Server *server = (Server *) _server;

	if (server != NULL) {
#if defined(HAVE_PTHREAD_COND_INIT)
		(void) pthread_mutex_destroy(&server->slow_quit_mutex);
		(void) pthread_cond_destroy(&server->slow_quit_cv);
#endif
#if defined(HAVE_PTHREAD_ATTR_INIT)
		(void) pthread_attr_destroy(&server->thread_attr);
#endif
		(void) pthread_mutex_destroy(&server->connections_mutex);
		(void) pthread_mutex_destroy(&server->accept_mutex);
		free((char *) server->option.interfaces);
		VectorDestroy(server->interfaces);
		free(server->interfaces_ready);
		free(server->interfaces_fd);
		free(server);
	}
}

Server *
serverCreate(const char *interfaces, unsigned default_port)
{
	long i;
	Vector list;
	Server *server;
	static unsigned count = 0;

	if (interfaces == NULL)
		goto error0;

	if ((server = calloc(1, sizeof (*server))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error0;
	}

	if ((server->option.interfaces = strdup(interfaces)) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error1;
	}

	if (pthread_mutex_init(&server->accept_mutex, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if (pthread_mutex_init(&server->connections_mutex, NULL)) {
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
#if defined(HAVE_PTHREAD_COND_INIT)
	if (pthread_cond_init(&server->slow_quit_cv, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}
	if (pthread_mutex_init(&server->slow_quit_mutex, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}
#endif
	server->option.min_threads = SERVER_MIN_THREADS;
	server->option.max_threads = SERVER_MAX_THREADS;
	server->option.new_threads = SERVER_NEW_THREADS;
	server->option.queue_size  = SERVER_QUEUE_SIZE;
	server->option.accept_to   = SERVER_ACCEPT_TO;
	server->option.read_to     = SERVER_READ_TO;
	server->option.port	   = default_port;

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

	return server;
error2:
	VectorDestroy(list);
error1:
	serverFree(server);
error0:
	return NULL;
}

int
serverSetStackSize(Server *server, size_t stack_size)
{
#if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	if (server == NULL)
		return errno = EFAULT;
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
	pthreadMutexDestroy(&server->accept_mutex);
	pthreadMutexDestroy(&server->connections_mutex);
}

static void *serverChild(void *_server);

static void
serverCheckThreadPool(Server *server)
{
	long i;
	pthread_t thread;

	(void) pthread_mutex_lock(&server->connections_mutex);

	server->connections++;

	if (server->hook.server_connect != NULL)
		(void) (*server->hook.server_connect)(server);

	if (!server->debug.valgrind
	&& server->threads <= server->connections
	&& server->threads < server->option.max_threads) {
		if (0 < server->debug.level)
			syslog(LOG_DEBUG, "server-id=%d creating %u more server threads", server->id, server->option.new_threads);

		for (i = 0; i < server->option.new_threads; i++) {
			if (pthread_create(&thread, &server->thread_attr, serverChild, server)) {
				syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
				break;
			}
			pthread_detach(thread);
			server->threads++;
		}
	}

	(void) pthread_mutex_unlock(&server->connections_mutex);
}

static void *
serverChild(void *_server)
{
	Session *session;
	Server *server = (Server *) _server;

	if ((session = sessionCreate(server)) == NULL)
		return NULL;

#ifdef HAVE_PTHREAD_CLEANUP_PUSH
	pthread_cleanup_push(sessionFree, session);
#endif
	while (server->running) {
		if (pthread_mutex_lock(&server->accept_mutex))
			break;

		/* When the child thread is unblocked from waiting on the
		 * accept_mutex, check that the server is still running.
		 */
		if (!server->running) {
			(void) pthread_mutex_unlock(&server->accept_mutex);
			break;
		}

		/* Wait for new connections. The thread is terminated
		 * if we timeout and have more threads than we need.
		 */
		if (socketTimeouts(server->interfaces_fd, server->interfaces_ready, server->interfaces->_length, server->option.accept_to, 1)) {
			int i;

			for (i = 0; i < server->interfaces->_length; i++) {
				if (server->interfaces_fd[i] == server->interfaces_ready[i]) {
					session->iface = VectorGet(server->interfaces, i);
					session->client = socketAccept(session->iface->socket);
					if (session->client != NULL)
						sessionAccept(session);
					break;
				}
			}
		}

		(void) pthread_mutex_unlock(&server->accept_mutex);

		if (session->client == NULL
		&& errno != ECONNABORTED && server->option.min_threads < server->threads) {
			if (errno != 0 && errno != ETIMEDOUT)
				syslog(LOG_ERR, "socket accept: %s (%d); th=%u cn=%u", strerror(errno), errno, server->threads, server->connections);
/*{LOG
When this error occurs, it is most likely due to two types of errors:
a timeout or the process is out of file discriptors, though other
network related errors may occur.

<p>The former case (ETIMEDOUT) is trival and occurs when there is a
lull in activity, in which case surplus threads are discontinued as
they timeout.
</p>

<p>The latter case (EMFILE) is more serious, in which case the process
is out of file descriptors, so the thread is terminated cleanly to
release resources. If this occurs in multiple threads in the accept
state, then this will terminate any surplus of threads, temporarily
preventing the server from answering more connections. This will allow
the threads with active connections to finish, release resources, and
eventually resume answering again once sufficent resources are available.
</p>
<p>
If the process got an error during socket accept and did not terminate
the thread and eliminate the surplus, it might be possible to get into
a tight busy loop, which contantly tries to accept a connection yet fails
with EMFILE. This would slow down or prevent other threads with active
connections from completing normally and possibly hang the process.
</p>
}*/
			break;
		}

		if (session->client != NULL) {
			serverCheckThreadPool(server);
			if (server->hook.session_process != NULL)
				(void) (*server->hook.session_process)(session);
			sessionFinish(session);
		}

		/* If the number of active connections falls below
		 * half the number of extra threads, then terminate
		 * this thread to free up excess unused resources.
		 */
		if (server->option.min_threads < server->threads
		&& server->connections + server->option.new_threads < server->threads) {
			if (0 < server->debug.level)
				syslog(LOG_DEBUG, "server-id=%d terminating excess thread th=%u cn=%u", server->id, server->threads, server->connections);
			break;
		}

		if (server->debug.valgrind)
			break;
	}

#ifdef HAVE_PTHREAD_CLEANUP_PUSH
	pthread_cleanup_pop(1);
#else
	sessionFree(session);
#endif
	return NULL;
}

/* This is a special test wrapper that is intended to exercise serverChild,
 * sessionCreate, and sessionFree, in particular with valgrind looking for
 * memory leaks.
 */
static void *
serverChildTest(void *_server)
{
	unsigned long count;

	for (count = 1; ((Server *) _server)->running; count++) {
		VALGRIND_PRINTF("serverChild begin %lu\n", count);
		(void) serverChild(_server);
		VALGRIND_PRINTF("serverChild end %lu\n", count);
		VALGRIND_DO_LEAK_CHECK;
	}

	return NULL;
}

int
serverStart(Server *server)
{
	pthread_t thread;

	if (1 < server->debug.valgrind) {
		VALGRIND_PRINTF("serverInit\n");
		VALGRIND_DO_LEAK_CHECK;
	}

	/* Start our first server thread to start handling requests. */
	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%d creating initial server thread", server->id);

	/* First thread successfully created. */
	server->threads = 1;
	server->running = 1;

	if (pthread_create(&thread, &server->thread_attr, server->debug.valgrind ? serverChildTest : serverChild, server)) {
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
		server->threads = 0;
		server->running = 0;
		return -1;
	}

	pthread_detach(thread);

	return 0;
}

#ifdef __unix__
void
serverStop(Server *server, int slow_quit)
{
	Session *session, *next;

	server->running = 0;

#if defined(HAVE_PTHREAD_COND_INIT)
	if (slow_quit && pthread_mutex_lock(&server->slow_quit_mutex) == 0) {
		while (server->head != NULL) {
			syslog(LOG_INFO, "server-id=%u slow quit cn=%u", server->id, server->connections);
			if (pthread_cond_wait(&server->slow_quit_cv, &server->slow_quit_mutex))
				break;
		}
		(void) pthread_mutex_unlock(&server->slow_quit_mutex);
	}
#endif
	if (pthread_mutex_lock(&server->connections_mutex))
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);

	for (session = server->head; session != NULL; session = next) {
		next = session->next;
		pthread_cancel(session->thread);
	}

	if (pthread_mutex_unlock(&server->connections_mutex))
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
}
#endif /* __unix__ */

#ifdef __WIN32__
void
serverStop(Server *server, int slow_quit)
{
	Session *session;

	server->running = 0;

	/*** todo slow_quit ***/

	if (pthread_mutex_lock(&server->connections_mutex))
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO);

	for (session = server->head; session != NULL; session = session->next) {
		/* Originally I had planned to just do an on_error longjmp,
		 * but realised that can only be done within the thread's
		 * context.
		 *
		 * So instead we set a kill_event, which each thread polls
		 * before reading the next client request or command.
		 */
		SetEvent(session->kill_event);
	}

	if (pthread_mutex_unlock(&server->connections_mutex))
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO);
}
#endif /* __WIN32__ */

#ifdef TEST
/***********************************************************************
 *** Mulitple Network Servers
 ***********************************************************************/

#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>

#define _NAME			"server"
#define PID_FILE		"/var/run/" _NAME ".pid"
#define ECHO_PORT		7
#define DAYTIME_PORT		13

int debug;
int server_quit;
int daemon_mode = 1;
char *windows_service;
ServerSignals signals;

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

int
reportAccept(Session *session)
{
	syslog(LOG_INFO, "%s start interface=[%s] client=[%s]", session->id, session->if_addr, session->address);
	return 0;
}

int
reportFinish(Session *session)
{
	syslog(LOG_INFO, "%s end interface=[%s] client=[%s]", session->id, session->if_addr, session->address);
	return 0;
}

int
echoProcess(Session *session)
{
	long length;
	unsigned char buffer[256];

	while (0 < (length = socketReadLine2(session->client, buffer, sizeof (buffer), 1))) {
		if (sessionIsTerminated(session))
			break;
		syslog(LOG_INFO, "%s > %ld:%s", session->id, length, buffer);
		if (socketWrite(session->client, buffer, length) != length)
			break;
		syslog(LOG_INFO, "%s < %ld:%s", session->id, length, buffer);
	}

	return reportFinish(session);
}

int
daytimeProcess(Session *session)
{
	int length;
	time_t now;
	char stamp[40];
	struct tm local;

	(void) time(&now);
	(void) localtime_r(&now, &local);
	length = getRFC2821DateTime(&local, stamp, sizeof (stamp));

	(void) socketWrite(session->client, stamp, length);
	syslog(LOG_INFO, "%s < %d:%s", session->id, length, stamp);

	return reportFinish(session);
}

void
serverOptions(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "dqvw:")) != -1) {
		switch (ch) {
		case 'd':
			daemon_mode = 0;
			break;

		case 'q':
			server_quit++;
			break;

		case 'v':
			debug++;
			break;

		case 'w':
			if (strcmp(optarg, "add") == 0 || strcmp(optarg, "remove") == 0) {
				windows_service = optarg;
				break;
			}
			/*@fallthrough@*/

		default:
			fprintf(stderr, "usage: " _NAME " [-dqv][-w add|remove]\n");
			exit(EX_USAGE);
		}
	}
}

int
serverMain(void)
{
	int rc, signal;
	Server *echo, *daytime;

	rc = EXIT_FAILURE;

	if ((echo = serverCreate("[::0]:" QUOTE(ECHO_PORT) "; 0.0.0.0:" QUOTE(ECHO_PORT), ECHO_PORT)) == NULL)
		goto error0;

	echo->debug.level = debug;
	echo->hook.session_accept = reportAccept;
	echo->hook.session_process = echoProcess;
	serverSetStackSize(echo, SERVER_STACK_SIZE);

	if ((daytime = serverCreate("[::0]:" QUOTE(DAYTIME_PORT) "; 0.0.0.0:" QUOTE(DAYTIME_PORT), DAYTIME_PORT)) == NULL)
		goto error1;

	daytime->debug.level = debug;
	daytime->hook.session_accept = reportAccept;
	daytime->hook.session_process = daytimeProcess;
	serverSetStackSize(daytime, SERVER_STACK_SIZE);

	if (serverSignalsInit(&signals, _NAME))
		goto error2;

#if defined(__OpenBSD__) || defined(__FreeBSD__)
	(void) processDumpCore(2);
#endif
	if (processDropPrivilages("nobody", "nobody", "/tmp", 0))
		goto error3;
#if defined(__linux__)
	(void) processDumpCore(1);
#endif
	if (serverStart(echo) || serverStart(daytime))
		goto error3;

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);

	syslog(LOG_INFO, "signal %d, stopping sessions, cn=%u", signal, echo->connections + daytime->connections);
	serverStop(daytime, signal == SIGQUIT);
	serverStop(echo, signal == SIGQUIT);
	syslog(LOG_INFO, "signal %d, terminating process", signal);

	rc = EXIT_SUCCESS;
error3:
	serverSignalsFini(&signals);
error2:
	serverFree(daytime);
error1:
	serverFree(echo);
error0:
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
	(void) unlink(PID_FILE);
	closelog();
}

int
main(int argc, char **argv)
{
	serverOptions(argc, argv);
	LogSetProgramName(_NAME);

	switch (server_quit) {
	case 1:
		/* Slow quit	-q */
		exit(pidKill(PID_FILE, SIGQUIT) != 0);

	case 2:
		/* Quit now	-q -q */
		exit(pidKill(PID_FILE, SIGTERM) != 0);

	default:
		/* Restart	-q -q -q
		 * Restart-If	-q -q -q -q
		 */
		if (pidKill(PID_FILE, SIGTERM) && 3 < server_quit) {
			fprintf(stderr, "no previous instance running: %s (%d)\n", strerror(errno), errno);
			return EXIT_FAILURE;
		}

		sleep(2);
	}

	if (daemon_mode) {
		pid_t ppid;
		int pid_fd;

		openlog(_NAME, LOG_PID|LOG_NDELAY, LOG_USER);
		setlogmask(LOG_UPTO(LOG_DEBUG));

		if ((ppid = fork()) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_OSERR;
		}

		if (ppid != 0)
			return EXIT_SUCCESS;

		if (setsid() == -1) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_OSERR;
		}

		if (atexit(atExitCleanUp)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if (pidSave(PID_FILE)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if ((pid_fd = pidLock(PID_FILE)) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}
	} else {
		LogOpen("(standard error)");
		LogSetLevel(LOG_PRI(LOG_DEBUG));
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

#define QUIT_EVENT_NAME		"Global\\" _NAME "-quit"

int
main(int argc, char **argv)
{
	long length;
	char *cwd, *backslash, *server_root, default_root[256];

	/* Get this now so we can use the event log. */
	ReportInit();

	serverOptions(argc, argv);

	if (server_quit) {
		HANDLE signal_quit = OpenEvent(EVENT_MODIFY_STATE , 0, QUIT_EVENT_NAME);
		if (signal_quit == NULL) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s quit error: %s (%d)", _NAME, strerror(errno), errno);
			exit(EX_OSERR);
		}

		SetEvent(signal_quit);
		CloseHandle(signal_quit);
		exit(EXIT_SUCCESS);
	}

	if (windows_service != NULL) {
		if (winServiceInstall(*windows_service == 'a', _NAME, NULL) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s %s error: %s (%d)", _NAME, windows_service, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

	if (daemon_mode) {
		winServiceSetSignals(&signals);
		if (winServiceStart(_NAME, argc, argv) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s start error: %s (%d)", _NAME, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

#ifdef NOT_USED
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
#endif

	return serverMain();
}

# endif /* __WIN32__ */

#endif /* TEST */


