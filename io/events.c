/*
 * events.c
 *
 * IO Event API
 *
 * Copyright 2011 by Anthony Howe. All rights reserved.
 */

#ifndef MAX_MILLI_SECONDS
#define MAX_MILLI_SECONDS		(LONG_MAX / UNIT_MILLI)
#endif

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

/* Only needed for EPOLL/POLL EOF detection. Need to find way to remove
 * the need of socket_peek().
 */
#include <com/snert/lib/io/socket3.h>

int
eventGetEnable(Event *event)
{
	return event->enable;
}

void
eventSetEnable(Event *event, int flag)
{
	event->enable = flag;
}

long
eventGetTimeout(Event *event)
{
	return event->timeout;
}

void
eventSetTimeout(Event *event, long ms)
{
	event->timeout = ms;
}

void
eventResetExpire(Event *event, const time_t *now)
{
	event->expire = *now + (event->timeout < 0 ? MAX_MILLI_SECONDS : event->timeout / UNIT_MILLI);
}

int
eventsWait(Events *loop, long ms)
{
	time_t now;
	Event *event;
	int i, fd_length, fd_active, fd_ready;

#if defined(HAVE_KQUEUE)
{
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
		if (event->enable) {
			EV_SET(
				&loop->set[fd_active], event->fd,
				event->io_type, EV_ADD|EV_ENABLE, 0, 0, event
			);
			fd_active++;
		}
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	if ((fd_ready = kevent(kq, loop->set, fd_active, loop->set, fd_active, to)) == 0 && errno != EINTR)
		errno = ETIMEDOUT;

	(void) time(&now);
	if (SIGSETJMP(loop->on_error, 1) != 0)
		goto next_event;
	for (i = 0; i < fd_ready; i++) {
		event = loop->set[i].udata;
		if (loop->set[i].flags & (EV_EOF|EV_ERROR)) {
			errno = (loop->set[i].flags & EV_EOF) ? EPIPE : EIO;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		} else if (loop->set[i].filter == EVFILT_READ || loop->set[i].filter == EVFILT_WRITE) {
			if (event->on.io != NULL)
				(*event->on.io)(loop, event);
			eventResetExpire(event, &now);
		}
next_event:
		;
	}

	(void) close(kq);
error0:
	;
}
#elif defined(HAVE_EPOLL_CREATE)
{
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
		if (event->enable) {
			loop->set[fd_active].data.ptr = event;
			loop->set[fd_active].events = event->io_type;
			if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, event->fd, &loop->set[fd_active]))
				goto error1;
			fd_active++;
		}
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	if ((fd_ready = epoll_wait(ev_fd, loop->set, fd_active, ms)) == 0 && errno != EINTR)
		errno = ETIMEDOUT;

	(void) time(&now);
	if (SIGSETJMP(loop->on_error, 1) != 0)
		goto next_event;
	for (i = 0; i < fd_ready; i++) {
		event = loop->set[i].data.ptr;
		if (loop->set[i].events & (EPOLLHUP|EPOLLERR)) {
			errno = (loop->set[i].events & EPOLLHUP) ? EPIPE : EIO;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		} else if (loop->set[i].events & (EPOLLIN|EPOLLOUT)) {
			/* On disconnect, Linux returns EPOLLIN and zero
			 * bytes sent instead of a more sensible EPOLLHUP.
			 */
			if (loop->set[i].events & EPOLLIN) {
				unsigned char peek_a_boo;
				long nbytes = socket_peek(event->fd, &peek_a_boo, sizeof (peek_a_boo));
				if (nbytes == 0) {
					errno = EPIPE;
					if (event->on.error != NULL)
						(*event->on.error)(loop, event);
					continue;
				}
			}
			if (event->on.io != NULL)
				(*event->on.io)(loop, event);
			eventResetExpire(event, &now);
		}
next_event:
		;
	}
error1:
	(void) close(ev_fd);
