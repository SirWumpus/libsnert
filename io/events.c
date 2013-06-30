/*
 * events.c
 *
 * IO Event API
 *
 * Copyright 2011 by Anthony Howe. All rights reserved.
 */

#ifndef EVENT_GROWTH
#define EVENT_GROWTH			100
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#include <com/snert/lib/io/events.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/util/Text.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

#if defined(HAVE_KQUEUE)
# define KQUEUE_READ		EVFILT_READ
# define KQUEUE_WRITE		EVFILT_WRITE
#endif
#if defined(HAVE_EPOLL_CREATE)
# define EPOLL_READ		(EPOLLIN | EPOLLHUP | EPOLLERR)
# define EPOLL_WRITE		(EPOLLOUT | EPOLLHUP | EPOLLERR)
#endif
#if defined(HAVE_POLL)
# define POLL_READ		(POLLIN | POLLHUP | POLLERR | POLLNVAL)
# define POLL_WRITE		(POLLOUT | POLLHUP | POLLERR | POLLNVAL)
#endif

/***********************************************************************
 *** Individual Event Functions
 ***********************************************************************/

int
eventGetEnabled(Event *event)
{
	return event->enabled;
}

void
eventSetEnabled(Event *event, int flag)
{
	event->enabled = flag;

#ifdef USE_LIBEV
	if (flag) {
		if (ev_cb(&event->on.io) != NULL)
			ev_io_start(event->loop, &event->on.io);
		if (ev_cb(&event->on.timeout) != NULL)
			ev_timer_start(event->loop, &event->on.timeout);
	} else {
		if (ev_cb(&event->on.timeout) != NULL)
			ev_timer_stop(event->loop, &event->on.timeout);
		if (ev_cb(&event->on.io) != NULL)
			ev_io_stop(event->loop, &event->on.io);
	}
#endif
}

int
eventGetType(Event *event)
{
#ifdef USE_LIBEV
	return event->on.io.events;
#else
	return event->io_type;
#endif
}

void
eventSetType(Event *event, int type)
{
#ifdef USE_LIBEV
	if (ev_cb(&event->on.io) != NULL) {
		ev_io_stop(event->loop, &event->on.io);
		ev_io_set(&event->on.io, event->fd, type);
		ev_io_start(event->loop, &event->on.io);
	}
#else
	event->io_type = type;
#endif
}

long
eventGetTimeout(Event *event)
{
	return event->timeout;
}

#ifndef USE_LIBEV
static void
eventResetExpire(Event *event, const time_t *now)
{
	event->expire = *now + (event->timeout < 0 ? LONG_MAX/UNIT_MILLI : event->timeout);
}
#endif

/**
 * @param event
 *	A pointer to Event structure.
 *
 * @note
 *	The original design of the Snert IO Events API was based on
 *	a paper about implementing Haskell Event IO and how IO typically
 *	always needs a timeout, so the Snert version always assumes that
 *	both an IO event and timeout event for the same file descriptor.
 *	The timeout event would automatically reset if there was an IO
 * 	event.
 *
 *	libev design decouples the two and leaves it upto the application
 *	to reset the timeout if necessary.
 */
void
eventResetTimeout(Event *event)
{
	if (0 < event->timeout) {
#ifdef USE_LIBEV
		if (ev_cb(&event->on.timeout) != NULL) {
			event->on.timeout.repeat = (ev_tstamp) event->timeout;
			ev_timer_again(event->loop, &event->on.timeout);
		}
#else
		time_t now;
		(void) time(&now);
		eventResetExpire(event, &now);
#endif
	}
}

void
eventSetTimeout(Event *event, long seconds)
{
	event->timeout = seconds;
	eventResetTimeout(event);
}

