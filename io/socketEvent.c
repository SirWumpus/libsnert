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

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
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
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/timer.h>

#if !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL_CREATE)
static void
socket_reset_set(SOCKET *array, int length, void *_set)
{
#if defined(HAVE_POLL) && ! defined(HAS_BROKEN_POLL)
{
	struct pollfd *set = _set;

	for ( ; 0 < length; length--, set++) {
		set->revents = 0;
	}
}
#endif
}
#endif /* !defined(HAVE_KQUEUE) && !defined(HAVE_EPOLL_CREATE) */

void
socketEventSetExpire(SocketEvent *event, const time_t *now, long ms)
{
	event->expire = *now + (ms < 0 ? MAX_MILLI_SECONDS : ms / UNIT_MILLI);
}

int
socketEventsWait(SocketEvents *loop, long timeout)
{
	int i, fd_length;
	SocketEvent *event;

#if defined(HAVE_KQUEUE)
{
	int kq, n;
	time_t now;
	struct timespec ts, *to = NULL;
	struct kevent *set, pre_assigned_set[PRE_ASSIGNED_SET_SIZE];

	if ((kq = kqueue()) < 0)
		return 0;

	fd_length = VectorLength(loop->events);
	if (fd_length <= PRE_ASSIGNED_SET_SIZE) {
		set = pre_assigned_set;
		MEMSET(pre_assigned_set, 0, sizeof (pre_assigned_set));
	} else if ((set = malloc(sizeof (*set) * fd_length)) == NULL) {
		goto error1;
	}

	to = timeout < 0 ? NULL : &ts;
	TIMER_SET_MS(&ts, timeout);

	/* Add file desriptors to list. */
	for (i = 0; i < fd_length; i++) {
		event = VectorGet(loop->events, i);
		if (event->socket != NULL)
			EV_SET(&set[i], socketGetFd(event->socket), event->type, EV_ADD|EV_ENABLE, 0, 0, event);
	}

	errno = 0;

	/* Wait for some I/O or timeout. */
	if ((n = kevent(kq, set, fd_length, set, fd_length, to)) == 0)
		errno = ETIMEDOUT;

	(void) time(&now);
	for (i = 0; i < n; i++) {
		event = set[i].udata;
		if (set[i].flags & (EV_EOF|EV_ERROR)) {
			errno = (set[i].flags & EV_EOF) ? EPIPE : EIO;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		} else if (set[i].filter == EVFILT_READ || set[i].filter == EVFILT_WRITE) {
			socketEventSetExpire(event, &now, event->socket->readTimeout);
			if (event->on.io != NULL)
				(*event->on.io)(loop, event);
		}
	}

	if (set != pre_assigned_set)
		free(set);
error1:
	(void) close(kq);
}
#elif defined(HAVE_EPOLL_CREATE)
{
	time_t now;
	SOCKET ev_fd;
	struct epoll_event *set, pre_assigned_set[PRE_ASSIGNED_SET_SIZE];

	if ((ev_fd = epoll_create(fd_length)) < 0)
		return 0;

	if (fd_length <= PRE_ASSIGNED_SET_SIZE) {
		set = pre_assigned_set;
		MEMSET(pre_assigned_set, 0, sizeof (pre_assigned_set));
	} else if ((set = malloc(sizeof (*set) * fd_length)) == NULL) {
		goto error1;
	}

	fd_length = VectorLength(loop->events);
	for (i = 0; i < fd_length; i++) {
		set[i].data.ptr = event;
		set[i].events = event->type | EPOLLERR | EPOLLHUP;
		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, socketGetFd(event->socket), &set[i]))
			goto error2;
	}

	if (timeout < 0)
		timeout = INFTIM;

	errno = 0;

	/* Wait for some I/O or timeout. */
	if ((i = epoll_wait(ev_fd, set, fd_length, timeout)) == 0)
		errno = ETIMEDOUT;

	(void) time(&now);
	for (i = 0; i < fd_length; i++) {
		if (set[i].events & (EPOLLHUP|EPOLLERR)) {
			errno = (set[i].events & EPOLLHUP) ? EPIPE : EIO;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		} else if (set[i].events & (EPOLLIN|EPOLLOUT)) {
			socketEventSetExpire(event, &now, event->socket->readTimeout);
			if (event->on.io != NULL)
				(*event->on.io)(loop, event);
		}
	}
error2:
	if (set != pre_assigned_set)
		free(set);
error1:
	(void) close(ev_fd);
}
#elif defined(HAVE_POLL) && ! defined(HAS_BROKEN_POLL)
# error "poll() not supported"

