/*
 * events.h
 *
 * IO Event API
 *
 * Copyright 2011 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_events_h__
#define __com_snert_lib_io_events_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 *** Events (EXPERIMENTAL)
 ***********************************************************************/

# ifndef INFTIM
#  define INFTIM	(-1)
# endif

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>

# define EVENT_READ		EVFILT_READ
# define EVENT_WRITE		EVFILT_WRITE

typedef struct kevent os_event;

#elif defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>

# define EVENT_READ		(EPOLLIN | EPOLLHUP)
# define EVENT_WRITE		(EPOLLOUT | EPOLLHUP)

typedef struct epoll_event os_event;

#elif defined(HAVE_POLL)
# if defined(HAVE_POLL_H)
#  include <poll.h>
# elif defined(HAVE_SYS_POLL_H)
#  include <sys/poll.h>
# endif

# define EVENT_READ		(POLLIN | POLLHUP)
# define EVENT_WRITE		(POLLOUT | POLLHUP)

typedef struct pollfd os_event;

#elif defined(HAVE_SELECT)

# define EVENT_READ		0x1
# define EVENT_WRITE		0x2

#else
# error "kqueue, epoll, or poll APIs required."
#endif

#define EVENT_RW		(EVENT_READ|EVENT_WRITE)

#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif

#include <com/snert/lib/type/Vector.h>

typedef struct event Event;
typedef struct events Events;
typedef void (*EventHook)(Events *loop, Event *event);

typedef struct {
	EventHook io;			/* input ready or output buffer available */
	EventHook close;		/* called immediately before close() */
	EventHook error;		/* errno will be explicitly set */
} EventOn;

struct event {
	/* Private */
	FreeFn free;
	time_t expire;
	int io_type;
	int enable;

	/* Public */
	int fd;
	void *data;
	EventOn on;
	long timeout;
};

struct events {
	/* Public read only */
	JMP_BUF on_error;

	/* Private */
	int running;
	Vector events;
	os_event *set;
	unsigned set_size;
};

extern void eventFree(void *_event);
extern Event *eventAlloc(int fd, int type);
extern void eventInit(Event *event, int fd, int type);
extern void eventResetExpire(Event *event, const time_t *now);
extern void eventSetTimeout(Event *event, long ms);
extern long eventGetTimeout(Event *event);
extern void eventSetEnable(Event *event, int flag);
extern  int eventGetEnable(Event *event);

extern  int eventAdd(Events *loop, Event *event);
extern void eventClose(Events *loop, Event *event);
extern void eventRemove(Events *loop, Event *event);

extern  int eventsInit(Events *loop);
extern void eventsFree(Events *loop);
extern void eventsStop(Events *loop);
extern void eventsRun(Events *loop);
extern  int eventsWait(Events *loop, long ms);
extern long eventsTimeout(Events *loop, const time_t *now);
extern void eventsExpire(Events *loop, const time_t *expire);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_events_h__ */
