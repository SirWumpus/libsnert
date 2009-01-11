/*
 * grey.h
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_grey_h__
#define __com_snert_lib_mail_grey_h__	1

#include <time.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/Cache.h>
#include <com/snert/lib/util/setBitWord.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GREY_LIST_TUPLE_IP		1
#define GREY_LIST_TUPLE_HELO		2
#define GREY_LIST_TUPLE_MAIL		4
#define GREY_LIST_TUPLE_RCPT		8

typedef struct {
	long tuple;
	Cache cache;
#ifdef ENABLE_GREY_LIST_REJECT_COUNT
	long reject_count;		/* 0 = disable (default) */
#endif
	const char *key_prefix;		/* cache name space prefix */
#if defined(HAVE_PTHREAD_CREATE)
	pthread_mutex_t	*mutex;		/* mutex to control cache access */
#endif
} GreyList;

/* These values are negative so that they map into sfsistat and avoid
 * conflicting with libmilter SMFIS_* values. See smf.h & milter-sender.
 */
#define GREY_LIST_STATUS_UNKNOWN	(-1)
#define GREY_LIST_STATUS_CONTINUE	(-2)
#define GREY_LIST_STATUS_TEMPFAIL	(-3)
#define GREY_LIST_STATUS_REJECT		(-4)
#define GREY_LIST_STATUS_ERROR		(-5)

typedef struct {
	int status;
	time_t created;
	unsigned long count;
} GreyListEntry;

#define GREY_SCANF_FORMAT	"%lx %d %lu"
#define GREY_SCANF_DOT(v)	(long *) &(v).created, &(v).status, &(v).count
#define GREY_SCANF_ARROW(v)	(long *) &(v)->created, &(v)->status, &(v)->count

#define GREY_PRINTF_FORMAT	"%lx %d %lu"
#define GREY_PRINTF_DOT(v)	(long) (v).created, (v).status, (v).count
#define GREY_PRINTF_ARROW(v)	(long) (v)->created, (v)->status, (v)->count

extern void greyListSetDebug(long flags);

/**
 * @param grey
 *	A pointer to a GreyList configuration instance.
 *
 * @param entry
 *	A pointer to a GreyListEntry structure to be filled in. Can be NULL.
 *
 * @param block_time
 *	The grey-list block time in seconds. This is a separate
 *	parameter to allow different block times for different
 *	situations. See milter-gris -I and -b options.
 *
 * @param client_addr
 *	A C string for the client IP address. The GreyList->tuple
 *	may select this value to create the key for the cached
 *	GreyListEntry.
 *
 * @param helo
 *	A C string for the HELO argument. The GreyList->tuple
 *	may select this value to create the key for the cached
 *	GreyListEntry.
 *
 * @param mail
 *	A C string for the sender's address. The GreyList->tuple
 *	may select this value to create the key for the cached
 *	GreyListEntry.
 *
 * @param rcpt
 *	A C string for the recipient's address. The GreyList->tuple
 *	may select this value to create the key for the cached
 *	GreyListEntry.
 *
 * @return
 *	GREY_LIST_STATUS_TEMPFAIL is returned if this is the first
 *	a grey list key tuple has been seen or if the tuple is still
 *	within the block time.
 *
 *	GREY_LIST_STATUS_CONTINUE is returned if key tuple has been
 *	seen before and we're outside the grey list block time period.
#ifdef ENABLE_GREY_LIST_REJECT_COUNT
 *
 *	GREY_LIST_STATUS_REJECT is returned if the key tuple made too
 *	many retries during the grey-list block time so as to exceed
 *	GreyList->reject_count. (Depricated)
#endif
 */
extern int greyListCheck(
	GreyList *grey, GreyListEntry *entry, long block_time,
	const char *client_addr, const char *helo,
	const char *mail, const char *rcpt
);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_grey_h__ */
