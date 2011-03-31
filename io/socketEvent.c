/*
 * socketEvent.c
 *
 * Copyright 2011 by Anthony Howe. All rights reserved.
 */

#ifndef PRE_ASSIGNED_SET_SIZE
#define PRE_ASSIGNED_SET_SIZE		8
#endif

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

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/timer.h>

int
socketEventGetEnable(SocketEvent *event)
{
	return event->enable;
}

void
socketEventSetEnable(SocketEvent *event, int flag)
{
	event->enable = flag;
}

void
socketEventExpire(SocketEvent *event, const time_t *now, long ms)
{
	event->expire = *now + (ms < 0 ? MAX_MILLI_SECONDS : ms / UNIT_MILLI);
}

int
socketEventsWait(SocketEvents *loop, long ms)
{
	time_t now;
	SocketEvent *event;
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
		if (event->enable && event->socket != NULL) {
			EV_SET(
				&loop->set[fd_active], socketGetFd(event->socket),
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
			socketEventExpire(event, &now, socketGetTimeout(event->socket));
			if (event->on.io != NULL)
				(*event->on.io)(loop, event);
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
	SOCKET ev_fd;

	errno = 0;
	fd_length = VectorLength(loop->events);

	if (fd_length <= 0 || (ev_fd = epoll_create(fd_length)) < 0)
		goto error0;

	if (ms < 0)
		ms = INFTIM;

	fd_active = 0;
	for (i = 0; i < fd_length; i++) {
		event = VectorGet(loop->events, i);
		if (event->enable && event->socket != NULL) {
			loop->set[fd_active].data.ptr = event;
			loop->set[fd_active].events = event->io_type;
			if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, socketGetFd(event->socket), &loop->set[fd_active]))
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
				long nbytes = socketPeek(event->socket, &peek_a_boo, sizeof (peek_a_boo));
				if (nbytes == 0) {
					errno = EPIPE;
					if (event->on.error != NULL)
						(*event->on.error)(loop, event);
					continue;
				}
			}
			socketEventExpire(event, &now, socketGetTimeout(event->socket));
			if (event->on.io != NULL)
				(*event->on.io)(loop, event);
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
		if (event->enable && event->socket != NULL) {
			loop->set[fd_active].events = event->enable ? event->io_type : 0;
			loop->set[fd_active].fd = socketGetFd(event->socket);
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
		if (loop->set[i].fd == socketGetFd(event->socket)) {
			if (loop->set[i].revents & (POLLIN|POLLOUT)) {
				if (loop->set[i].revents & POLLIN) {
					unsigned char peek_a_boo;
					long nbytes = socketPeek(event->socket, &peek_a_boo, sizeof (peek_a_boo));
					if (nbytes == 0) {
						errno = EPIPE;
						if (event->on.error != NULL)
							(*event->on.error)(loop, event);
						continue;
					}
				}
				socketEventExpire(event, &now, socketGetTimeout(event->socket));
				if (event->on.io != NULL)
					(*event->on.io)(loop, event);
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
socketEventClose(SocketEvents *loop, SocketEvent *event)
{
	if (event->on.close != NULL)
		(*event->on.close)(loop, event);
	socketClose(event->socket);
	event->socket = NULL;
	event->expire = 0;
}

void
socketEventInit(SocketEvent *event, Socket2 *socket, int io_type)
{
	if (event != NULL && socket != NULL) {
		memset(event, 0, sizeof (*event));
		event->socket = socket;
		event->io_type = io_type;
		event->enable = 1;
	}
}

void
socketEventFree(void *_event)
{
	SocketEvent *event = _event;

	if (event != NULL) {
		/*** This is a little odd due to how VectorDestroy works.
		 *** The vector may contain a mix of static and dynamic
		 *** elements.
		 ***/
		socketEventClose(NULL, event);
		if (event->free == socketEventFree) {
			free(event);
		}
	}
}

SocketEvent *
socketEventAlloc(Socket2 *socket, int io_type)
{
	SocketEvent *event;

	if ((event = malloc(sizeof (*event))) != NULL) {
		socketEventInit(event, socket, io_type);
		event->free = socketEventFree;
	}

	return event;
}

int
socketEventAdd(SocketEvents *loop, SocketEvent *event)
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
	socketEventExpire(event, &now, socketGetTimeout(event->socket));

	return 0;
}

void
socketEventRemove(SocketEvents *loop, SocketEvent *event)
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
 *	A pointer to a SocketEvents loop.
 *
 * @param pnow
 *	A pointer to a time_t start time.
 *
 * @return
 *	The minimum timeout in milli-seconds or -1 for infinite.
 */
long
socketEventsTimeout(SocketEvents *loop, const time_t *start)
{
	long seconds;
	time_t now, expire;
	SocketEvent **item;

	if (loop == NULL || start == NULL)
		return -1;

	now = *start;
	seconds = MAX_MILLI_SECONDS;
	expire = now + seconds;

	for (item = (SocketEvent **) VectorBase(loop->events); *item != NULL; item++) {
		if ((*item)->enable && now <= (*item)->expire && (*item)->expire < expire) {
			expire = (*item)->expire;
			seconds = expire - now;
		}
	}

	return seconds * UNIT_MILLI;
}

void
socketEventsExpire(SocketEvents *loop, const time_t *expire)
{
	time_t when;
	SocketEvent *event, **item;

	when = *expire;

	for (item = (SocketEvent **) VectorBase(loop->events); *item != NULL; item++) {
		event = *item;
		if (event->enable && event->expire <= when) {
			errno = ETIMEDOUT;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		}
	}
}

void
socketEventsRun(SocketEvents *loop)
{
	long ms;
	time_t now, expire;

	if (loop == NULL || loop->events == NULL || VectorLength(loop->events) == 0)
		return;

	for (loop->running = 1; loop->running; ) {
		(void) time(&now);
		ms = socketEventsTimeout(loop, &now);
		if (socketEventsWait(loop, ms) == ETIMEDOUT && 0 <= ms) {
			expire = now + ms / UNIT_MILLI;
			socketEventsExpire(loop, &expire);
		}
	}
}


void
socketEventsStop(SocketEvents *loop)
{
	loop->running = 0;
}

int
socketEventsInit(SocketEvents *loop)
{
	memset(loop, 0, sizeof (*loop));

	loop->set_size = EVENT_GROWTH;

	if ((loop->events = VectorCreate(loop->set_size)) == NULL)
		return -1;
	VectorSetDestroyEntry(loop->events, socketEventFree);

	if ((loop->set = calloc(loop->set_size, sizeof (*loop->set))) == NULL) {
		VectorDestroy(loop->events);
		return -1;
	}

	return 0;
}

void
socketEventsFree(SocketEvents *loop)
{
	if (loop != NULL) {
		VectorDestroy(loop->events);
		free(loop->set);
	}
}

