/*
 * grey.c
 *
 * Copyright 2004, 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/mail/grey.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/type/Data.h>

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

#define GREY_LIST_KEY_TUPLE_LENGTH	1024

static long debug;

static GreyListEntry cacheUndefinedEntry = { GREY_LIST_STATUS_UNKNOWN, 0, 0 };

void
greyListInit(void)
{
}

void
greyListSetDebug(long flags)
{
	debug = flags;
}

int
greyListCacheGet(GreyList *grey, char *name, GreyListEntry *entry)
{
	int rc;
	Data value;
	struct data key;

	rc = -1;
	*entry = cacheUndefinedEntry;
	DataInitWithBytes(&key, (unsigned char *) name, strlen(name)+1);

#if defined(HAVE_PTHREAD_CREATE)
	if (pthread_mutex_lock(grey->mutex))
		syslog(LOG_ERR, "mutex lock in greyListCacheGet() failed: %s (%d) ", strerror(errno), errno);
#endif
	value = grey->cache->get(grey->cache, &key);

#if defined(HAVE_PTHREAD_CREATE)
	if (pthread_mutex_unlock(grey->mutex))
		syslog(LOG_ERR, "mutex unlock in greyListCacheGet() failed: %s (%d) ", strerror(errno), errno);
#endif
	if (value != NULL) {
		if (value->length(value) == sizeof (GreyListEntry)) {
			*entry = *(GreyListEntry *)(value->base(value));
			rc = 0;
		}
		value->destroy(value);
	}

	if (debug)
		syslog(LOG_DEBUG, "cache get key={%s} value={" GREY_PRINTF_FORMAT "} rc=%d", name, GREY_PRINTF_ARROW(entry), rc);

	return rc;
}

int
greyListCachePut(GreyList *grey, char *name, GreyListEntry *entry)
{
	int rc;
	struct data key, value;

	DataInitWithBytes(&key, (unsigned char *) name, strlen(name)+1);
	DataInitWithBytes(&value, (unsigned char *) entry, sizeof (*entry));

#if defined(HAVE_PTHREAD_CREATE)
	if (pthread_mutex_lock(grey->mutex))
		syslog(LOG_ERR, "mutex lock in greyListCachePut() failed: %s (%d) ", strerror(errno), errno);
#endif
	rc = grey->cache->put(grey->cache, &key, &value);

#if defined(HAVE_PTHREAD_CREATE)
	if (pthread_mutex_unlock(grey->mutex))
		syslog(LOG_ERR, "mutex unlock in greyListCachePut() failed: %s (%d) ", strerror(errno), errno);
#endif
	if (debug)
		syslog(LOG_DEBUG, "cache put key={%s} value={" GREY_PRINTF_FORMAT "} rc=%d", name, GREY_PRINTF_ARROW(entry), rc);

	return rc;
}


int
greyListCheck(GreyList *grey, GreyListEntry *out, long block_time, const char *client_addr, const char *helo, const char *mail, const char *rcpt)
{
	int i, n;
	time_t now;
	size_t length;
	GreyListEntry entry;
	char key_tuple[GREY_LIST_KEY_TUPLE_LENGTH];

	if (grey == NULL || grey->tuple <= 0)
		return GREY_LIST_STATUS_ERROR;

	(void) time(&now);

	if (block_time < 0) {
		entry.status = GREY_LIST_STATUS_CONTINUE;
		entry.created = now;
#ifdef ENABLE_GREY_LIST_REJECT_COUNT
		entry.count = 0;
#endif
		goto error0;
	}

	/* Construct the lookup key tuple...
	 */
	length = grey->key_prefix == NULL ? 0 : snprintf(key_tuple, sizeof (key_tuple), "%s", grey->key_prefix);

	for (i = GREY_LIST_TUPLE_IP; i <= GREY_LIST_TUPLE_RCPT; i <<= 1) {
		switch (grey->tuple & i) {
		case GREY_LIST_TUPLE_IP:
			if (client_addr != NULL) {
				n = snprintf(key_tuple + length, sizeof (key_tuple) - length, ",%s", client_addr);
				if (sizeof (key_tuple)-length <= n) {
					break;
				}
				length += n;
			}
			break;
		case GREY_LIST_TUPLE_HELO:
			if (helo != NULL) {
				n = snprintf(key_tuple + length, sizeof (key_tuple) - length, ",%s", helo);
				if (sizeof (key_tuple)-length <= n) {
					break;
				}
				length += n;
			}
			break;
		case GREY_LIST_TUPLE_MAIL:
			if (mail != NULL) {
				n = snprintf(key_tuple + length, sizeof (key_tuple) - length, ",%s", mail);
				if (sizeof (key_tuple)-length <= n) {
					break;
				}
				length += n;
			}
			break;
		case GREY_LIST_TUPLE_RCPT:
			if (rcpt != NULL) {
				n = snprintf(key_tuple + length, sizeof (key_tuple) - length, ",%s", rcpt);
				if (sizeof (key_tuple)-length <= n) {
					break;
				}
				length += n;
			}
			break;
		}
	}

	/* Flatten the case, since Sendmail tends to be case-insensitive.
	 * Local-parts are case sensitive, but Sendmail doesn't care.
	 */
	TextLower(key_tuple, -1);

	/* ...Then check if we have already seen this tuple before...
	 */
	if (greyListCacheGet(grey, key_tuple, &entry)) {
		/* We have never seen this tuple before, prepare an entry
		 * in the TEMPFAIL state.
		 */
		if (debug)
			syslog(LOG_DEBUG, "no grey listing for {%s}", key_tuple);

		entry.status = GREY_LIST_STATUS_TEMPFAIL;
		entry.created = now;
#ifdef ENABLE_GREY_LIST_REJECT_COUNT
		entry.count = 0;
#endif
	}

	/* ...If so, is the tuple still in the TEMPFAIL state? ...
	 */
	if (entry.status == GREY_LIST_STATUS_TEMPFAIL) {
		/* Is the tuple still being temporarily blocked? */
		if (now < entry.created + block_time) {
#ifdef ENABLE_GREY_LIST_REJECT_COUNT
			entry.count++;
#endif
			goto error1;
		}

#ifdef ENABLE_GREY_LIST_REJECT_COUNT
		/* We've reached the end of the temporary blocking period,
		 * so now we check if the number of delivery attempts made
		 * during that period was excessive (ie. think mail cannons
		 * and stupidily short queue retry times), in which case
		 * reject the message. See the milter-gris -r option.
		 */
		if (0 < grey->reject_count && grey->reject_count < entry.count) {
			syslog(LOG_INFO, "blocked grey listing {%s}, count=%ld", key_tuple, entry.count);
			entry.status = GREY_LIST_STATUS_REJECT;
			goto error1;
		}
#endif
		/* Once we get this far, we know the number of delivery
		 * attempts was reasonable and that the server appears
		 * to queue mail, so we can upgrade the state to CONTINUE.
		 */
		if (debug)
			syslog(LOG_DEBUG, "upgrading grey listing for {%s}", key_tuple);

		entry.status = GREY_LIST_STATUS_CONTINUE;
	}

	/* Touch an existing entry so as to maintain active tuples. */
	entry.created = now;
error1:
	if (greyListCachePut(grey, key_tuple, &entry))
		syslog(LOG_WARNING, "failed to update grey listing for {%s}", key_tuple);
error0:
	if (out != NULL)
		*out = entry;

	return entry.status;
}