void
eventInit(Event *event, int fd, int io_type)
{
	if (event != NULL) {
		memset(event, 0, sizeof (*event));
		event->timeout = -1;
		event->enabled = 1;
		event->fd = fd;
#ifdef USE_LIBEV
		ev_io_init(&event->on.io, NULL, fd, io_type);
		ev_init(&event->on.timeout, NULL);
#else
		event->node.free = eventFree;
		event->node.data = event;
		event->io_type = io_type;
#endif
	}
}

void
eventFree(void *_event)
{
	Event *event = _event;
	if (event != NULL && event->free != NULL)
		(*event->free)(event);
}

Event *
eventNew(int fd, int io_type)
{
	Event *event;

	if ((event = malloc(sizeof (*event))) != NULL) {
		eventInit(event, fd, io_type);
		event->free = free;
	}

	return event;
}

int
eventAdd(Events *loop, Event *event)
{
	if (loop == NULL || event == NULL)
		return -1;

#ifdef USE_LIBEV
	event->loop = loop;
	eventSetEnabled(event, 1);
#else
{
	long length;

	listInsertAfter(&loop->events, loop->events.tail, &event->node);

	length = loop->events.length;
	if (loop->set_size < length) {
		free(loop->set);
		if ((loop->set = malloc((length + EVENT_GROWTH) * sizeof (*loop->set))) == NULL)
			return -1;
		loop->set_size = length + EVENT_GROWTH;
	}

	eventResetTimeout(event);
}
#endif
	return 0;
}

void
eventRemove(Events *loop, Event *event)
{
	if (loop != NULL) {
#ifdef USE_LIBEV
		/* Note that ev_TYPE_stop() actually removes an event
		 * from the event loop's list, rather than set a flag.
		 */
		eventSetEnabled(event, 0);
#else
		listDelete(&loop->events, &event->node);
#endif
		eventFree(event);
	}
}

void
eventSetCbIo(Event *event, void *_fn)
{
#ifdef USE_LIBEV
	ev_set_cb(&event->on.io, _fn);
	event->on.io.data = event;
#else
	event->on.io = (EventHook) _fn;
#endif
}

void
eventSetCbTimer(Event *event, void *_fn)
{
#ifdef USE_LIBEV
	ev_set_cb(&event->on.timeout, _fn);
	event->on.timeout.data = event;
#else
	event->on.timeout = (EventHook) _fn;
#endif
}

/***********************************************************************
 *** Event Loop Functions
 ***********************************************************************/

#ifdef USE_LIBEV

static unsigned ev_loop_flags;

Events *
eventsNew(void)
{
	return ev_loop_new(ev_loop_flags);
}

void
eventsFree(Events *loop)
{
	ev_loop_destroy(loop);
}

void
eventsRun(Events *loop)
{
	ev_run(loop, 0);
}

void
eventsStop(Events *loop)
{
	ev_break(loop, 0);
}

typedef struct {
	const char *name;
	int value;
} map_integer;

void
eventsWaitFnSet(const char *name)
{
	map_integer *item;
	static map_integer backends[] = {
		{ "kqueue", 	EVBACKEND_KQUEUE },
		{ "epoll", 	EVBACKEND_EPOLL },
		{ "poll", 	EVBACKEND_POLL },
		{ "select", 	EVBACKEND_SELECT },
		{ NULL, 	0 }
	};

	for (item = backends; item->name != NULL; item++) {
		if (TextInsensitiveCompare(name, item->name) == 0) {
			ev_loop_flags |= item->value;
			break;
		}
	}
}

#else /* SNERT_EVENTS */

