/*
 * sqlite3.c
 *
 * Sqlite3 Support Functions
 *
 * Copyright 2011 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/sqlite3.h>

/***********************************************************************
 *** Code bassed on http://www.sqlite.org/unlock_notify.html
 ***********************************************************************/

typedef struct {
	int unlocked;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
} unlock_condition;

static void
unlock_notify_cb(void **table, int length)
{
	int i;
	unlock_condition *un;

	for (i = 0; i < length; i++) {
		un = table[i];
		PTHREAD_MUTEX_LOCK(&un->mutex);
		un->unlocked = 1;
		(void) pthread_cond_signal(&un->cond);
		PTHREAD_MUTEX_UNLOCK(&un->mutex);
	}
}

static int
unlock_notify_wait(sqlite3 *db)
{
	int rc;
	unlock_condition un;

	un.unlocked = 0;
	(void) pthread_cond_init(&un.cond, 0);
	(void) pthread_mutex_init(&un.mutex, 0);

	rc = sqlite3_unlock_notify(db, unlock_notify_cb, (void *)&un);

	/* The call to sqlite3_unlock_notify() always returns either
	 * SQLITE_LOCKED or SQLITE_OK.
	 *
	 * If SQLITE_LOCKED was returned, then the system is deadlocked.
	 * In this case this function needs to return SQLITE_LOCKED to
	 * the caller so that the current transaction can be rolled back.
	 *
	 * Otherwise, block until the unlock-notify callback is invoked,
	 * then return SQLITE_OK.
	 */
	if (rc == SQLITE_OK) {
		PTHREAD_MUTEX_LOCK(&un.mutex);
		while (!un.unlocked) {
			pthread_cond_wait(&un.cond, &un.mutex);
		}
		PTHREAD_MUTEX_UNLOCK(&un.mutex);
	}

	pthread_mutex_destroy(&un.mutex);
	pthread_cond_destroy(&un.cond);

	return rc;
}

int
sqlite3_prepare_v2_blocking(sqlite3 *db, const char *sql, int sql_len, sqlite3_stmt **stmt, const char **stop)
{
	int rc;

	while ((rc = sqlite3_prepare_v2(db, sql, sql_len, stmt, stop)) == SQLITE_LOCKED) {
		rc = unlock_notify_wait(db);
		if (rc != SQLITE_OK)
			break;
	}

	return rc;
}

int
sqlite3_step_blocking(sqlite3_stmt *stmt)
{
	int rc;

	while ((rc = sqlite3_step(stmt)) == SQLITE_LOCKED) {
		rc = unlock_notify_wait(sqlite3_db_handle(stmt));
		if (rc != SQLITE_OK)
			break;
		sqlite3_reset(stmt);
	}

	return rc;
}

/***********************************************************************
 *** END
 ***********************************************************************/