error0:
	;
}
#elif defined(HAVE_POLL)
{
	if (ms < 0)
		ms = INFTIM;

	errno = 0;
	fd_length = VectorLength(loop->events);
	if (fd_length <= 0)
		goto error0;

	fd_active = 0;
	for (i = 0; i < fd_length; i++) {
		event = VectorGet(loop->events, i);
		if (event->enable) {
			loop->set[fd_active].events = event->enable ? event->io_type : 0;
			loop->set[fd_active].fd = event->fd;
			fd_active++;
		}
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	if ((fd_ready = poll(loop->set, fd_active, ms)) == 0 && errno != EINTR)
		errno = ETIMEDOUT;

	(void) time(&now);
	if (SIGSETJMP(loop->on_error, 1) != 0)
		goto next_event;
	for (i = 0; i < fd_active; i++) {
		event = VectorGet(loop->events, i);
		if (loop->set[i].fd == event->fd) {
			if (loop->set[i].revents & (POLLIN|POLLOUT)) {
				if (loop->set[i].revents & POLLIN) {
					unsigned char peek_a_boo;
					long nbytes = socket_peek(event->fd, &peek_a_boo, sizeof (peek_a_boo));
					if (nbytes == 0) {
						errno = EPIPE;
						if (event->on.error != NULL)
							(*event->on.error)(loop, event);
						continue;
					}
				}
				if (event->on.io != NULL)
					(*event->on.io)(loop, event);
				eventResetExpire(event, &now);
			} else if (loop->set[i].revents & (POLLHUP|POLLERR|POLLNVAL)) {
				errno = (loop->set[i].revents & POLLHUP) ? EPIPE : EIO;
				if (event->on.error != NULL)
					(*event->on.error)(loop, event);
			}
		}
next_event:
		;
	}
error0:
	;
}
#else
# error "kqueue, epoll, or poll APIs required."
	errno = EIO;
#endif
	return errno;
}

void
eventClose(Events *loop, Event *event)
{
	if (event->on.close != NULL)
		(*event->on.close)(loop, event);
	eventRemove(loop, event);
}

void
eventInit(Event *event, int fd, int io_type)
{
	if (event != NULL) {
		memset(event, 0, sizeof (*event));
		event->io_type = io_type;
		event->timeout = -1;
		event->enable = 1;
		event->fd = fd;
	}
}

void
eventFree(void *_event)
{
	Event *event = _event;

	if (event != NULL) {
		/*** This is a little odd due to how VectorDestroy works.
		 *** The vector may contain a mix of static and dynamic
		 *** elements.
		 ***/
		if (event->free == eventFree) {
			free(event);
		}
	}
}

Event *
eventAlloc(int fd, int io_type)
{
	Event *event;

	if ((event = malloc(sizeof (*event))) != NULL) {
		eventInit(event, fd, io_type);
		event->free = eventFree;
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
 *	The minimum timeout in milli-seconds or -1 for infinite.
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
	seconds = MAX_MILLI_SECONDS;
	expire = now + seconds;

	for (item = (Event **) VectorBase(loop->events); *item != NULL; item++) {
		if ((*item)->enable && now <= (*item)->expire && (*item)->expire < expire) {
			expire = (*item)->expire;
			seconds = expire - now;
		}
	}

	return seconds * UNIT_MILLI;
}

void
eventsExpire(Events *loop, const time_t *expire)
{
	time_t when;
	Event *event, **item;

	when = *expire;

	for (item = (Event **) VectorBase(loop->events); *item != NULL; item++) {
		event = *item;
		if (event->enable && event->expire <= when) {
			errno = ETIMEDOUT;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		}
	}
}

void
eventsRun(Events *loop)
{
	long ms;
	time_t now, expire;

	if (loop == NULL || loop->events == NULL || VectorLength(loop->events) == 0)
		return;

	for (loop->running = 1; loop->running; ) {
		(void) time(&now);
		ms = eventsTimeout(loop, &now);
		if (eventsWait(loop, ms) == ETIMEDOUT && 0 <= ms) {
			expire = now + ms / UNIT_MILLI;
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