#if defined(HAVE_KQUEUE)
static int
events_wait_kqueue(Events *loop, long ms)
{
	time_t now;
	Event *event;
	ListItem *node;
	struct timespec ts;
	struct kevent *k_ev;
	int i, fd_length, fd_active, fd_ready;
	int kq, saved_errno, io_want, io_seen;

	fd_length = loop->events.length;
	if (fd_length <= 0)
		return EINVAL;

	if ((kq = kqueue()) < 0)
		return errno;

	/* Create file descriptors list. */
	fd_active = 0;
	for (node = loop->events.head; node != NULL; node = node->next) {
		event = node->data;
		if (event->enabled) {
			io_want = (event->io_type & EVENT_READ) ? EVFILT_READ : EVFILT_WRITE;
			EV_SET(
				&((struct kevent *)loop->set)[fd_active], event->fd,
				io_want, EV_ADD|EV_ENABLE, 0, 0, event
			);
			fd_active++;
		}
	}

	TIMER_SET_MS(&ts, ms);

	errno = 0;

	/* Wait for some I/O or timeout. */
	switch (fd_ready = kevent(kq, (struct kevent *)loop->set, fd_active, (struct kevent *)loop->set, fd_active, ms < 0 ? NULL : &ts)) {
	case 0:
		if (errno != EINTR)
			errno = ETIMEDOUT;
		/*@fallthrough@*/
	case -1:
		goto error1;
	}

	saved_errno = errno;

	(void) time(&now);
	if (SIGSETJMP(loop->on_error, 1) != 0)
		goto next_event;
	for (i = 0; i < fd_ready; i++) {
		k_ev = &((struct kevent *) loop->set)[i];

		if ((event = (Event *)k_ev->udata) == NULL || !event->enabled)
			continue;

		errno = 0;
		io_seen = 0;
		io_want = event->io_type;

		if (k_ev->flags & EV_ERROR) {
			errno = k_ev->data;
		} else if(k_ev->filter == EVFILT_READ) {
			if ((k_ev->flags & EV_EOF) && k_ev->data == 0)
				errno = k_ev->flags == 0 ? EPIPE : k_ev->flags;
			io_seen = EVENT_READ;
		} else if(k_ev->filter == EVFILT_WRITE) {
			if (k_ev->flags & EV_EOF)
				errno = k_ev->flags == 0 ? EPIPE : k_ev->flags;
			io_seen = EVENT_WRITE;
		} else {
			/* errno might be changed by the event hook, so
			 * restore before calling the next event hook.
			 */
			errno = saved_errno;
		}

		if (saved_errno == 0 || event->timeout < 0)
			eventResetExpire(event, &now);

		if (errno != 0 || (io_want & io_seen)) {
			/* NOTE the event might remove itself from the loop
			 * and destroy itself, so we cannot reference it
			 * after the caller the handler.
			 */
			if (event->on.io != NULL)
				(*event->on.io)(loop, event, 0);
		}
next_event:
		;
	}
error1:
	saved_errno = errno;
	(void) close(kq);

	return saved_errno;
}
#endif
#if defined(HAVE_EPOLL_CREATE)
#include <com/snert/lib/io/socket3.h>

