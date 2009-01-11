/*
 * socketTimeoutIO.c
 *
 * Socket Portability API
 *
 * Copyright 2001, 2008 by Anthony Howe. All rights reserved.
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

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/util/timer.h>

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

int
socketTimeouts(SOCKET *fd_table, SOCKET *fd_ready, int fd_length, long timeout, int is_input)
{
	int i;
#if defined(HAVE_CLOCK_GETTIME)
	struct timespec mark;
	clock_gettime(CLOCK_REALTIME, &mark);
#elif defined(HAVE_GETTIMEOFDAY)
	struct timeval mark;
	gettimeofday(&mark, NULL);
#else
	time_t mark;
	(void) time(&mark);
#endif
#if defined(HAVE_POLL) && ! defined(HAS_BROKEN_POLL)
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
		errno = ETIMEDOUT;
		socket_reset_set(fd_table, fd_length, set);

		/* Wait for some I/O or timeout. */
		if (0 < (i = poll(set, fd_length, timeout))) {
			errno = 0;
		} else if (i == 0) {
			errno = ETIMEDOUT;
		} else if (errno == EINTR && timeout != INFTIM) {
			/* Adjust the timeout in the event of I/O interrupt. */
#if defined(HAVE_CLOCK_GETTIME)
			struct timespec now;

			/* Have we gone back in time, ie. clock adjustment? */
			if (clock_gettime(CLOCK_REALTIME, &now) || now.tv_sec < mark.tv_sec)
				break;
			timeout -= (now.tv_sec - mark.tv_sec) * UNIT_MILLI
				 + (now.tv_nsec - mark.tv_nsec) / UNIT_MICRO;
			mark = now;
#elif defined(HAVE_GETTIMEOFDAY)
			struct timeval now;

			/* Have we gone back in time, ie. clock adjustment? */
			if (gettimeofday(&now, NULL) || now.tv_sec < mark.tv_sec)
				break;
			timeout -= (now.tv_sec - mark.tv_sec) * UNIT_MILLI
				 + (now.tv_usec - mark.tv_usec) / UNIT_MILLI;
			mark = now;
#else
			time_t now;
			if (time(&now) < mark)
				break;
			timeout -= (now - mark) * UNIT_MILLI;
			mark = now;
#endif
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
	fd_set *set, *rd, *wr, *err_set;
	struct timeval tv, tv2, *to;

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

	tv2.tv_sec = timeout / UNIT_MILLI;
	tv2.tv_usec = (timeout % UNIT_MILLI) * UNIT_MILLI;
	to = timeout < 0 ? NULL : &tv;

	do {
		tv = tv2;
		errno = 0;
		memset(err_set, 0, set_size);
		socket_reset_set(fd_table, fd_length, set);

		if (select(max_fd + 1, rd, wr, err_set, to) == 0)
			errno = ETIMEDOUT;
		else if (0 <= timeout) {
#if defined(HAVE_CLOCK_GETTIME)
			struct timespec now;

			/* Have we gone back in time, ie. clock adjustment? */
			if (clock_gettime(CLOCK_REALTIME, &now) || now.tv_sec < mark.tv_sec)
				break;
			if (tv2.tv_nsec < now.tv_nsec - mark.tv_nsec) {
				tv2.tv_usec += UNIT_MICRO;
				tv2.tv_sec--;
			}
			tv2.tv_usec -= now.tv_nsec - mark.tv_nsec;
			tv2.tv_sec -= now.tv_sec - mark.tv_sec;
			mark = now;
#elif defined(HAVE_GETTIMEOFDAY)
			struct timeval now;
			if (gettimeofday(&now, NULL) || now.tv_sec < mark.tv_sec)
				break;
			if (tv2.tv_usec < now.tv_usec - mark.tv_usec) {
				tv2.tv_usec += UNIT_MICRO;
				tv2.tv_sec--;
			}
			tv2.tv_usec -= now.tv_usec - mark.tv_usec;
			tv2.tv_sec -= now.tv_sec - mark.tv_sec;
			mark = now;
#else
			time_t now;
			if (time(&now) < mark)
				break;
			tv2.tv_sec -= now - mark;
			tv2.tv_usec = 0;
			mark = now;
#endif
			if (tv2.tv_sec <= 0 && tv2.tv_usec <= 0)
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
	fd_set set, *rd, *wr, err_set;
	struct timeval tv, tv2, *to;

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

	tv2.tv_sec = timeout / UNIT_MILLI;
	tv2.tv_usec = (timeout % UNIT_MILLI) * UNIT_MILLI;
	to = timeout < 0 ? NULL : &tv;

	do {
		tv = tv2;
		errno = 0;
		FD_ZERO(&err_set);
		socket_reset_set(fd_table, fd_length, &set);

		if (select(max_fd + 1, rd, wr, &err_set, to) == 0)
			errno = ETIMEDOUT;
		else if (0 <= timeout) {
#if defined(HAVE_CLOCK_GETTIME)
			struct timespec now;

			/* Have we gone back in time, ie. clock adjustment? */
			if (clock_gettime(CLOCK_REALTIME, &now) || now.tv_sec < mark.tv_sec)
				break;
			if (tv2.tv_usec < now.tv_nsec - mark.tv_nsec) {
				tv2.tv_usec += UNIT_MICRO;
				tv2.tv_sec--;
			}
			tv2.tv_usec -= now.tv_nsec - mark.tv_nsec;
			tv2.tv_sec -= now.tv_sec - mark.tv_sec;
			mark = now;
#elif defined(HAVE_GETTIMEOFDAY)
			struct timeval now;
			if (gettimeofday(&now, NULL) || now.tv_sec < mark.tv_sec)
				break;
			if (tv2.tv_usec < now.tv_usec - mark.tv_usec) {
				tv2.tv_usec += UNIT_MICRO;
				tv2.tv_sec--;
			}
			tv2.tv_usec -= now.tv_usec - mark.tv_usec;
			tv2.tv_sec -= now.tv_sec - mark.tv_sec;
			mark = now;
#else
			time_t now;
			if (time(&now) < mark)
				break;
			tv2.tv_sec -= now - mark;
			tv2.tv_usec = 0;
			mark = now;
#endif
			if (tv2.tv_sec <= 0 && tv2.tv_usec <= 0)
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
