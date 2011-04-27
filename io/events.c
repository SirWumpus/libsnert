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

#include <com/snert/lib/io/events.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/timer.h>

int
eventGetEnabled(Event *event)
{
	return event->enabled;
}

void
eventSetEnabled(Event *event, int flag)
{
	event->enabled = flag;
}

long
eventGetTimeout(Event *event)
{
	return event->timeout;
}

void
eventSetTimeout(Event *event, long seconds)
{
	time_t now;
	(void) time(&now);
	event->timeout = seconds;
	eventResetExpire(event, &now);
}

void
eventResetExpire(Event *event, const time_t *now)
{
	event->expire = *now + (event->timeout < 0 ? LONG_MAX/UNIT_MILLI : event->timeout);
}

#if defined(HAVE_KQUEUE)
int
events_wait_kqueue(Events *loop, long ms)
{
	time_t now;
	Event *event;
	struct kevent *k_ev;
	int io_want, io_seen;
	int i, fd_length, fd_active, fd_ready, saved_errno;

	int kq;
	struct timespec ts, *to = NULL;

	errno = 0;
	fd_length = VectorLength(loop->events);

	if (fd_length <= 0 || (kq = kqueue()) < 0)
		goto error0;

	to = ms < 0 ? NULL : &ts;
	TIMER_SET_MS(&ts, ms);

	/* Create file descriptors list. */
	fd_active = 0;
	for (i = 0; i < fd_length; i++) {
		event = VectorGet(loop->events, i);
		if (event->enabled) {
			io_want = (event->io_type & EVENT_READ) ? EVFILT_READ : EVFILT_WRITE;
			EV_SET(
				&((struct kevent *)loop->set)[fd_active], event->fd,
				io_want, EV_ADD|EV_ENABLE, 0, 0, event
			);
			fd_active++;
		}
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	switch (fd_ready = kevent(kq, (struct kevent *)loop->set, fd_active, (struct kevent *)loop->set, fd_active, to)) {
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

		if ((event = k_ev->udata) == NULL || !event->enabled)
			continue;

		io_seen = 0;
		io_want = event->io_type;

		if (k_ev->flags & EV_ERROR) {
			errno = k_ev->data;
		} else if(k_ev->filter == EVFILT_READ) {
			if ((k_ev->flags & EV_EOF) && k_ev->data == 0)
				errno = k_ev->flags == 0 ? EPIPE : k_ev->flags;
			else
				errno = 0;
			io_seen = EVENT_READ;
		} else if(k_ev->filter == EVFILT_WRITE) {
			if (k_ev->flags & EV_EOF)
				errno = k_ev->flags == 0 ? EPIPE : k_ev->flags;
			else
				errno = 0;
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
				(*event->on.io)(loop, event);
		}
next_event:
		;
	}
error1:
	(void) close(kq);
error0:
	return errno;
}
#endif
#if defined(HAVE_EPOLL_CREATE)
#include <com/snert/lib/io/socket3.h>

int
events_wait_epoll(Events *loop, long ms)
{
	time_t now;
	int io_want;
	Event *event;
	struct epoll_event *e_ev;
	int i, fd_length, fd_active, fd_ready, saved_errno;

	int ev_fd;

	errno = 0;
	fd_length = VectorLength(loop->events);

	if (fd_length <= 0 || (ev_fd = epoll_create(fd_length)) < 0)
		goto error0;

	if (ms < 0)
		ms = INFTIM;

	fd_active = 0;
	for (i = 0; i < fd_length; i++) {
		event = VectorGet(loop->events, i);
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
			errno = socket_get_error(event->fd);
		else if (e_ev->events & (EPOLLIN|EPOLLOUT))
			errno = 0;
		else
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
				(*event->on.io)(loop, event);
		}
next_event:
		;
	}
error1:
	(void) close(ev_fd);
error0:
	return errno;
}
#endif
#if defined(HAVE_POLL)
int
events_wait_poll(Events *loop, long ms)
{
	time_t now;
	int io_want;
	Event *event;
	struct pollfd *p_ev;
	int i, fd_length, fd_active, fd_ready, saved_errno;

	if (ms < 0)
		ms = INFTIM;

	errno = 0;
	fd_length = VectorLength(loop->events);
	if (fd_length <= 0)
		goto error0;

	fd_active = 0;
	for (i = 0; i < fd_length; i++) {
		event = VectorGet(loop->events, i);
		if (event->enabled) {
			io_want = 0;
			if (event->io_type & EVENT_READ)
				io_want |= POLL_READ;
			if (event->io_type & EVENT_WRITE)
				io_want |= POLL_WRITE;
			p_ev = &((struct pollfd *) loop->set)[fd_active];
			p_ev->events = event->enabled ? io_want : 0;
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
	for (i = 0; i < fd_active; i++) {
		if ((event = VectorGet(loop->events, i)) == NULL || !event->enabled)
			continue;

		p_ev = &((struct pollfd *) loop->set)[i];

		if (p_ev->fd == event->fd) {
			if ((p_ev->events & (POLLHUP|POLLIN)) == POLLHUP)
				errno = EPIPE;
			else if (p_ev->events & POLLERR)
				errno = EIO;
			else if (p_ev->events & (POLLIN|POLLOUT))
				errno = 0;
			else
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
					(*event->on.io)(loop, event);
			}
		}
next_event:
		;
	}
error0:
	return errno;
}
#endif

int (*events_wait_fn)(Events *loop, long ms);

int
eventsWait(Events *loop, long ms)
{
	if (events_wait_fn == NULL) {
		errno = EIO;
		return 0;
	}

	return (*events_wait_fn)(loop, ms);
}

void
eventInit(Event *event, int fd, int io_type)
{
	if (event != NULL) {
		memset(event, 0, sizeof (*event));
		event->io_type = io_type;
		event->timeout = -1;
		event->enabled = 1;
		event->fd = fd;
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
	time_t now;
	long length;

	if (loop == NULL || event == NULL)
		return -1;

	if (VectorAdd(loop->events, event))
		return -1;

	length = VectorLength(loop->events);
	if (loop->set_size < length) {
		free(loop->set);
		if ((loop->set = malloc((length + EVENT_GROWTH) * sizeof (*loop->set))) == NULL)
			return -1;
		loop->set_size = length + EVENT_GROWTH;
	}

	(void) time(&now);
	eventResetExpire(event, &now);

	return 0;
}

void
eventRemove(Events *loop, Event *event)
{
	long i, length;

	if (loop != NULL) {
		length = VectorLength(loop->events);

		for (i = 0; i < length; i++) {
			if (VectorGet(loop->events, i) == event) {
				VectorRemove(loop->events, i--);
				break;
			}
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
long
eventsTimeout(Events *loop, const time_t *start)
{
	long seconds;
	time_t now, expire;
	Event **item;

	if (loop == NULL || start == NULL)
		return -1;

	now = *start;
	seconds = LONG_MAX / UNIT_MILLI - 1;
	expire = now + seconds;

	for (item = (Event **) VectorBase(loop->events); *item != NULL; item++) {
		if ((*item)->enabled && now <= (*item)->expire && (*item)->expire < expire) {
			expire = (*item)->expire;
			seconds = expire - now;
		}
	}

	return seconds;
}

void
eventsExpire(Events *loop, const time_t *expire)
{
	time_t when;
	Event *event;
	long i, length;

	when = *expire;

	length = VectorLength(loop->events);
	for (i = 0; i < length; i++) {
		event = VectorGet(loop->events, i);
		if (event->enabled && event->expire <= when) {
			errno = ETIMEDOUT;
			if (event->on.timeout != NULL)
				(*event->on.timeout)(loop, event);
		}
	}
}

void
eventsRun(Events *loop)
{
	long seconds;
	time_t now, expire;

	if (loop == NULL || loop->events == NULL || VectorLength(loop->events) == 0)
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

int
eventsInit(Events *loop)
{
	memset(loop, 0, sizeof (*loop));

	loop->set_size = EVENT_GROWTH;

	if ((loop->events = VectorCreate(loop->set_size)) == NULL)
		return -1;
	VectorSetDestroyEntry(loop->events, eventFree);

	if ((loop->set = calloc(loop->set_size, sizeof (*loop->set))) == NULL) {
		VectorDestroy(loop->events);
		return -1;
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

	return 0;
}

void
eventsFree(Events *loop)
{
	if (loop != NULL) {
		VectorDestroy(loop->events);
		free(loop->set);
	}
}