static int
events_wait_epoll(Events *loop, long ms)
{
	time_t now;
	int io_want;
	Event *event;
	ListItem *node;
	struct epoll_event *e_ev;
	int i, fd_length, fd_active, fd_ready, saved_errno;

	int ev_fd;

	fd_length = loop->events.length;
	if (fd_length <= 0)
		return EINVAL;

	if ((ev_fd = epoll_create(fd_length)) < 0)
		return errno;

	if (ms < 0)
		ms = INFTIM;

	fd_active = 0;
	for (node = loop->events.head; node != NULL; node = node->next) {
		event = node->data;
		if (event->enabled) {
			io_want = 0;
			if (event->io_type & EVENT_READ)
				io_want |= EPOLL_READ;
			if (event->io_type & EVENT_WRITE)
				io_want |= EPOLL_WRITE;
			e_ev = &((struct epoll_event *) loop->set)[fd_active];
			e_ev->data.ptr = event;
			e_ev->events = io_want;
			if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, event->fd, e_ev))
				goto error1;
			fd_active++;
		}
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	switch (fd_ready = epoll_wait(ev_fd, (struct epoll_event *)loop->set, fd_active, ms)) {
	case 0:
		if (errno != EINTR)
			errno = ETIMEDOUT;
		/*@fallthrough@*/
	case -1:
		goto error1;
	}

	saved_errno = errno;

	(void) time(&now);
	if (SIGSETJMP(loop->on_error, 1) != 0)
		goto next_event;
	for (i = 0; i < fd_ready; i++) {
		e_ev = &((struct epoll_event *) loop->set)[i];

		if ((event = e_ev->data.ptr) == NULL || !event->enabled)
			continue;

		if ((e_ev->events & (EPOLLHUP|EPOLLIN)) == EPOLLHUP)
			errno = EPIPE;
		else if (e_ev->events & EPOLLERR)
			errno = socket3_get_error(event->fd);
		else if (!(e_ev->events & (EPOLLIN|EPOLLOUT)))
			errno = saved_errno;

		io_want = 0;
		if (event->io_type & EVENT_READ)
			io_want |= EPOLL_READ;
		if (event->io_type & EVENT_WRITE)
			io_want |= EPOLL_WRITE;

		if (errno != 0 || (e_ev->events & io_want)) {
			if (saved_errno == 0 || event->timeout < 0)
				eventResetExpire(event, &now);
			if (event->on.io != NULL)
				(*event->on.io)(loop, event, 0);
		}
next_event:
		;
	}
