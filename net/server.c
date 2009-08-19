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
#include <com/snert/lib/io/socket2.h>
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

static void
serverListInsertAfter(ServerList *list, ServerListNode *node, ServerListNode *new_node)
{
	if (node != NULL) {
		new_node->prev = node;
		new_node->next = node->next;
		if (node->next == NULL)
			list->tail = new_node;
		else
			node->next->prev = new_node;
		node->next = new_node;
		list->length++;
	} else if (list->tail == NULL) {
		new_node->prev = new_node->next = NULL;
		list->head = list->tail = new_node;
		list->length++;
	} else {
		serverListInsertAfter(list, list->tail, new_node);
	}
}

#ifdef NOT_USED
static void
serverListInsertBefore(ServerList *list, ServerListNode *node, ServerListNode *new_node)
{
	if (node != NULL) {
		new_node->prev = node->prev;
		new_node->next = node;
		if (node->prev == NULL)
			list->head = new_node;
		else
			node->prev->next = new_node;
		node->prev = new_node;
		list->length++;
	} else if (list->head == NULL) {
		new_node->prev = new_node->next = NULL;
		list->head = list->tail = new_node;
		list->length++;
	} else {
		serverListInsertBefore(list, list->head, new_node)
	}

}
#endif

static void *
serverListDelete(ServerList *list, ServerListNode *node)
{
	void *data = NULL;
	ServerListNode *prev, *next;

	if (node != NULL) {
		data = node->data;
		prev = node->prev;
		next = node->next;

		if (prev == NULL)
			list->head = next;
		else
			prev->next = next;

		if (next == NULL)
			list->tail = prev;
		else
			next->prev = prev;

		list->length--;
		free(node);

		if (list->length == 0 && list->hook.list_empty != NULL)
			(*list->hook.list_empty)(list);
	}

	return data;
}

void *
serverListRemove(ServerList *list, ServerListNode *node)
{
	void *data;

	if (!pthread_mutex_lock(&list->mutex)) {
		if ((data = serverListDelete(list, node)) != NULL)
			(void) pthread_cond_signal(&list->cv_less);
		(void) pthread_mutex_unlock(&list->mutex);
	}

	return data;
}

unsigned
serverListLength(ServerList *list)
{
	unsigned length = 0;

	if (!pthread_mutex_lock(&list->mutex)) {
		length = list->length;
		(void) pthread_mutex_unlock(&list->mutex);
	}

	return length;
}

int
serverListIsEmpty(ServerList *list)
{
	return serverListLength(list) == 0;
}

ServerListNode *
serverListEnqueue(ServerList *list, void *data)
{
	ServerListNode *new_node = NULL;

	if (data != NULL && !pthread_mutex_lock(&list->mutex)) {
		if ((new_node = calloc(1, sizeof (*new_node))) != NULL) {
			new_node->data = data;
			serverListInsertAfter(list, list->tail, new_node);
			(void) pthread_cond_signal(&list->cv_more);
		}
		(void) pthread_mutex_unlock(&list->mutex);
	}

	return new_node;
}

void *
serverListDequeue(ServerList *list)
{
	void *data = NULL;

	if (!pthread_mutex_lock(&list->mutex)) {
		/* Wait until the list has an element. */
		while (list->head == NULL) {
			if (pthread_cond_wait(&list->cv_more, &list->mutex))
				break;
		}

		if ((data = serverListDelete(list, list->head)) != NULL)
			(void) pthread_cond_signal(&list->cv_less);
		(void) pthread_mutex_unlock(&list->mutex);
	}

	return data;
}

