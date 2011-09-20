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
 *** Events
 ***********************************************************************/

#ifdef USE_LIBEV

#include <ev.h>

#define EVENT_READ		EV_READ
#define EVENT_WRITE		EV_WRITE
#define EVENT_TIMER		EV_TIMER

typedef struct event Event;
typedef struct ev_loop Events;

typedef struct {
	ev_io io;
	ev_timer timeout;
} EventOn;

struct event {
	/* Private */
	FreeFn free;
	int enabled;
	Events *loop;

	/* Public */
	int fd;
	void *data;
	EventOn on;
	long timeout;
};

#define eventGetBase(e)			(Event *)((struct ev_watcher *) e)->data
#define eventDoIo(fn, l, e, f)		(*fn)(l, &(e)->on.io, f)
#define eventDoTimeout(fn, l, e, f)	(*fn)(l, &(e)->on.timeout, f)

#else /* SNERT_EVENTS */

# ifndef INFTIM
#  define INFTIM	(-1)
# endif

typedef struct event Event;
typedef struct events Events;
typedef void (*EventHook)(Events *loop, Event *event, int _reserved_);

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>

#endif
#if defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>

#endif
#if defined(HAVE_POLL)
# if defined(HAVE_POLL_H)
#  include <poll.h>
# elif defined(HAVE_SYS_POLL_H)
#  include <sys/poll.h>
# endif

#endif
#if defined(HAVE_SELECT)

#else
# error "kqueue, epoll, or poll APIs required."
#endif

#define EVENT_READ		0x1
#define EVENT_WRITE		0x2
#define EVENT_TIMER		0x4	/* not implemented yet */

typedef union {
#if defined(HAVE_KQUEUE)
	struct kevent k_ev;
#endif
#if defined(HAVE_EPOLL_CREATE)
	struct epoll_event e_ev;
#endif
#if defined(HAVE_POLL)
	struct pollfd p_ev;
#endif
} os_event;

#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif

#include <com/snert/lib/type/list.h>

typedef struct {
	EventHook io;			/* input ready or output buffer available */
	EventHook timeout;
} EventOn;

struct event {
	/* Private */
	FreeFn free;
	time_t expire;
	int io_type;
	int enabled;
	ListItem node;

	/* Public */
	int fd;				/* ro */
	void *data;			/* rw */
	EventOn on;			/* rw */
	long timeout;			/* rw */
};

struct events {
	/* Private */
	int running;
	List events;
	os_event *set;
	unsigned set_size;

	/* Public */
	JMP_BUF on_error;		/* ro */
};

#define eventGetBase(e)			(Event *)(e)
#define eventDoIo(fn, l, e, f)		(*fn)(l, e, f)
#define eventDoTimeout(fn, l, e, f)	(*fn)(l, e, f)

#endif /* SNERT_EVENTS */

extern void eventFree(void *_event);
extern Event *eventNew(int fd, int type);
extern void eventInit(Event *event, int fd, int type);
extern void eventSetTimeout(Event *event, long seconds);
extern long eventGetTimeout(Event *event);
extern void eventResetTimeout(Event *event);
extern void eventSetEnabled(Event *event, int flag);
extern  int eventGetEnabled(Event *event);
extern void eventSetType(Event *event, int type);
extern  int eventGetType(Event *event);
extern void eventSetCbIo(Event *event, void *_cb);
extern void eventSetCbTimer(Event *event, void *_cb);

extern  int eventAdd(Events *loop, Event *event);
extern void eventRemove(Events *loop, Event *event);

extern Events *eventsNew(void);
extern void eventsFree(Events *loop);
extern void eventsStop(Events *loop);
extern void eventsRun(Events *loop);

extern void eventsWaitFnSet(const char *name);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_events_h__ */