error1:
	saved_errno = errno;
	(void) close(ev_fd);

	return saved_errno;
}
#endif
#if defined(HAVE_POLL)
static int
events_wait_poll(Events *loop, long ms)
{
	time_t now;
	int io_want;
	Event *event;
	ListItem *node;
	struct pollfd *p_ev;
	int fd_length, fd_active, fd_ready, saved_errno;

	if (ms < 0)
		ms = INFTIM;

	errno = 0;
	fd_length = loop->events.length;
	if (fd_length <= 0)
		goto error0;

	fd_active = 0;
	for (node = loop->events.head; node != NULL; node = node->next) {
		event = node->data;
		if (event->enabled) {
			io_want = 0;
			if (event->io_type & EVENT_READ)
				io_want |= POLL_READ;
			if (event->io_type & EVENT_WRITE)
				io_want |= POLL_WRITE;
			p_ev = &((struct pollfd *) loop->set)[fd_active];
			p_ev->events = io_want;
			p_ev->revents = 0;
			p_ev->fd = event->fd;
			fd_active++;
		}
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	switch (fd_ready = poll((struct pollfd *)loop->set, fd_active, ms)) {
	case 0:
		if (errno != EINTR)
			errno = ETIMEDOUT;
		/*@fallthrough@*/
	case -1:
		goto error0;
	}

	saved_errno = errno;

	(void) time(&now);
	if (SIGSETJMP(loop->on_error, 1) != 0)
		goto next_event;

	fd_active = 0;
	for (node = loop->events.head; node != NULL; node = node->next) {
		event = node->data;

		if (!event->enabled)
			continue;

		p_ev = &((struct pollfd *) loop->set)[fd_active++];

		if (p_ev->fd == event->fd) {
			if ((p_ev->revents & (POLLHUP|POLLIN)) == POLLHUP)
				errno = EPIPE;
			else if (p_ev->revents & POLLERR)
				errno = EIO;
			else if (!(p_ev->revents & (POLLIN|POLLOUT)))
				errno = saved_errno;

			io_want = 0;
			if (event->io_type & EVENT_READ)
				io_want |= POLL_READ;
			if (event->io_type & EVENT_WRITE)
				io_want |= POLL_WRITE;

			if (errno != 0 || (p_ev->revents & io_want)) {
				if (saved_errno == 0 || event->timeout < 0)
					eventResetExpire(event, &now);
				if (event->on.io != NULL)
					(*event->on.io)(loop, event, 0);
			}
		}
next_event:
		;
	}
error0:
	return errno;
}
#endif

static int (*events_wait_fn)(Events *loop, long ms);

static int
eventsWait(Events *loop, long ms)
{
	if (events_wait_fn == NULL) {
		errno = EIO;
		return 0;
	}

	return (*events_wait_fn)(loop, ms);
}

typedef struct {
	const char *name;
	int (*wait_fn)(Events *loop, long ms);
} eventsWaitMapping;

static eventsWaitMapping wait_mapping[] = {
#if defined(HAVE_KQUEUE)
	{ "kqueue", events_wait_kqueue },
#endif
#if defined(HAVE_EPOLL_CREATE)
	{ "epoll", events_wait_epoll },
#endif
#if defined(HAVE_POLL)
	{ "poll", events_wait_poll },
#endif
	{ NULL, NULL }
};

void
eventsWaitFnSet(const char *name)
{
	eventsWaitMapping *mapping;

	for (mapping = wait_mapping; mapping->name != NULL; mapping++) {
		if (TextInsensitiveCompare(mapping->name, name) == 0) {
			events_wait_fn = mapping->wait_fn;
			break;
		}
	}
}

/**
 * @param loop
 *	A pointer to a Events loop.
 *
 * @param start
 *	A pointer to a time_t start time.
 *
 * @return
 *	The minimum timeout in seconds or -1 for infinite.
 */
static long
eventsTimeout(Events *loop, const time_t *start)
{
	long seconds;
	Event *event;
	ListItem *node;
	time_t now, expire;

	if (loop == NULL || start == NULL)
		return -1;

	now = *start;
	seconds = LONG_MAX / UNIT_MILLI - 1;
	expire = now + seconds;

	for (node = loop->events.head; node != NULL; node = node->next) {
		event = node->data;
		if (event->enabled && now <= event->expire && event->expire < expire) {
			expire = event->expire;
			seconds = expire - now;
		}
	}

	return seconds;
}

static void
eventsExpire(Events *loop, const time_t *expire)
{
	time_t when;
	Event *event;
	ListItem *node, *next;

	when = *expire;

	for (node = loop->events.head; node != NULL; node = next) {
		next = node->next;
		event = node->data;
		if (event->enabled && event->expire <= when) {
			errno = ETIMEDOUT;
			if (event->on.timeout != NULL)
				(*event->on.timeout)(loop, event, 0);
		}
	}
}

Events *
eventsNew(void)
{
	Events *loop;

	if ((loop = calloc(1, sizeof (*loop))) == NULL)
		return NULL;

	loop->set_size = EVENT_GROWTH;
	if ((loop->set = calloc(loop->set_size, sizeof (*loop->set))) == NULL) {
		free(loop);
		return NULL;
	}

	if (events_wait_fn == NULL) {
#if defined(HAVE_KQUEUE)
		events_wait_fn = events_wait_kqueue;
#elif defined(HAVE_EPOLL_CREATE)
		events_wait_fn = events_wait_epoll;
#elif defined(HAVE_POLL)
		events_wait_fn = events_wait_poll;
#endif
	}

	return loop;
}

void
eventsFree(Events *loop)
{
	if (loop != NULL) {
		listFini(&loop->events);
		free(loop->set);
		free(loop);
	}
}

void
eventsRun(Events *loop)
{
	long seconds;
	time_t now, expire;

	if (loop == NULL || loop->events.length == 0)
		return;

	for (loop->running = 1; loop->running; ) {
		(void) time(&now);
		seconds = eventsTimeout(loop, &now);
		if (eventsWait(loop, seconds * UNIT_MILLI) == ETIMEDOUT && 0 <= seconds) {
			expire = now + seconds;
			eventsExpire(loop, &expire);
		}
	}
}

void
eventsStop(Events *loop)
{
	loop->running = 0;
}

#endif /* SNERT_EVENTS */

/***********************************************************************
 *** -end-
 ***********************************************************************/
