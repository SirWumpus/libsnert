/*
 * socketTimeoutIO.c
 *
 * Socket Portability API
 *
 * Copyright 2001, 2010 by Anthony Howe. All rights reserved.
 */

#ifndef PRE_ASSIGNED_SET_SIZE
#define PRE_ASSIGNED_SET_SIZE		10
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

#ifdef HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#endif

#ifdef HAVE_SYS_EVENT_H
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>
# ifndef INFTIM
#  define INFTIM	(-1)
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/timer.h>

#if !defined(HAVE_EPOLL_CREATE) && !defined(HAVE_KQUEUE)
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
#elif defined(HAVE_SELECT) && defined(__unix__)
{
	fd_set *set = _set;

	memset(set, 0, howmany(length+1, NFDBITS) * sizeof(fd_mask));

	for ( ; 0 < length; length--, array++) {
		FD_SET(*array, set);
	}
}
#elif defined(HAVE_SELECT) && defined(__WIN32__)
{
	fd_set *set = _set;

	FD_ZERO(set);

	for ( ; 0 < length; length--, array++) {
		FD_SET(*array, set);
	}
}
#endif
}
#endif /* !defined(HAVE_EPOLL_CREATE) && !defined(HAVE_KQUEUE) */

int
socketTimeouts(SOCKET *fd_table, SOCKET *fd_ready, int fd_length, long timeout, int is_input)
{
	int i;
	TIMER_DECLARE(mark);

	TIMER_START(mark);
#if defined(HAVE_KQUEUE)
{
	int kq, n;
	struct timespec ts, *to = NULL;
	struct kevent *ready, *set, pre_assigned_set[PRE_ASSIGNED_SET_SIZE];

	if ((kq = kqueue()) < 0)
		return 0;

	if (fd_length <= PRE_ASSIGNED_SET_SIZE)
		set = pre_assigned_set;
	else if ((set = malloc(2 * sizeof (*set) * fd_length)) == NULL)
		goto error1;

	is_input = is_input ? EVFILT_READ : EVFILT_WRITE;

	to = timeout < 0 ? NULL : &ts;
	TIMER_SET_MS(&ts, timeout);

	/* Add file desriptors to list. */
	for (i = 0; i < fd_length; i++) {
		EV_SET(&set[i], fd_table[i], is_input, EV_ADD|EV_ENABLE, 0, 0, &fd_ready[i]);
		fd_ready[i] = INVALID_SOCKET;
	}

	do {
		errno = 0;

		/* Wait for some I/O or timeout. */
		if (0 < (n = kevent(kq, set, fd_length, set+fd_length, fd_length, to))) {
			errno = 0;
		} else if (n == 0) {
			errno = ETIMEDOUT;
		} else if (errno == EINTR && timeout != INFTIM) {
			/* Adjust the timeout in the event of I/O interrupt. */
			TIMER_DIFF(mark);
			timeout -= TIMER_GET_MS(&TIMER_DIFF_VAR(mark));
			TIMER_SET_MS(&ts, timeout);
			mark = TIMER_DIFF_VAR(mark);
			if (timeout <= 0)
				break;
		}
	} while (errno == EINTR);

	ready = set+fd_length;
	for (i = 0; i < n; i++) {
		if (ready[i].flags & EV_ERROR) {
			*(SOCKET *) ready[i].udata = ERROR_SOCKET;
		} else if (ready[i].filter == EVFILT_READ || ready[i].filter == EVFILT_WRITE) {
			/* Report which sockets are ready. */
			*(SOCKET *) ready[i].udata = ready[i].ident;
		}
	}

	if (set != pre_assigned_set)
		free(set);
error1:
	(void) close(kq);
}
#elif defined(HAVE_EPOLL_CREATE)
{
	SOCKET ev_fd;
	struct epoll_event *set, pre_assigned_set[PRE_ASSIGNED_SET_SIZE];

	if ((ev_fd = epoll_create(fd_length)) < 0)
		return 0;

	if (fd_length <= PRE_ASSIGNED_SET_SIZE)
		set = pre_assigned_set;
	else if ((set = malloc(sizeof (*set) * fd_length)) == NULL)
		goto error1;

	is_input = is_input ? EPOLLIN : EPOLLOUT;

	for (i = 0; i < fd_length; i++) {
		set[i].data.fd = fd_table[i];
		set[i].events = is_input | EPOLLERR | EPOLLHUP;
		if (epoll_ctl(ev_fd, EPOLL_CTL_ADD, fd_table[i], &set[i]))
			goto error2;
	}

	if (timeout < 0)
		timeout = INFTIM;

	do {
		errno = 0;

		/* Wait for some I/O or timeout. */
		if (0 < (i = epoll_wait(ev_fd, set, fd_length, timeout))) {
			errno = 0;
		} else if (i == 0) {
			errno = ETIMEDOUT;
		} else if (errno == EINTR && timeout != INFTIM) {
			/* Adjust the timeout in the event of I/O interrupt. */
			TIMER_DIFF(mark);
			timeout -= TIMER_GET_MS(&TIMER_DIFF_VAR(mark));
			mark = TIMER_DIFF_VAR(mark);
			if (timeout <= 0)
				break;
		}
	} while (errno == EINTR);

	for (i = 0; i < fd_length; i++) {
		if (set[i].events & (EPOLLIN|EPOLLOUT)) {
			/* Report which sockets are ready. */
			fd_ready[i] = fd_table[i];
		} else if ((set[i].events & ~(EPOLLIN|EPOLLOUT)) == 0) {
			/* Report which sockets are NOT ready. */
			fd_ready[i] = INVALID_SOCKET;
		} else {
			fd_ready[i] = ERROR_SOCKET;

			if (errno == 0) {
				/* Did something else happen? */
				if (set[i].events & EPOLLHUP)
					errno = EPIPE;
				else if (set[i].events & EPOLLERR)
					errno = EIO;
			}
		}
	}
error2:
	if (set != pre_assigned_set)
		free(set);
error1:
	(void) close(ev_fd);
}
#elif defined(HAVE_POLL) && ! defined(HAS_BROKEN_POLL)
{
	struct pollfd *set, pre_assigned_set[PRE_ASSIGNED_SET_SIZE];

	if (fd_length <= PRE_ASSIGNED_SET_SIZE)
		set = pre_assigned_set;
	else if ((set = malloc(sizeof (*set) * fd_length)) == NULL)
		return 0;

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
		socket_reset_set(fd_table, fd_length, set);

		/* Wait for some I/O or timeout. */
		if (0 < (i = poll(set, fd_length, timeout))) {
			errno = 0;
		} else if (i == 0) {
			errno = ETIMEDOUT;
		} else if (errno == EINTR && timeout != INFTIM) {
			/* Adjust the timeout in the event of I/O interrupt. */
			TIMER_DIFF(mark);
			timeout -= TIMER_GET_MS(&TIMER_DIFF_VAR(mark));
			mark = TIMER_DIFF_VAR(mark);
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
# ifdef __unix__
/* Handle HUGE file descriptor sets with select(). */
{
	SOCKET max_fd;
	size_t set_size;
	struct timeval tv, *to;
	fd_set *set, *rd, *wr, *err_set;

	max_fd = -1;
	for (i = 0; i < fd_length; i++) {
		if (max_fd < fd_table[i])
			max_fd = fd_table[i];
	}

	set_size = howmany(max_fd+1, NFDBITS) * sizeof(fd_mask);
	if ((set = malloc(set_size * 2)) == NULL)
		return 0;

	err_set = &set[set_size];

	if (is_input) {
		rd = set;
		wr = NULL;
	} else {
		rd = NULL;
		wr = set;
	}

	TIMER_SET_MS(&tv, timeout);
	to = timeout < 0 ? NULL : &tv;

	do {
		errno = 0;
		memset(err_set, 0, set_size);
		socket_reset_set(fd_table, fd_length, set);

		if (select(max_fd + 1, rd, wr, err_set, to) == 0)
			errno = ETIMEDOUT;
		else if (0 < tv.tv_sec && 0 < tv.tv_usec) {
			TIMER_DIFF(mark);
			timeout -= TIMER_GET_MS(&TIMER_DIFF_VAR(mark));
			TIMER_SET_MS(&tv, timeout);
			mark = TIMER_DIFF_VAR(mark);
			if (tv.tv_sec <= 0 && tv.tv_usec <= 0)
				break;
		}
	} while (errno == EINTR);

	for (i = 0; i < fd_length; i++) {
		if (FD_ISSET(fd_table[i], set)) {
			fd_ready[i] = fd_table[i];
		} else if (FD_ISSET(fd_table[i], err_set)) {
			fd_ready[i] = ERROR_SOCKET;
		} else {
			fd_ready[i] = INVALID_SOCKET;
		}
	}

	free(set);
}
# else /* not __unix__ */
{
	SOCKET max_fd;
	struct timeval tv, *to;
	fd_set set, *rd, *wr, err_set;

	max_fd = -1;
	for (i = 0; i < fd_length; i++) {
		if (max_fd < fd_table[i])
			max_fd = fd_table[i];
	}

	if (is_input) {
		rd = &set;
		wr = NULL;
	} else {
		rd = NULL;
		wr = &set;
	}

	TIMER_SET_MS(&tv, timeout);
	to = timeout < 0 ? NULL : &tv;

	do {
		tv = tv;
		errno = 0;
		FD_ZERO(&err_set);
		socket_reset_set(fd_table, fd_length, &set);

		if (select(max_fd + 1, rd, wr, &err_set, to) == 0)
			errno = ETIMEDOUT;
		else if (0 < tv.tv_sec && 0 < tv.tv_usec) {
			TIMER_DIFF(mark);
			timeout -= TIMER_GET_MS(&TIMER_DIFF_VAR(mark));
			TIMER_SET_MS(&tv, timeout);
			mark = TIMER_DIFF_VAR(mark);
			if (tv.tv_sec <= 0 && tv.tv_usec <= 0)
				break;
		}
	} while (errno == EINTR);

	for (i = 0; i < fd_length; i++) {
		if (FD_ISSET(fd_table[i], &set)) {
			fd_ready[i] = fd_table[i];
		} else if (FD_ISSET(fd_table[i], &err_set)) {
			fd_ready[i] = ERROR_SOCKET;
		} else {
			fd_ready[i] = INVALID_SOCKET;
		}
	}
}
# endif
#else
	errno = 0;
#endif
	return errno == 0;
}

int
socketTimeoutIO(SOCKET fd, long timeout, int is_input)
{
	SOCKET array[1];

	array[0] = fd;

	return socketTimeouts(array, array, 1, timeout, is_input);
}