int
serverListInit(ServerList *list)
{
	memset(list, 0, sizeof (*list));

	if (pthread_cond_init(&list->cv_more, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}

	if (pthread_cond_init(&list->cv_less, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}

	if (pthread_mutex_init(&list->mutex, NULL)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}

	return 0;
}

void
serverListFini(ServerList *list)
{
	ServerListNode *element, *next;

	for (element = list->head; element != NULL; element = next) {
		next = element->next;
		free(element);
	}

	(void) pthreadMutexDestroy(&list->mutex);
	(void) pthread_cond_destroy(&list->cv_less);
	(void) pthread_cond_destroy(&list->cv_more);

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
serverSignalsInit(ServerSignals *signals, const char *name)
{
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
# ifdef SIGPIPE
	(void) sigaddset(&signals->signal_set, SIGPIPE);
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
# ifdef SIGPIPE
		case SIGPIPE:
# endif
			syslog(LOG_INFO, "signal %d ignored", signal);
			break;
		}
	}

	return signal;
}
#endif /* __unix__ */

/***********************************************************************
 *** Windows Signal Handling
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
 *** ServerSession API
 ***********************************************************************/

int
sessionIsTerminated(ServerSession *session)
{
#ifdef __WIN32__
	return WaitForSingleObject(session->kill_event, 0) == WAIT_OBJECT_0;
#else
	pthread_testcancel();
	return 0;
#endif
}

static int
sessionAccept(ServerSession *session)
{
	if (0 < session->server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u session-id=%u accept", session->server->id, session->id);
	VALGRIND_PRINTF("server-id=%u session-id=%u accept", session->server->id, session->id);

	if (session->server->hook.session_accept != NULL
	&& (*session->server->hook.session_accept)(session)) {
		socketClose(session->client);
		session->client = NULL;
		session->iface = NULL;
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
}

static void
sessionFinish(ServerSession *session)
{
	if (session->client != NULL) {
		socketClose(session->client);
		session->client = NULL;
		session->iface = NULL;
	}
}

static ServerSession *
sessionCreate(Server *server)
{
	int length;
	ServerSession *session;
	static unsigned short counter = 0;

	if ((session = malloc(sizeof (*session))) == NULL) {
		syslog(LOG_ERR, log_oom, SERVER_FILE_LINENO);
		goto error0;
	}

#ifndef NDEBUG
	memset(session, 0, sizeof (*session));
#endif
	session->data = NULL;
	session->client = NULL;
	session->server = server;

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
	session->id = counter;
	session->start = time(NULL);
	time62Encode(session->start, session->id_log);
	length = snprintf(
		session->id_log+TIME62_BUFFER_SIZE,
		sizeof (session->id_log)-TIME62_BUFFER_SIZE,
		"%05u%05u00", getpid(), counter
	);

	if (sizeof (session->id_log)-TIME62_BUFFER_SIZE <= length) {
		syslog(LOG_ERR, log_buffer, SERVER_FILE_LINENO);
		goto error1;
	}

	if (0 < session->server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u session-id=%u create", session->server->id, session->id);
	VALGRIND_PRINTF("server-id=%u session-id=%u create", session->server->id, session->id);

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

static void
sessionFree(void *_session)
{
	ServerSession *session = _session;

	if (session != NULL) {
		if (session->server->hook.session_free != NULL)
			(void) (*session->server->hook.session_free)(session);

		free(session);
	}
}

/***********************************************************************
 *** Server API
 ***********************************************************************/

void
serverFini(Server *server)
{
	if (server != NULL) {
#if defined(HAVE_PTHREAD_ATTR_INIT)
		(void) pthread_attr_destroy(&server->thread_attr);
#endif
		serverListFini(&server->sessions_queued);
		serverListFini(&server->workers);

		free((char *) server->option.interfaces);
		VectorDestroy(server->interfaces);
		free(server->interfaces_ready);
		free(server->interfaces_fd);
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

	if (serverListInit(&server->workers)) {
		syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if (serverListInit(&server->sessions_queued)) {
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
	serverListFini(&server->sessions_queued);
	serverListFini(&server->workers);
}

static void
serverWorkerFree(void *_worker)
{
	ServerWorker *worker = (ServerWorker *) _worker;

	if (worker != NULL) {
		/* Free thread persistent application data. */
		if (worker->server->hook.worker_create != NULL)
			(void) (*worker->server->hook.worker_free)(worker);

		_worker = serverListRemove(&worker->server->workers, worker->node);
#ifndef NDEDUG
		if (_worker != NULL) assert(worker == _worker);
#endif
		if (0 < worker->server->debug.level)
			syslog(LOG_DEBUG, "server-id=%d worker-id=%u stop", worker->server->id, worker->id);
#ifdef __WIN32__
		CloseHandle(session->kill_event);
#endif
		free(worker);
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

#ifdef HAVE_PTHREAD_CLEANUP_PUSH
	pthread_cleanup_push(serverWorkerFree, worker);
#endif
	server = worker->server;

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u worker-id=%u running", server->id, worker->id);

	while (server->running) {
		session = serverListDequeue(&server->sessions_queued);

		sess_id = session->id;
		if (0 < server->debug.level)
			syslog(LOG_DEBUG, "server-id=%u worker-id=%u session-id=%u process", server->id, worker->id, sess_id);
		VALGRIND_PRINTF("server-id=%u worker-id=%u session-id=%u process", server->id, worker->id, sess_id);

		/* Keep track of active worker threads. */
		(void) pthread_mutex_lock(&server->workers.mutex);
		server->workers_active++;
		(void) pthread_mutex_unlock(&server->workers.mutex);

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
		(void) pthread_mutex_lock(&server->workers.mutex);
		active = --server->workers_active;
		threads = server->workers.length;
		idle = threads - active;
		(void) pthread_mutex_unlock(&server->workers.mutex);
		queued = serverListLength(&server->sessions_queued);

                if (0 < server->debug.level)
                        syslog(LOG_DEBUG, "server-id=%u active=%u idle=%u queued=%u", server->id, active, idle, queued);

		if (server->option.min_threads < threads && queued + server->option.new_threads < idle) {
			if (0 < server->debug.level)
				syslog(LOG_DEBUG, "server-id=%u worker-id=%u exit; active=%u idle=%u queued=%u", server->id, worker->id, active, idle, queued);
			break;
		}

		if (0 < server->debug.level)
			syslog(LOG_DEBUG, "server-id=%u worker-id=%u session-id=%u done", server->id, worker->id, sess_id);
		VALGRIND_PRINTF("server-id=%u worker-id=%u session-id=%u done", server->id, worker->id, sess_id);
		if (1 < server->debug.valgrind)
			VALGRIND_DO_LEAK_CHECK;
	}
#ifdef HAVE_PTHREAD_CLEANUP_PUSH
	pthread_cleanup_pop(1);
#else
	serverWorkerFree(worker);
#endif
	return NULL;
}

static ServerWorker *
serverWorkerCreate(Server *server)
{
	ServerWorker *worker;
	static unsigned short counter = 0;

	if ((worker = calloc(1, sizeof (*worker))) == NULL)
		goto error0;

	/* Counter ID zero is reserved for server thread identification. */
	if (++counter == 0)
		counter = 1;

	worker->id = counter;
	worker->server = server;
#ifdef __WIN32__
	worker->kill_event = CreateEvent(NULL, 0, 0, NULL);
#endif
	worker->node = serverListEnqueue(&server->workers, worker);

	/* Create thread persistent data. */
	if (server->hook.worker_create != NULL
	&& (*server->hook.worker_create)(worker)) {
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if (pthread_create(&worker->thread, &server->thread_attr, serverWorker, worker)) {
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
		goto error1;
	}

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u worker-id=%u start", server->id, worker->id);

	pthread_detach(worker->thread);

	return worker;
error1:
	serverWorkerFree(worker);
error0:
	return NULL;
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
			for (i = 0; i < server->interfaces->_length; i++) {
				if (server->interfaces_fd[i] == server->interfaces_ready[i]) {
					if ((session = sessionCreate(server)) != NULL) {
						session->iface = (ServerInterface *) VectorGet(server->interfaces, i);
						session->client = socketAccept(session->iface->socket);
						(void) sessionAccept(session);
						(void) serverListEnqueue(&server->sessions_queued, session);

						/* Do we have too few threads? */
						(void) pthread_mutex_lock(&server->workers.mutex);
						threads = server->workers.length;
						active = server->workers_active;
						idle = threads - active;
						(void) pthread_mutex_unlock(&server->workers.mutex);
						queued = serverListLength(&server->sessions_queued);

						if (idle < queued && threads < server->option.max_threads)
							(void) serverWorkerCreate(server);
					}
				}
			}
		}
	}

	return NULL;
}

int
serverStart(Server *server)
{
	if (server == NULL)
		return -1;

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u start", server->id);
	VALGRIND_PRINTF("server-id=%u\n start", server->id);

	server->running = 1;

	if (pthread_create(&server->accept_thread, &server->thread_attr, serverAccept, server)) {
		syslog(LOG_ERR, log_internal, SERVER_FILE_LINENO, strerror(errno), errno);
		return -1;
	}

	pthread_detach(server->accept_thread);

	return 0;
}

void
serverStop(Server *server, int slow_quit)
{
	ServerListNode *node;

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u stopping...", server->id);

	if (server == NULL)
		return;

	/* Stop accepting new sessions. */
	server->running = 0;

	/* Stop creating new threads. */
	server->option.min_threads = 0;
	server->option.max_threads = 0;
	server->option.new_threads = 0;

	/* Stop the accept listener thread. */
#ifndef __WIN32__
	(void) pthread_cancel(server->accept_thread);
#endif
	(void) pthread_join(server->accept_thread, NULL);

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u accept stopped", server->id);

	if (slow_quit && !pthread_mutex_lock(&server->workers.mutex)) {
		/* Wait for all the active workers to finish. */
		while (0 < server->workers_active) {
			syslog(LOG_INFO, "server-id=%u slow quit active=%u", server->id, server->workers_active);
			if (pthread_cond_wait(&server->workers.cv_less, &server->workers.mutex))
				break;
		}
		(void) pthread_mutex_unlock(&server->workers.mutex);
	}

	/* Stop the remaining worker threads. */
	(void) pthread_mutex_lock(&server->workers.mutex);
	for (node = server->workers.head; node != NULL; node = node->next) {
#ifdef __unix__
		(void) pthread_cancel(((ServerWorker *) node->data)->thread);
#endif
#ifdef __WIN32__
		SetEvent(((ServerWorker *) node->data)->kill_event);
#endif
	}
	(void) pthread_mutex_unlock(&server->workers.mutex);

	if (0 < server->debug.level)
		syslog(LOG_DEBUG, "server-id=%u all stopped", server->id);
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
#define PID_FILE		"/var/run/" _NAME ".pid"
#define ECHO_PORT		7
#define DISCARD_PORT		9
#define DAYTIME_PORT		13
#define CHARGEN_PORT		19
#define SMTP_PORT		25

int debug;
int server_quit;
int daemon_mode = 1;
char *windows_service;
ServerSignals signals;
int min_threads = SERVER_MIN_THREADS;
int max_threads = SERVER_MAX_THREADS;

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

	while (socketHasInput(session->client, session->server->option.read_to)) {
		if ((length = socketReadLine2(session->client, buffer, sizeof (buffer), 1)) <= 0)
			break;
		if (sessionIsTerminated(session))
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

	while (socketHasInput(session->client, session->server->option.read_to)) {
		if ((length = socketReadLine2(session->client, buffer, sizeof (buffer), 1)) <= 0)
			break;
		if (sessionIsTerminated(session))
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

	(void) time(&now);
	(void) localtime_r(&now, &local);
	length = getRFC2821DateTime(&local, stamp, sizeof (stamp));

	(void) socketWrite(session->client, (unsigned char *) stamp, length);
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

	for (offset = 0; !sessionIsTerminated(session); offset = (offset+1) % 95) {
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

	socketWrite(session->client, (unsigned char *) "421 server down for maintenance\r\n", sizeof ("421 server down for maintenance\r\n")-1);
	syslog(LOG_INFO, "%s < %lu:%s", session->id_log, (unsigned long) sizeof ("421 server down for maintenance\r\n")-1, "421 server down for maintenance\r\n");

	while (socketHasInput(session->client, session->server->option.read_to)) {
		if ((length = socketReadLine2(session->client, buffer, sizeof (buffer), 1)) <= 0)
			break;
		if (sessionIsTerminated(session)) {
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

void
serverOptions(int argc, char **argv)
{
	int ch;

	optind = 1;
	while ((ch = getopt(argc, argv, "dm:M:qvw:")) != -1) {
		switch (ch) {
		case 'm':
			min_threads = strtol(optarg, NULL, 10);
			break;

		case 'M':
			max_threads = strtol(optarg, NULL, 10);
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

		case 'w':
			if (strcmp(optarg, "add") == 0 || strcmp(optarg, "remove") == 0) {
				windows_service = optarg;
				break;
			}
			/*@fallthrough@*/

		default:
			fprintf(stderr, "usage: " _NAME " [-dqv][-m min][-M max][-w add|remove]\n");
			exit(EX_USAGE);
		}
	}
}

typedef struct {
	Server *server;
	ServerSessionHook process;
	const char *host;
	int port;
} ServerHandler;

ServerHandler services[] = {
#ifdef ECHO_PORT
	{ NULL, echoProcess, "[::0]:" QUOTE(ECHO_PORT) "; 0.0.0.0:" QUOTE(ECHO_PORT), ECHO_PORT },
#endif
#ifdef DISCARD_PORT
	{ NULL, discardProcess, "[::0]:" QUOTE(DISCARD_PORT) "; 0.0.0.0:" QUOTE(DISCARD_PORT), DISCARD_PORT },
#endif
#ifdef DAYTIME_PORT
	{ NULL, daytimeProcess, "[::0]:" QUOTE(DAYTIME_PORT) "; 0.0.0.0:" QUOTE(DAYTIME_PORT), DAYTIME_PORT },
#endif
#ifdef CHARGEN_PORT
	{ NULL, chargenProcess, "[::0]:" QUOTE(CHARGEN_PORT) "; 0.0.0.0:" QUOTE(CHARGEN_PORT), CHARGEN_PORT },
#endif
#ifdef SMTP_PORT
	{ NULL, smtpProcess, "[::0]:" QUOTE(SMTP_PORT) "; 0.0.0.0:" QUOTE(SMTP_PORT), SMTP_PORT },
#endif
	{ NULL, NULL, NULL, 0 }
};

int
serverMain(void)
{
	int rc, signal;
	ServerHandler *service;

	rc = EXIT_FAILURE;

	for (service = services; service->process != NULL; service++) {
		if ((service->server = serverCreate(service->host, service->port)) == NULL)
			goto error0;

		service->server->option.min_threads = min_threads;
		service->server->option.max_threads = max_threads;
		service->server->debug.level = debug;
		service->server->hook.session_process = service->process;
		serverSetStackSize(service->server, SERVER_STACK_SIZE);
	}

	if (serverSignalsInit(&signals, _NAME))
		goto error1;

#if defined(__OpenBSD__) || defined(__FreeBSD__)
	(void) processDumpCore(2);
#endif
	if (processDropPrivilages("nobody", "nobody", "/tmp", 0))
		goto error2;
#if defined(__linux__)
	(void) processDumpCore(1);
#endif

	for (service = services; service->process != NULL; service++) {
		if (serverStart(service->server))
			goto error2;
	}

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);

	for (service = services; service->process != NULL; service++) {
		serverStop(service->server, signal == SIGQUIT);
	}

	syslog(LOG_INFO, "signal %d, terminating process", signal);
	rc = EXIT_SUCCESS;
error2:
	serverSignalsFini(&signals);
error1:
	for (service = services; service->process != NULL; service++) {
		serverFree(service->server);
	}
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

	case 3:
	case 4:
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


