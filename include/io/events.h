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

typedef struct event Event;
typedef struct events Events;
typedef void (*EventHook)(Events *loop, Event *event);

#if defined(HAVE_KQUEUE)
# include <sys/types.h>
# include <sys/event.h>
# include <sys/time.h>

extern int events_wait_kqueue(Events *loop, long ms);

#endif
#if defined(HAVE_EPOLL_CREATE)
# include <sys/epoll.h>

extern int events_wait_epoll(Events *loop, long ms);

#endif
#if defined(HAVE_POLL)
# if defined(HAVE_POLL_H)
#  include <poll.h>
# elif defined(HAVE_SYS_POLL_H)
#  include <sys/poll.h>
# endif

extern int events_wait_poll(Events *loop, long ms);

#endif
#if defined(HAVE_SELECT)
extern int events_wait_select(Events *loop, long ms);

#else
# error "kqueue, epoll, or poll APIs required."
#endif

#define EVENT_READ		0x1
#define EVENT_WRITE		0x2

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
	List events;
	os_event *set;
	unsigned set_size;
};

extern void eventFree(void *_event);
extern Event *eventNew(int fd, int type);
extern void eventInit(Event *event, int fd, int type);
extern void eventResetExpire(Event *event, const time_t *now);
extern void eventSetTimeout(Event *event, long seconds);
extern long eventGetTimeout(Event *event);
extern void eventSetEnabled(Event *event, int flag);
extern  int eventGetEnabled(Event *event);
extern void eventSetType(Event *event, int type);
extern  int eventGetType(Event *event);

extern  int eventAdd(Events *loop, Event *event);
extern void eventRemove(Events *loop, Event *event);

extern  int eventsInit(Events *loop);
extern void eventsFree(Events *loop);
extern void eventsStop(Events *loop);
extern void eventsRun(Events *loop);
extern  int eventsWait(Events *loop, long ms);
extern long eventsTimeout(Events *loop, const time_t *now);
extern void eventsExpire(Events *loop, const time_t *expire);

extern int (*events_wait_fn)(Events *loop, long ms);
extern void eventsWaitFnSet(const char *name);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_events_h__ */