{
	struct pollfd *set, pre_assigned_set[PRE_ASSIGNED_SET_SIZE];

	if (fd_length <= PRE_ASSIGNED_SET_SIZE) {
		set = pre_assigned_set;
		MEMSET(pre_assigned_set, 0, sizeof (pre_assigned_set));
	} else if ((set = malloc(sizeof (*set) * fd_length)) == NULL) {
		return 0;
	}

	is_input = is_input ? POLLIN : POLLOUT;

	for (i = 0; i < fd_length; i++) {
		if (fd_table[i] == INVALID_SOCKET) {
			errno = EINVAL;
			goto error1;
		}

		set[i].fd = fd_table[i];
		set[i].events = is_input;
	}

	if (timeout < 0)
		timeout = INFTIM;

	do {
		errno = 0;
		pthread_testcancel();
		socket_reset_set(fd_table, fd_length, set);

		/* Wait for some I/O or timeout. */
		if (0 < (i = poll(set, fd_length, timeout))) {
			errno = 0;
		} else if (i == 0) {
			errno = ETIMEDOUT;
		} else if (errno == EINTR && timeout != INFTIM) {
			/* Adjust the timeout in the event of I/O interrupt. */
			CLOCK_GET(&now);
			TIMER_DIFF_VAR(mark) = now;
			CLOCK_SUB(&TIMER_DIFF_VAR(mark), &mark);
			timeout -= TIMER_GET_MS(&TIMER_DIFF_VAR(mark));
			mark = now;
			if (timeout <= 0)
				break;
		}
	} while (errno == EINTR);

	for (i = 0; i < fd_length; i++) {
		if (set[i].revents & (POLLIN|POLLOUT)) {
			/* Report which sockets are ready. */
			fd_ready[i] = fd_table[i];
		} else if ((set[i].revents & ~(POLLIN|POLLOUT)) == 0) {
			/* Report which sockets are NOT ready. */
			fd_ready[i] = INVALID_SOCKET;
		} else {
			fd_ready[i] = ERROR_SOCKET;

			if (errno == 0) {
				/* Did something else happen? */
				if (set[i].revents & POLLHUP)
					errno = EPIPE;
				else if (set[i].revents & POLLERR)
					errno = EIO;
				else if (set[i].revents & POLLNVAL)
					errno = EBADF;
			}
		}
	}
error1:
	if (set != pre_assigned_set)
		free(set);
}
#elif defined(HAVE_SELECT)
# error "select() not supported"
#else
	errno = 0;
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
socketEventsExpire(SocketEvents *loop, const time_t *expire)
{
	time_t when;
	SocketEvent *event, **item;

	when = *expire;

	for (item = (SocketEvent **) VectorBase(loop->events); *item != NULL; item++) {
		event = *item;
		if (event->expire <= when /* && 0 <= event->socket->readTimeout */) {
			errno = ETIMEDOUT;
			if (event->on.error != NULL)
				(*event->on.error)(loop, event);
		}
	}
}

void
socketEventInit(SocketEvent *event, Socket2 *socket, int type)
{
	if (event != NULL && socket != NULL) {
		memset(event, 0, sizeof (*event));
		event->socket = socket;
		event->type = type;
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
		if (event->free == socketEventFree) {
			socketEventClose(NULL, event);
			free(event);
		}
	}
}

SocketEvent *
socketEventAlloc(Socket2 *socket, int type)
{
	SocketEvent *event;

	if ((event = malloc(sizeof (*event))) != NULL) {
		socketEventInit(event, socket, type);
		event->free = socketEventFree;
	}

	return event;
}

int
socketEventAdd(SocketEvents *loop, SocketEvent *event)
{
	time_t now;

	if (loop == NULL || event == NULL)
		return -1;

	if (loop->events == NULL) {
		if ((loop->events = VectorCreate(10)) == NULL)
			return -1;
		VectorSetDestroyEntry(loop->events, socketEventFree);
	}

	(void) time(&now);
	socketEventSetExpire(event, &now, event->socket->readTimeout);

	return VectorAdd(loop->events, event);
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
		if (now <= (*item)->expire && (*item)->expire < expire) {
			expire = (*item)->expire;
			seconds = expire - now;
		}
	}

	return seconds * UNIT_MILLI;
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
			if (loop->on.idle != NULL)
				(*loop->on.idle)(loop, NULL);
		}
	}
}


void
socketEventsStop(SocketEvents *loop)
{
	loop->running = 0;
}

void
socketEventsInit(SocketEvents *loop)
{
	memset(loop, 0, sizeof (*loop));
}

void
socketEventsFree(SocketEvents *loop)
{
	if (loop != NULL) {
		VectorDestroy(loop->events);
	}
}

