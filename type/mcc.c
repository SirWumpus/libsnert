/*
 * mcc.c
 *
 * Multicast Cache
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef MCC_LISTENER_TIMEOUT
#define MCC_LISTENER_TIMEOUT	10000
#endif

#ifndef MCC_MULTICAST_PORT
#define MCC_MULTICAST_PORT	6920
#endif

#ifndef MCC_UNICAST_PORT
#define MCC_UNICAST_PORT	6921
#endif

#ifndef MCC_STEP_BUSY_DELAY
#define MCC_STEP_BUSY_DELAY	2
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/type/mcc.h>

#ifdef HAVE_SQLITE3_H

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef __MINGW32__
# ifdef HAVE_SYSLOG_H
#  include <syslog.h>
# endif
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/sys/Time.h>

static int debug;

#define MCC_KEY_FMT	"key={%." MCC_MAX_KEY_SIZE_S "s}"

/***********************************************************************
 ***
 ***********************************************************************/

void
mccStringFree(void *_note)
{
	mcc_string *note = _note;

	if (note != NULL) {
		free(note->string);
		free(note);
	}
}

void
mccStringReplace(mcc_string *note, const char *str)
{
	if (note != NULL && str != NULL) {
		free(note->string);
		note->string = strdup(str);
	}
}

mcc_string *
mccStringCreate(const char *str)
{
	mcc_string *note;

	if (str == NULL)
		return NULL;

	if ((note = calloc(1, sizeof (*note))) != NULL)
		mccStringReplace(note, str);

	return note;
}

mcc_string *
mccNotesFind(mcc_string *notes, const char *str)
{
	for ( ; notes != NULL; notes = notes->next) {
		if (strstr(notes->string, str) != NULL)
			break;
	}

	return notes;
}

void
mccNotesUpdate(mcc_context *mcc, const char *ip, const char *find, const char *text)
{
	mcc_string *note;
	mcc_active_host *host;

	if (mcc == NULL || ip == NULL || find == NULL || text == NULL)
		return;

	PTHREAD_MUTEX_LOCK(&mcc->active_mutex);

	host = mccFindActive(mcc, ip);
	note = mccNotesFind(host->notes, find);

	if (note == NULL) {
		if ((note = mccStringCreate(text)) != NULL) {
			note->next = host->notes;
			host->notes = note;
		}
	} else {
		mccStringReplace(note, text);
	}

	PTHREAD_MUTEX_UNLOCK(&mcc->active_mutex);
}

void
mccNotesFree(mcc_string *notes)
{
	mcc_string *next;

	for ( ; notes != NULL; notes = next) {
		next = notes->next;
		mccStringFree(notes);
	}
}

/***********************************************************************
 *** Active Host Hash Table
 ***********************************************************************/

/*
 * D.J. Bernstien Hash version 2 (+ replaced by ^).
 */
static unsigned long
djb_hash_index(const unsigned char *buffer, size_t size, size_t table_size)
{
	unsigned long hash = 5381;

	while (0 < size--)
		hash = ((hash << 5) + hash) ^ *buffer++;

	return hash & (table_size-1);
}

mcc_active_host *
mccFindActive(mcc_context *mcc, const char *ip)
{
	unsigned i;
	size_t ip_len;
	unsigned long hash;
	mcc_active_host *entry, *oldest;

	ip_len = strlen(ip);
	hash = djb_hash_index((const unsigned char *) ip, ip_len, MCC_HASH_TABLE_SIZE);
	oldest = &mcc->active[hash];

	for (i = 0; i < MCC_MAX_LINEAR_PROBE; i++) {
		entry = &mcc->active[(hash + i) & (MCC_HASH_TABLE_SIZE-1)];

		if (entry->touched == 0)
			oldest = entry;

		if (strcmp(ip, entry->ip) == 0)
			break;
	}

	/* If we didn't find the entry within the linear probe
	 * distance, then overwrite the oldest hash entry. Note
	 * that we take the risk of two or more IPs repeatedly
	 * cancelling out each other's entry. Shit happens.
	 */
	if (MCC_MAX_LINEAR_PROBE <= i) {
		entry = oldest;
		entry->max_ppm = 0;
		(void) TextCopy(entry->ip, sizeof (entry->ip), ip);
		memset(entry->intervals, 0, sizeof (entry->intervals));
		mccNotesFree(entry->notes);
		entry->notes = NULL;
	}

	return entry;
}

unsigned long
mccGetRate(mcc_interval *intervals, unsigned long ticks)
{
	int i;
	mcc_interval *interval;
	unsigned long count = 0;

	/* Sum the counts within this window. */
	interval = intervals;
	for (i = 0; i < MCC_INTERVALS; i++) {
		if (ticks - MCC_INTERVALS <= interval->ticks && interval->ticks <= ticks)
			count += interval->count;
		interval++;
	}

	return count;
}

unsigned long
mccUpdateRate(mcc_interval *intervals, unsigned long ticks)
{
	mcc_interval *interval;

	/* Update the current interval. */
	interval = &intervals[ticks % MCC_INTERVALS];
	if (interval->ticks != ticks) {
		interval->ticks = ticks;
		interval->count = 0;
	}
	interval->count++;

	return mccGetRate(intervals, ticks);
}

void
mccUpdateActive(mcc_handle *mcc, const char *ip, uint32_t *touched)
{
	unsigned long rate;
	mcc_active_host *entry;

	if (mcc == NULL || ip == NULL)
		return;

	PTHREAD_MUTEX_LOCK(&mcc->active_mutex);

	entry = mccFindActive(mcc, ip);
	entry->touched = *touched;

	rate = mccUpdateRate(entry->intervals, *touched / MCC_TICK);
	if (entry->max_ppm < rate)
		entry->max_ppm = rate;

	if (0 < debug)
		syslog(LOG_DEBUG, "multi/unicast cache active ip=%s ppm=%lu max-ppm=%lu", ip, rate, entry->max_ppm);

	PTHREAD_MUTEX_UNLOCK(&mcc->active_mutex);
}

Vector
mccGetActive(mcc_handle *mcc)
{
	unsigned i;
	Vector hosts;
	mcc_active_host *entry;

	if ((hosts = VectorCreate(10)) == NULL)
		return NULL;

	VectorSetDestroyEntry(hosts, free);

	PTHREAD_MUTEX_LOCK(&mcc->active_mutex);

	for (i = 0; i < sizeof (mcc->active) / sizeof (mcc_active_host); i++) {
		if (mcc->active[i].touched == 0)
			continue;

		if ((entry = malloc(sizeof (*entry))) != NULL) {
			*entry = mcc->active[i];
			if (VectorAdd(hosts, entry))
				free(entry);
		}
	}

	PTHREAD_MUTEX_UNLOCK(&mcc->active_mutex);

	return hosts;
}

/***********************************************************************
 ***
 ***********************************************************************/

void
mccSetDebug(int level)
{
	debug = level;
}

static int on_corrupt = MCC_ON_CORRUPT_REPLACE;

void
mccSetOnCorrupt(int level)
{
	on_corrupt = level;
}

static const char *synchronous[] = { "OFF", "NORMAL", "FULL", NULL };
static const char *synchronous_stmt[] = {
	MCC_SQL_PRAGMA_SYNC_OFF,
	MCC_SQL_PRAGMA_SYNC_NORMAL,
	MCC_SQL_PRAGMA_SYNC_FULL,
	NULL
};

int
mccSetMulticastTTL(mcc_handle *mcc, int ttl)
{
	if (socketMulticastTTL(mcc->multicast.socket, ttl) == SOCKET_ERROR)
		return -1;

	return 0;
}

int
mccRegisterKey(mcc_context *mcc, mcc_key_hook *tag_hook)
{
	return VectorAdd(mcc->key_hooks, tag_hook);
}

int
mccSend(mcc_handle *mcc, mcc_row *row, uint8_t command)
{
	int rc;
	md5_state_t md5;
	SocketAddress **unicast;

	if (!mcc->multicast.is_running && !mcc->unicast.is_running)
		return MCC_OK;

	row->command = command;

	mccUpdateActive(mcc, "127.0.0.1", &row->touched);

	/* Covert some of the values to network byte order. */
	row->hits = htonl(row->hits);
	row->created = htonl(row->created);
	row->touched = htonl(row->touched);
	row->expires = htonl(row->expires);

	md5_init(&md5);
	md5_append(&md5, (md5_byte_t *) row+sizeof (row->digest), sizeof (*row)-sizeof (row->digest));
	md5_append(&md5, (md5_byte_t *) mcc->secret, mcc->secret_length);
	md5_finish(&md5, (md5_byte_t *) row->digest);

	rc = MCC_OK;

	if (mcc->multicast.is_running) {
		if (1 < debug)
			syslog(LOG_DEBUG, "mccSend multicast command=%c " MCC_KEY_FMT, row->command, row->key_data);
		PTHREAD_MUTEX_LOCK(&mcc->mutex);
		if (socketWriteTo(mcc->multicast.socket, (unsigned char *) row, sizeof (*row), mcc->multicast_ip) != sizeof (*row))
			rc = MCC_ERROR;
		PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
	}

	if (mcc->unicast.is_running) {
		if (1 < debug)
			syslog(LOG_DEBUG, "mccSend unicast command=%c " MCC_KEY_FMT, row->command, row->key_data);
		PTHREAD_MUTEX_LOCK(&mcc->mutex);
		for (unicast = mcc->unicast_ip; *unicast != NULL; unicast++) {
			if (socketWriteTo(mcc->unicast.socket, (unsigned char *) row, sizeof (*row), *unicast) != sizeof (*row))
				rc = MCC_ERROR;
		}
		PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
	}

	/* Restore our record. */
	row->hits = ntohl(row->hits);
	row->created = ntohl(row->created);
	row->touched = ntohl(row->touched);
	row->expires = ntohl(row->expires);

	return rc;
}

static int
mcc_sql_count(void *data, int ncolumns, char **col_values, char **col_names)
{
	(*(int*) data)++;
	return 0;
}

static int
mcc_sql_create(mcc_handle *sql)
{
	int count;
	char *error;

	count = 0;
	if (sqlite3_exec(sql->db, MCC_SQL_TABLE_EXISTS, mcc_sql_count, &count, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s error %s: %s", sql->path, MCC_SQL_TABLE_EXISTS, error);
		sqlite3_free(error);
		return -1;
	}

	if (count != 1 && sqlite3_exec(sql->db, MCC_SQL_CREATE_TABLE, NULL, NULL, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s error %s: %s", sql->path, MCC_SQL_CREATE_TABLE, error);
		sqlite3_free(error);
		return -1;
	}

	count = 0;
	if (sqlite3_exec(sql->db, MCC_SQL_INDEX_EXISTS, mcc_sql_count, &count, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s error %s: %s", sql->path, MCC_SQL_INDEX_EXISTS, error);
		sqlite3_free(error);
		return -1;
	}

	if (count != 1 && sqlite3_exec(sql->db, MCC_SQL_CREATE_INDEX, NULL, NULL, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s error %s: %s", sql->path, MCC_SQL_CREATE_INDEX, error);
		sqlite3_free(error);
		return -1;
	}

	return 0;
}

static int
mcc_sql_stmts_prepare(mcc_handle *mcc)
{
	if (mcc->hook.prepare != NULL && (*mcc->hook.prepare)(mcc, mcc->hook.data)) {
		syslog(LOG_ERR, "sql=%s prepare hook failed", mcc->path);
		return -1;
	}

	/* Using the newer sqlite_prepare_v2() interface will
	 * handle SQLITE_SCHEMA errors automatically.
	 */
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_SELECT_ONE, -1, &mcc->select_one, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_SELECT_ONE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_REPLACE, -1, &mcc->replace, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_REPLACE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_TRUNCATE, -1, &mcc->truncate, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_TRUNCATE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_EXPIRE, -1, &mcc->expire, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_EXPIRE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_DELETE, -1, &mcc->remove, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_DELETE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_BEGIN, -1, &mcc->begin, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_BEGIN, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_COMMIT, -1, &mcc->commit, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_COMMIT, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_ROLLBACK, -1, &mcc->rollback, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s %s", mcc->path, MCC_SQL_ROLLBACK, sqlite3_errmsg(mcc->db));
		return -1;
	}

	if (mccSetSync(mcc, MCC_SYNC_OFF) == MCC_ERROR) {
		syslog(LOG_ERR, "sql=%s error %s: %s", mcc->path, synchronous_stmt[MCC_SYNC_OFF], sqlite3_errmsg(mcc->db));
		return -1;
	}

	return 0;
}

static void
mcc_sql_stmts_finalize(mcc_handle *mcc)
{
	if (mcc->hook.finalize != NULL)
		(void) (*mcc->hook.finalize)(mcc, mcc->hook.data);

	if (mcc->select_one != NULL) {
		(void) sqlite3_finalize(mcc->select_one);
		mcc->select_one = NULL;
	}
	if (mcc->replace != NULL) {
		(void) sqlite3_finalize(mcc->replace);
		mcc->replace = NULL;
	}
	if (mcc->truncate != NULL) {
		(void) sqlite3_finalize(mcc->truncate);
		mcc->truncate = NULL;
	}
	if (mcc->expire != NULL) {
		(void) sqlite3_finalize(mcc->expire);
		mcc->expire = NULL;
	}
	if (mcc->remove != NULL) {
		(void) sqlite3_finalize(mcc->remove);
		mcc->remove = NULL;
	}
	if (mcc->begin != NULL) {
		(void) sqlite3_finalize(mcc->begin);
		mcc->begin = NULL;
	}
	if (mcc->commit != NULL) {
		(void) sqlite3_finalize(mcc->commit);
		mcc->commit = NULL;
	}
	if (mcc->rollback != NULL) {
		(void) sqlite3_finalize(mcc->rollback);
		mcc->rollback = NULL;
	}
}

static int
mcc_sql_recreate(mcc_handle *mcc)
{
	int rc;

	syslog(LOG_ERR, "closing corrupted sqlite db %s...", mcc->path);
	mcc_sql_stmts_finalize(mcc);
	sqlite3_close(mcc->db);

	if (on_corrupt == MCC_ON_CORRUPT_RENAME) {
		char *new_name = strdup(mcc->path);
		if (new_name == NULL) {
			syslog(LOG_ERR, "mcc_sql_recreate: (%d) %s", errno, strerror(errno));
			exit(1);
		}
		new_name[strlen(new_name)-1] = 'X';
		(void) unlink(new_name);
		if (rename(mcc->path, new_name)) {
			syslog(LOG_ERR, "sql=%s rename to %s error: (%d) %s", mcc->path, new_name, errno, strerror(errno));
			free(new_name);
			exit(1);
		}
		free(new_name);
	} else if (on_corrupt == MCC_ON_CORRUPT_REPLACE) {
		if (unlink(mcc->path)) {
			syslog(LOG_ERR, "sql=%s unlink error: (%d) %s", mcc->path, errno, strerror(errno));
			goto error0;
		}
	}

	syslog(LOG_INFO, "creating new sqlite db %s...", mcc->path);
	if ((rc = sqlite3_open(mcc->path, &mcc->db)) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s open error: %s", mcc->path, sqlite3_errmsg(mcc->db));
		goto error0;
	}

	if (mcc_sql_create(mcc))
		goto error1;

	if (mcc_sql_stmts_prepare(mcc))
		goto error2;

	syslog(LOG_INFO, "sqlite db %s ready", mcc->path);

	return 0;
error2:
	mcc_sql_stmts_finalize(mcc);
error1:
	sqlite3_close(mcc->db);
error0:
	return -1;
}

int
mccSqlStep(mcc_handle *mcc, sqlite3_stmt *sql_stmt, const char *sql_stmt_text)
{
	int rc;

#ifdef HAVE_PTHREAD_SETCANCELSTATE
	int old_state;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif
	if (sql_stmt == mcc->commit || sql_stmt == mcc->rollback)
		mcc->is_transaction = 0;

	/* Using the newer sqlite_prepare_v2() interface means that
	 * sqlite3_step() will return more detailed error codes. See
	 * sqlite3_step() API reference.
	 */
	while ((rc = sqlite3_step(sql_stmt)) == SQLITE_BUSY && !mcc->is_transaction) {
		if (0 < debug)
			syslog(LOG_WARN, "sql=%s busy: %s", mcc->path, sql_stmt_text);

		pthreadSleep(1, 0);
	}

	if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
		syslog(LOG_ERR, "sql=%s step error (%d): %s; errno=%d stmt=%s", mcc->path, rc, sqlite3_errmsg(mcc->db), errno, sql_stmt_text);

		if (rc == SQLITE_CORRUPT || rc == SQLITE_CANTOPEN) {
			if (on_corrupt == MCC_ON_CORRUPT_EXIT)
				exit(1);
			(void) mcc_sql_recreate(mcc);
		}
	}

	if (rc != SQLITE_ROW) {
		/* http://www.sqlite.org/cvstrac/wiki?p=DatabaseIsLocked
		 *
		 * "Sometimes people think they have finished with a SELECT statement
		 *  because sqlite3_step() has returned SQLITE_DONE. But the SELECT is
		 *  not really complete until sqlite3_reset() or sqlite3_finalize()
		 *  have been called.
		 */
		(void) sqlite3_reset(sql_stmt);
	}

	if (sql_stmt == mcc->begin)
		mcc->is_transaction = 1;

#ifdef HAVE_PTHREAD_SETCANCELSTATE
	pthread_setcancelstate(old_state, NULL);
#endif
	return rc;
}

int
mccDeleteKey(mcc_handle *mcc, const unsigned char *key, unsigned length)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || key == NULL)
		return MCC_ERROR;

	PTHREAD_MUTEX_LOCK(&mcc->mutex);

	if (sqlite3_bind_text(mcc->remove, 1, (const char *) key, length, SQLITE_STATIC) == SQLITE_OK) {
		if (mccSqlStep(mcc, mcc->remove, MCC_SQL_DELETE) == SQLITE_DONE)
			rc = MCC_OK;
		(void) sqlite3_clear_bindings(mcc->remove);
	}

	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

	return rc;
}

int
mccSetSyncByName(mcc_handle *mcc, const char *name)
{
	int i;

	for (i = 0; synchronous[i] != NULL; i++) {
		if (TextInsensitiveCompare(name, synchronous[i]) == 0)
			return mccSetSync(mcc, i);
	}

	return MCC_ERROR;
}

int
mccSetSync(mcc_handle *mcc, int level)
{
	int rc;
	char *error;
#ifdef HAVE_PTHREAD_SETCANCELSTATE
	int old_state;
#endif
	rc = MCC_ERROR;

	if (mcc == NULL || level < MCC_SYNC_OFF || MCC_SYNC_FULL < level)
		return MCC_ERROR;

	PTHREAD_MUTEX_LOCK(&mcc->mutex);
#ifdef HAVE_PTHREAD_SETCANCELSTATE
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif
	if (sqlite3_exec(mcc->db, synchronous_stmt[level], NULL, NULL, &error) == SQLITE_OK) {
		rc = MCC_OK;
	} else {
		syslog(LOG_ERR, "sql=%s error %s: %s", mcc->path, synchronous_stmt[level], error);
		sqlite3_free(error);
	}
#ifdef HAVE_PTHREAD_SETCANCELSTATE
	pthread_setcancelstate(old_state, NULL);
#endif
	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

	return rc;
}

int
mccPutRowLocal(mcc_handle *mcc, mcc_row *row, int touch)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || row == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&mcc->mutex);

	if (touch) {
		row->hits++;
		row->touched = (uint32_t) time(NULL);
	}

	if (sqlite3_bind_text(mcc->replace, 1, (const char *) row->key_data, row->key_size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_text(mcc->replace, 2, (const char *) row->value_data, row->value_size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error2;

	if (sqlite3_bind_int(mcc->replace, 3, (int) row->hits) != SQLITE_OK)
		goto error2;

	if (sqlite3_bind_int(mcc->replace, 4, (int) row->created) != SQLITE_OK)
		goto error2;

	if (sqlite3_bind_int(mcc->replace, 5, (int) row->touched) != SQLITE_OK)
		goto error2;

	if (sqlite3_bind_int(mcc->replace, 6, (int) row->expires) != SQLITE_OK)
		goto error2;

	if (mccSqlStep(mcc, mcc->replace, MCC_SQL_REPLACE) == SQLITE_DONE)
		rc = MCC_OK;
error2:
	(void) sqlite3_clear_bindings(mcc->replace);
error1:
	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
error0:
	return rc;
}

/*
 * This function is generic for both UDP unicast and multicast.
 */
static void *
mcc_listener_thread(void *data)
{
	long nbytes;
	int is_unicast;
	md5_state_t md5;
	mcc_handle *mcc;
	SocketAddress from;
	mcc_network *listener;
	mcc_row new_row, old_row;
	mcc_key_hook **hooks, *hook;
	unsigned char our_digest[16];
	char ip[IPV6_STRING_LENGTH], *listen_addr;

	mcc = ((mcc_listener *) data)->mcc;
	listener = ((mcc_listener *) data)->listener;
	free(data);

	is_unicast = (&mcc->unicast == listener);

	listen_addr = socketAddressToString(&listener->socket->address);
	PTHREAD_PUSH_FREE(listen_addr);

	syslog(LOG_INFO, "started multi/unicast listener %s", listen_addr);

	for (listener->is_running = 1; listener->is_running; ) {
		if (!socketHasInput(listener->socket, MCC_LISTENER_TIMEOUT)) {
			if (1 < debug)
				syslog(LOG_WARN, "multi/unicast socket timeout: %s (%d)", strerror(errno), errno);
			continue;
		}

		PTHREAD_MUTEX_LOCK(&mcc->mutex);
		nbytes = socketReadFrom(listener->socket, (unsigned char *) &new_row, sizeof (new_row), &from);
		PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

		if (nbytes <= 0) {
			syslog(LOG_ERR, "multi/unicast socket read error: %s (%d)", strerror(errno), errno);
			continue;
		}

		(void) socketAddressGetString(&from, 0, ip, sizeof (ip));

		if (1 < debug)
			syslog(LOG_DEBUG, "multi/unicast listener=%s from=%s cmd=%c", listen_addr, ip, new_row.command);

		md5_init(&md5);
		md5_append(&md5, (md5_byte_t *) &new_row + sizeof (new_row.digest), sizeof (new_row)-sizeof (new_row.digest));
		md5_append(&md5, (md5_byte_t *) mcc->secret, mcc->secret_length);
		md5_finish(&md5, (md5_byte_t *) our_digest);

		new_row.hits = ntohl(new_row.hits);
		new_row.created = ntohl(new_row.created);
		new_row.touched = ntohl(new_row.touched);
		new_row.expires = ntohl(new_row.expires);

		mccUpdateActive(mcc, ip, &new_row.touched);

		if (memcmp(our_digest, new_row.digest, sizeof (our_digest)) != 0) {
			syslog(LOG_ERR, "multi/unicast cache digest error from [%s]", ip);
			mccNotesUpdate(mcc, ip, "md5=", "md5=N");
			continue;
		}

		mccNotesUpdate(mcc, ip, "md5=", "md5=Y");

		if (1 < debug) {
			if (new_row.key_size < MCC_MAX_KEY_SIZE)
				new_row.key_data[new_row.key_size] = '\0';
			syslog(LOG_DEBUG, "multi/unicast cache packet [%s] command=%c " MCC_KEY_FMT, ip, new_row.command, new_row.key_data);
		}

		switch (new_row.command) {
		case MCC_CMD_PUT:
			/* The multicast cache for efficiency only broadcasts
			 * on put and fetches from the local cache on get.
			 * There is no multicast query/get as this would
			 * introduce the need for timeouts and conflict
			 * resolution. So in the event that one of our peers
			 * goes off-line and becomes out of sync with the rest
			 * of the group we have to implement a "correction"
			 * strategy. This means the out-of-sync peer will act
			 * on incorrect information at least once before
			 * receiving a correction.
			 *
			 * The correction strategy requires a record version
			 * number and create timestamp associated with each
			 * key-value pair. We check the version and create
			 * time of the local key-value pair before accepting
			 * an update according to the following rules:
			 *
			 * a) Accept the update if we have no local record.
			 *
			 * b) If the received record has version number zero,
			 * replace our local record. This can occur when the
			 * version counter rolls over and is treated as a sync
			 * point. Normally this should not occur if records
			 * are expired regularly before a roll over state ever
			 * occurs. Yet this does allow for manual overrides.
			 *
			 * c) If the local record is older than the received
			 * record, then discard the received record and
			 * broadcast our local record as a correction.
			 *
			 * d) If the create times of the local and received
			 * records are the same, but the received record has
			 * an older version number, broadcast our local record
			 * as a correction.
			 *
			 * e) If the create times of the local and received
			 * records are the same, but the received record has
			 * a newer version number, accept it.
			 *
			 * f) If the create times of the local and received
			 * records are the same and the version numbers of the
			 * received and local records are the same, then simply
			 * discard the received record. This can still result
			 * in out-of-sync data within the group, but avoids
			 * broadcast correction loops. This degraded state can
			 * occur when an out-of-sync peer's record is only one
			 * version behind its peers.
			 */

			PTHREAD_MUTEX_LOCK(&mcc->mutex);
			(void) mccSqlStep(mcc, mcc->begin, MCC_SQL_BEGIN);
			PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

			/* a) or b). */
			if (0 < new_row.hits && mccGetKey(mcc, new_row.key_data, new_row.key_size, &old_row) == MCC_OK) {
				/* f) Ignore updates of the same generation. */
				if (old_row.created == new_row.created && old_row.hits == new_row.hits) {
					PTHREAD_MUTEX_LOCK(&mcc->mutex);
					(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
					PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

					if (0 < debug) {
						old_row.key_data[old_row.key_size] = '\0';
						syslog(LOG_DEBUG, "multi/unicast ignore " MCC_KEY_FMT, old_row.key_data);
					}
					continue;
				}

				/* c) & d) Broadcast older or more current local record. */
				if (old_row.created < new_row.created
				|| (old_row.created == new_row.created && new_row.hits < old_row.hits)) {
					PTHREAD_MUTEX_LOCK(&mcc->mutex);
					(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
					PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
					if (mccSend(mcc, &old_row, MCC_CMD_PUT) && 0 < debug) {
						old_row.key_data[old_row.key_size] = '\0';
						syslog(LOG_DEBUG, "multi/unicast broadcast correction " MCC_KEY_FMT, old_row.key_data);
					}
					continue;
				}

				/* e) */
			}

			if (mcc->hook.remote_replace != NULL
			&& (*mcc->hook.remote_replace)(mcc, mcc->hook.data, &old_row, &new_row)) {
				PTHREAD_MUTEX_LOCK(&mcc->mutex);
				(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
				PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
				continue;
			}

			if (mccPutRowLocal(mcc, &new_row, 0) != MCC_OK) {
				syslog(LOG_ERR, "multi/unicast put error " MCC_KEY_FMT, new_row.key_data);
				PTHREAD_MUTEX_LOCK(&mcc->mutex);
				(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
				PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
				continue;
			}

			PTHREAD_MUTEX_LOCK(&mcc->mutex);
			(void) mccSqlStep(mcc, mcc->commit, MCC_SQL_COMMIT);
			PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
			break;

		case MCC_CMD_REMOVE:
#ifdef OFF
			PTHREAD_MUTEX_LOCK(&mcc->mutex);
			(void) mccSqlStep(mcc, mcc->begin, MCC_SQL_BEGIN);
			PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
#endif

			if (mcc->hook.remote_remove != NULL
			&& (*mcc->hook.remote_remove)(mcc, mcc->hook.data, NULL, &new_row)) {
#ifdef OFF
				PTHREAD_MUTEX_LOCK(&mcc->mutex);
				(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
				PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
#endif
				continue;
			}

			if (mccDeleteKey(mcc, new_row.key_data, new_row.key_size) != MCC_OK) {
				syslog(LOG_ERR, "multi/unicast remove error " MCC_KEY_FMT, new_row.key_data);
#ifdef OFF
				PTHREAD_MUTEX_LOCK(&mcc->mutex);
				(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
				PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
#endif
				continue;
			}

#ifdef OFF
			PTHREAD_MUTEX_LOCK(&mcc->mutex);
			(void) mccSqlStep(mcc, mcc->commit, MCC_SQL_COMMIT);
			PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
#endif
			break;

		case MCC_CMD_OTHER:
			/* Look for a matching prefix. */
			for (hooks = (mcc_key_hook **) VectorBase(mcc->key_hooks); *hooks != NULL; hooks++) {
				hook = *hooks;

				if (hook->prefix_length <= new_row.key_size
				&& memcmp(new_row.key_data, hook->prefix, hook->prefix_length) == 0) {
					(*hook->process)(mcc, hook, ip, &new_row);
					break;
				}
			}
			break;

		default:
			syslog(LOG_ERR, "multi/unicast packet [%s] unknown command=%c", ip, new_row.command);
		}
	}

	syslog(LOG_INFO, "multi/unicast listener %s thread exit", listen_addr);
	PTHREAD_POP_FREE(1, listen_addr);
#ifdef __WIN32__
	pthread_exit(NULL);
#endif
	return NULL;
}

void
mccStopUnicast(mcc_handle *mcc)
{
	void *rv;
	SocketAddress **unicast;

	if (0 < debug)
		syslog(LOG_DEBUG, "unicast listener port=%d running=%d", mcc->unicast.port, mcc->unicast.is_running);

	/* Do we already have a running listener? */
	if (mcc->unicast.is_running) {
		/* Stop the listener thread... */
		mcc->unicast.is_running = 0;

		/* Wait for the thread to exit. */
		(void) pthread_cancel(mcc->unicast.thread);

		if (0 < debug)
			syslog(LOG_DEBUG, "waiting for unicast listener thread to terminate...");
		(void) pthread_join(mcc->unicast.thread, &rv);
	}

	if (mcc->unicast_ip != NULL) {
		mcc->unicast.is_running = 0;

		for (unicast = mcc->unicast_ip; *unicast != NULL; unicast++)
			free(*unicast);

		free(mcc->unicast_ip);
		mcc->unicast_ip = NULL;
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "closing unicast socket...");

	/* Now we can clean this up. */
	socketClose(mcc->unicast.socket);
	mcc->unicast.socket = NULL;

	if (0 < debug)
		syslog(LOG_DEBUG, "unicast listener port=%d stopped", mcc->unicast.port);
}

int
mccStartUnicast(mcc_handle *mcc, const char **unicast_ips, int port)
{
	int i, j, count;
	const char *this_host;
	SocketAddress *address;
	mcc_listener *thread_data;
	pthread_attr_t pthread_attr;

	if (mcc == NULL)
		goto error0;

	mccStopUnicast(mcc);
	if (unicast_ips == NULL)
		return MCC_OK;

	if ((thread_data = malloc(sizeof (*thread_data))) == NULL) {
		syslog(LOG_ERR, "memory error: %s, (%d)", strerror(errno), errno);
		goto error0;
	}

	/* These are the arguments for mcc_listener_thread(),
	 * which will be freed by the thread.
	 */
	thread_data->mcc = mcc;
	thread_data->listener = &mcc->unicast;

	mcc->unicast.port = port;
	mcc->unicast.is_running = 0;

	for (count = 0; unicast_ips[count] != NULL; count++)
		;

	if ((mcc->unicast_ip = calloc(count+1, sizeof (*mcc->unicast_ip))) == NULL)
		goto error1;

	for (i = j = 0; i < count; i++, j++) {
		mcc->unicast_ip[j] = socketAddressCreate(unicast_ips[i], port);

		/* Avoid broadcast-to-self by discarding our own IP. */
		if (socketAddressIsLocal(mcc->unicast_ip[j])) {
			syslog(LOG_WARN, "unicast address %s skipped", unicast_ips[i]);
			free(mcc->unicast_ip[j]);
			j--;
		}
	}

	if (count <= 0 || mcc->unicast_ip[0] == NULL) {
		syslog(LOG_ERR, "empty unicast address list");
		goto error2;
	}

	/* Assume that list of unicast addresses are all in the same family. */
	this_host = mcc->unicast_ip[0]->sa.sa_family == AF_INET ? "0.0.0.0" : "::0";

	if ((address = socketAddressCreate(this_host, port)) == NULL) {
		syslog(LOG_ERR, "unicast address error: %s, (%d)", strerror(errno), errno);
		goto error2;
	}

	if ((mcc->unicast.socket = socketOpen(address, 0)) == NULL) {
		syslog(LOG_ERR, "unicast socket error: %s, (%d)", strerror(errno), errno);
		goto error3;
	}

	if (socketSetNonBlocking(mcc->unicast.socket, 1)) {
		syslog(LOG_ERR, "unicast socketSetNonBlocking(true) failed: %s (%d)", strerror(errno), errno);
		goto error3;
	}

	if (socketSetReuse(mcc->unicast.socket, 1)) {
		syslog(LOG_ERR, "unicast socketSetReuse(true) failed: %s (%d)", strerror(errno), errno);
		goto error3;
	}

	if (socketBind(mcc->unicast.socket, address)) {
		syslog(LOG_ERR, "unicast socketBind() failed: %s (%d)", strerror(errno), errno);
		goto error3;
	}

#ifdef HAVE_PTHREAD_ATTR_INIT
	if (pthread_attr_init(&pthread_attr))
		goto error3;

#  if defined(HAVE_PTHREAD_ATTR_SETSCOPE)
	(void) pthread_attr_setscope(&pthread_attr, PTHREAD_SCOPE_SYSTEM);
#  endif
#  if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	(void) pthread_attr_setstacksize(&pthread_attr, MCC_STACK_SIZE);
#  endif
#endif
	i = pthread_create(&mcc->unicast.thread, &pthread_attr, mcc_listener_thread, thread_data);
#ifdef HAVE_PTHREAD_ATTR_DESTROY
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (i != 0) {
		syslog(LOG_ERR, "unicast listener thread error: %s, (%d)", strerror(errno), errno);
		goto error3;
	}

	free(address);

	return MCC_OK;
error3:
	free(address);
error2:
	mccStopUnicast(mcc);
error1:
	free(thread_data);
error0:
	return MCC_ERROR;
}

void
mccStopMulticast(mcc_handle *mcc)
{
	void *rv;

	if (0 < debug)
		syslog(LOG_DEBUG, "multicast listener port=%d running=%d", mcc->multicast.port, mcc->multicast.is_running);

	/* Do we already have a running listener? */
	if (mcc->multicast.is_running) {
		/* Stop the listener thread... */
		mcc->multicast.is_running = 0;

		/* ... by closing both I/O channels of the socket. */
		socketShutdown(mcc->multicast.socket, SHUT_RDWR);

		/* Wait for the thread to exit. */
		(void) pthread_cancel(mcc->multicast.thread);

		if (0 < debug)
			syslog(LOG_DEBUG, "waiting for multicast listener thread to terminate...");
		(void) pthread_join(mcc->multicast.thread, &rv);

		/* Now we can clean this up. */
		(void) socketMulticast(mcc->multicast.socket, mcc->multicast_ip, 0);
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "closing multicast socket...");
	socketClose(mcc->multicast.socket);
	mcc->multicast.socket = NULL;

	free(mcc->multicast_ip);
	mcc->multicast_ip = NULL;

	if (0 < debug)
		syslog(LOG_DEBUG, "multicast listener port=%d stopped", mcc->multicast.port);
}

int
mccStartMulticast(mcc_handle *mcc, const char *multicast_ip, int port)
{
	int rc;
	mcc_listener *thread_data;
	SocketAddress *any_address;
	pthread_attr_t pthread_attr;

	if (mcc == NULL)
		goto error0;

	mccStopMulticast(mcc);
	if (multicast_ip == NULL)
		return MCC_OK;

	if ((thread_data = malloc(sizeof (*thread_data))) == NULL) {
		syslog(LOG_ERR, "memory error: %s, (%d)", strerror(errno), errno);
		goto error0;
	}

	/* These are the arguments for mcc_listener_thread(),
	 * which will be freed by the thread.
	 */
	thread_data->mcc = mcc;
	thread_data->listener = &mcc->multicast;

	mcc->multicast.port = port;
	mcc->multicast.is_running = 0;

	if ((mcc->multicast_ip = socketAddressCreate(multicast_ip, port)) == NULL) {
		syslog(LOG_ERR, "multicast address error: %s, (%d)", strerror(errno), errno);
		goto error1;
	}

	if ((mcc->multicast.socket = socketOpen(mcc->multicast_ip, 0)) == NULL) {
		syslog(LOG_ERR, "multicast socket error: %s, (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketSetReuse(mcc->multicast.socket, 1)) {
		syslog(LOG_ERR, "multicast socketSetReuse(true) failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if ((any_address = socketAddressCreate(mcc->multicast_ip->sa.sa_family == AF_INET ? "0.0.0.0" : "::0", port)) == NULL) {
		syslog(LOG_ERR, "multicast address error: %s, (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketBind(mcc->multicast.socket, any_address)) {
		syslog(LOG_ERR, "multicast socketBind() failed: %s (%d)", strerror(errno), errno);
		free(any_address);
		goto error2;
	}

	free(any_address);

	if (socketMulticast(mcc->multicast.socket, mcc->multicast_ip, 1)) {
		syslog(LOG_ERR, "multicast socketMulticast() join group failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketMulticastLoopback(mcc->multicast.socket, 0)) {
		syslog(LOG_ERR, "multicast socketMulticastLoopback(false) failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

#ifdef HAVE_PTHREAD_ATTR_INIT
	if (pthread_attr_init(&pthread_attr))
		goto error2;

#  if defined(HAVE_PTHREAD_ATTR_SETSCOPE)
	(void) pthread_attr_setscope(&pthread_attr, PTHREAD_SCOPE_SYSTEM);
#  endif
#  if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	(void) pthread_attr_setstacksize(&pthread_attr, MCC_STACK_SIZE);
#  endif
#endif

	rc = pthread_create(&mcc->multicast.thread, &pthread_attr, mcc_listener_thread, thread_data);
#ifdef HAVE_PTHREAD_ATTR_DESTROY
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (rc != 0) {
		syslog(LOG_ERR, "multicast listener thread error: %s, (%d)", strerror(errno), errno);
		goto error2;
	}

	return MCC_OK;
error2:
	mccStopMulticast(mcc);
error1:
	free(thread_data);
error0:
	return MCC_ERROR;
}

int
mccGetKey(mcc_handle *mcc, const unsigned char *key, unsigned length, mcc_row *row)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || key == NULL || row == NULL || sizeof (row->key_data) <= length)
		goto error0;

	if (1 < debug)
		syslog(LOG_DEBUG, "mccGetKey " MCC_KEY_FMT, row->key_data);

	PTHREAD_MUTEX_LOCK(&mcc->mutex);

	if (sqlite3_bind_text(mcc->select_one, 1, (const char *) key, length, SQLITE_STATIC) != SQLITE_OK)
		goto error1;

	switch (mccSqlStep(mcc, mcc->select_one, MCC_SQL_SELECT_ONE)) {
	case SQLITE_DONE:
		rc = MCC_NOT_FOUND;
		break;
	case SQLITE_ROW:
		if (row->key_data != key)
			memcpy(row->key_data, key, length);
		row->key_size = (uint16_t) length;
		row->value_size = (uint8_t) sqlite3_column_bytes(mcc->select_one, 1);

		/* Make sure the value is not bigger than the value_data
		 * field, otherwise truncate the f.
		 */
		if (sizeof (row->value_data) < row->value_size)
			row->value_size = sizeof (row->value_data);

		memcpy(row->value_data, sqlite3_column_text(mcc->select_one, 1), row->value_size);
		row->hits = (uint32_t) sqlite3_column_int(mcc->select_one, 2);
		row->created = (uint32_t) sqlite3_column_int(mcc->select_one, 3);
		row->touched = (uint32_t) sqlite3_column_int(mcc->select_one, 4);
		row->expires = (uint32_t) sqlite3_column_int(mcc->select_one, 5);
		rc = MCC_OK;

		/* There should be only one row for the key so we
		 * can reset the statement's state machine.
		 */
		(void) sqlite3_reset(mcc->select_one);
	}
	(void) sqlite3_clear_bindings(mcc->select_one);
error1:
	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
error0:
	return rc;
}

int
mccGetRow(mcc_handle *mcc, mcc_row *row)
{
	return mccGetKey(mcc, (const unsigned char *) row->key_data, row->key_size, row);
}

int
mccPutRow(mcc_handle *mcc, mcc_row *row)
{
	int rc;

	if ((rc = mccPutRowLocal(mcc, row, 1)) == MCC_OK) {
		(void) mccSend(mcc, row, MCC_CMD_PUT);
	}

	return rc;
}

int
mccDeleteRowLocal(mcc_handle *mcc, mcc_row *row)
{
	return mccDeleteKey(mcc, row->key_data, row->key_size);
}

int
mccDeleteRow(mcc_handle *mcc, mcc_row *row)
{
	(void) mccSend(mcc, row, MCC_CMD_REMOVE);
	return mccDeleteRowLocal(mcc, row);
}

int
mccExpireRows(mcc_handle *mcc, time_t *when)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || when == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&mcc->mutex);

#ifdef OFF
	if (mccSqlStep(mcc, mcc->begin, MCC_SQL_BEGIN) != SQLITE_DONE)
		goto error1;
#endif
	if (mcc->hook.expire != NULL) {
		if ((*mcc->hook.expire)(mcc, mcc->hook.data)) {
#ifdef OFF
			(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
#endif
			goto error1;
		}
	}

	if (sqlite3_bind_int(mcc->expire, 1, (int)(uint32_t) *when) != SQLITE_OK) {
#ifdef OFF
		(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
#endif
		goto error1;
	}
	if (mccSqlStep(mcc, mcc->expire, MCC_SQL_EXPIRE) == SQLITE_DONE)
		rc = MCC_OK;
	(void) sqlite3_clear_bindings(mcc->expire);

#ifdef OFF
	(void) mccSqlStep(mcc, mcc->commit, MCC_SQL_COMMIT);
#endif
error1:
	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);
error0:
	return rc;
}

int
mccDeleteAll(mcc_handle *mcc)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL)
		return MCC_ERROR;

	PTHREAD_MUTEX_LOCK(&mcc->mutex);

	if (mccSqlStep(mcc, mcc->truncate, MCC_SQL_TRUNCATE) == SQLITE_DONE)
		rc = MCC_OK;

	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

	return rc;
}

int
mccSetSecret(mcc_handle *mcc, const char *secret)
{
	char *copy;

	if (mcc == NULL)
		return MCC_ERROR;

	if ((copy = strdup(secret)) == NULL)
		return MCC_ERROR;

	PTHREAD_MUTEX_LOCK(&mcc->mutex);

	free(mcc->secret);
	mcc->secret = copy;
	mcc->secret_length = strlen(copy);

	PTHREAD_MUTEX_UNLOCK(&mcc->mutex);

	return MCC_OK;
}

static void *
mcc_expire_thread(void *data)
{
	time_t now;
	mcc_handle *mcc = data;

	(void) pthread_detach(pthread_self());

	for (mcc->gc_next = time(NULL);	0 < mcc->ttl; ) {
		if (mcc->gc_next <= (now = time(NULL))) {
			(void) mccExpireRows(mcc, &now);
			mcc->gc_next = now + mcc->ttl;
		}
		pthreadSleep(mcc->gc_next - now, 0);
	}

#ifdef __WIN32__
	pthread_exit(NULL);
#endif
	return NULL;
}

void
mccStopGc(mcc_handle *mcc)
{
	if (mcc->gc_next != 0) {
		mcc->ttl = 0;
#if defined(HAVE_PTHREAD_CANCEL)
		(void) pthread_cancel(mcc->gc_thread);
#endif
	}
}

int
mccStartGc(mcc_handle *mcc, unsigned ttl)
{
	int rc;
	pthread_attr_t pthread_attr;

	if (mcc == NULL)
		return MCC_ERROR;

	mcc->ttl = ttl;

	if (ttl == 0 && mcc->gc_next != 0) {
		mccStopGc(mcc);
		return MCC_OK;
	}

#ifdef HAVE_PTHREAD_ATTR_INIT
	if (pthread_attr_init(&pthread_attr))
		goto error1;

# if defined(HAVE_PTHREAD_ATTR_SETSCOPE)
	(void) pthread_attr_setscope(&pthread_attr, PTHREAD_SCOPE_SYSTEM);
# endif
# if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	(void) pthread_attr_setstacksize(&pthread_attr, MCC_STACK_SIZE);
# endif
#endif
	rc = pthread_create(&mcc->gc_thread,  &pthread_attr, mcc_expire_thread, mcc);
#ifdef HAVE_PTHREAD_ATTR_DESTROY
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (rc != 0) {
error1:
		syslog(LOG_ERR, "multi/unicast cache timer thread error: %s, (%d)", strerror(errno), errno);
		mcc->gc_next = 0;
		mcc->ttl = 0;
		return MCC_ERROR;
	}

	return MCC_OK;
}

void
mccAtForkPrepare(mcc_handle *mcc)
{
	(void) pthread_mutex_lock(&mcc->mutex);
}

void
mccAtForkParent(mcc_handle *mcc)
{
	(void) pthread_mutex_unlock(&mcc->mutex);
}

void
mccAtForkChild(mcc_handle *mcc)
{
	(void) pthread_mutex_unlock(&mcc->mutex);
	(void) pthread_mutex_init(&mcc->mutex, NULL);

	mcc->multicast.is_running = 0;
	socketClose(mcc->multicast.socket);
	mcc->multicast.socket = NULL;

	mcc->unicast.is_running = 0;
	socketClose(mcc->unicast.socket);
	mcc->unicast.socket = NULL;
}

static void
mcc_key_hook_free(void *_hook)
{
	mcc_key_hook *hook = _hook;

	if (hook != NULL) {
		if (hook->cleanup != NULL)
			(*hook->cleanup)(hook->data);
	}
}

static void
mcc_active_cleanup(mcc_active_host *table)
{
	int i;

	for (i = 0; i < MCC_HASH_TABLE_SIZE; i++)
		mccNotesFree(table[i].notes);
}

void
mccDestroy(void *_mcc)
{
	mcc_handle *mcc = _mcc;

	if (mcc != NULL) {
		if (0 < debug)
			syslog(LOG_DEBUG, "mccDestroy(%lx)", (long) mcc);

		/* Stop these threads before releasing the rest. */
		mccStopMulticast(mcc);
		mccStopUnicast(mcc);
		mccStopGc(mcc);

		if (mcc->db != NULL) {
#ifdef HAVE_PTHREAD_SETCANCELSTATE
			int old_state;
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif
			mcc_sql_stmts_finalize(mcc);
			sqlite3_close(mcc->db);
#ifdef HAVE_PTHREAD_SETCANCELSTATE
			pthread_setcancelstate(old_state, NULL);
#endif
		}

		mcc_active_cleanup(mcc->active);
		VectorDestroy(mcc->key_hooks);
		free(mcc->secret);

		(void) pthread_mutex_destroy(&mcc->active_mutex);
		(void) pthread_mutex_destroy(&mcc->mutex);
		free(mcc);
	}
}

mcc_context *
mccCreate(const char *filepath, int flags, mcc_hooks *hooks)
{
	int rc;
	size_t length;
	mcc_context *mcc;

	length = strlen(filepath) + 1;
	if ((mcc = calloc(1, sizeof (*mcc) + length)) == NULL) {
		syslog(LOG_ERR, "%s(%u): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		goto error0;
	}

	if (mccSetSecret(mcc, "") == MCC_ERROR) {
		syslog(LOG_ERR, "%s(%u): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	if ((mcc->key_hooks = VectorCreate(5)) == NULL) {
		syslog(LOG_ERR, "%s(%u): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	VectorSetDestroyEntry(mcc->key_hooks, mcc_key_hook_free);

	mcc->flags = flags;
	mcc->path = (char *) &mcc[1];
	TextCopy(mcc->path, length, filepath);

	/* Copy the mcc_hooks structure to the mcc_context. Normally these
	 * would be defined after the mccCreate, but we may need to prepare
	 * some SQL statement with respect to the database when we create
	 * the mcc_context.
	 */
	if (hooks != NULL)
		mcc->hook = *hooks;

	if (pthread_mutex_init(&mcc->mutex, NULL)) {
		syslog(LOG_ERR, "%s(%u): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	if (pthread_mutex_init(&mcc->active_mutex, NULL)) {
		syslog(LOG_ERR, "%s(%u): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	if ((rc = sqlite3_open(mcc->path, &mcc->db)) != SQLITE_OK) {
		syslog(LOG_ERR, "multi/unicast cache sql=%s open error: %s", mcc->path, sqlite3_errmsg(mcc->db));
		goto error1;
	}

	/* Ignore errors if the table already exists. */
	(void) mcc_sql_create(mcc);

	if (mcc_sql_stmts_prepare(mcc))
		goto error1;

	return mcc;
error1:
	mccDestroy(mcc);
error0:
	return NULL;
}

#ifdef TEST
# include <stdio.h>
/***********************************************************************
 *** mcc CLI
 ***********************************************************************/

# ifdef __sun__
#  define _POSIX_PTHREAD_SEMANTICS
# endif
# include <signal.h>

# include <com/snert/lib/util/Text.h>
# include <com/snert/lib/util/getopt.h>

static const char usage_opt[] = "g:m:M:s:t:vu:U:";

static char usage[] =
"usage: mcc [-v]"
"[-g seconds][-m ip[:port]][-s secret][-t seconds]\n"
"           [-u list][-U port] database.sq3\n"
"\n"
"-g seconds\tGC thread interval\n"
"-m ip[:port]\tthe multicast IP group & port number\n"
"-M port\t\tmulticast listener port (default 6920)\n"
"-s secret\tmutlicast cache shared secret\n"
"-t seconds\tcache time-to-live in seconds for each record\n"
"-u list\t\tcomma separated list of unicast hosts & optional port number\n"
"-U port\t\tunicast listener port (default 6921)\n"
"-v\t\tverbose logging to the user log\n"
"\n"
"Standard input are commands of the form:\n"
"\n"
"GET key\n"
"PUT key value\n"
"REMOVE key\n"
"QUIT\n"
"\n"
"Note that a key cannot contain whitespace, while the value may.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

/* 232.173.190.239 , FF02::DEAD:BEEF */

static unsigned ttl;
static unsigned gc_ttl;
static char *secret;
static char *group;

static Vector unicast_list = NULL;
static long unicast_listener_port = MCC_UNICAST_PORT;
static long multicast_listener_port = MCC_MULTICAST_PORT;

void
signalExit(int signum)
{
	signal(signum, SIG_IGN);
	exit(0);
}

void
signalThreadExit(int signum)
{
	pthread_exit(NULL);
}

#if ! defined(__MINGW32__)
void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}
#endif

int
main(int argc, char **argv)
{
	char *arg;
	long length;
	mcc_context *mcc;
	int ch, rc, span;
	unsigned long lineno;
	static char buffer[1024];
	mcc_row old_row, new_row;

	rc = EXIT_FAILURE;
	setvbuf(stdin, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);
	openlog("mcc", LOG_PID, LOG_USER);

	while ((ch = getopt(argc, argv, usage_opt)) != -1) {
		switch (ch) {
		case 'g':
			gc_ttl = (unsigned) strtol(optarg, NULL, 10);
			break;
		case 'm':
			group = optarg;;
			break;
		case 'M':
			multicast_listener_port = strtol(optarg, NULL, 10);
			break;
		case 's':
			secret = optarg;
			break;
		case 't':
			ttl = (unsigned) strtol(optarg, NULL, 10);
			break;
		case 'u':
			if ((unicast_list = TextSplit(optarg, ",", 0)) == NULL) {
				syslog(LOG_ERR, "memory error: %s (%d)", strerror(errno), errno);
				exit(71);
			}
			break;
		case 'U':
			unicast_listener_port = strtol(optarg, NULL, 10);
			break;
		case 'v':
			++debug;
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(64);
		}
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, usage);
		exit(64);
	}

	if (0 < debug) {
		LogSetProgramName("mcc");
		LogOpen("(standard error)");
		LogSetLevel(LOG_DEBUG);
		socketSetDebug(1);
	}

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() %s (%d)", strerror(errno), errno);
		exit(71);
	}

#ifdef SIGUSR1
	if (signal(SIGUSR1, signalThreadExit) == SIG_ERR) {
		syslog(LOG_ERR, "SIGUSR1 error: %s (%d)", strerror(errno), errno);
		exit(1);
	}
#endif
	signal(SIGTERM, signalExit);
	signal(SIGINT, signalExit);

	if ((mcc = mccCreate(argv[optind], 0, NULL)) == NULL)
		exit(71);

	if (0 < gc_ttl)
		mccStartGc(mcc, gc_ttl);
	if (secret != NULL)
		mccSetSecret(mcc, secret);
	if (group != NULL && mccStartMulticast(mcc, group, multicast_listener_port) == MCC_ERROR)
		goto error0;

	if (unicast_list != NULL && mccStartUnicast(mcc, (const char **) VectorBase(unicast_list), unicast_listener_port) == MCC_ERROR)
		goto error0;

#ifdef HAVE_PTHREAD_YIELD
	pthread_yield();
#endif
	syslog(LOG_INFO, "mcc " LIBSNERT_COPYRIGHT);

	rc = EXIT_SUCCESS;

	for (lineno = 1; 0 <= (length = TextInputLine(stdin, buffer, sizeof (buffer))); lineno++) {
		if (length == 0 || buffer[0] == '#')
			continue;
		if (0 < debug)
			syslog(LOG_DEBUG, "input=[%s]", buffer);
		if (buffer[0] == '.' && buffer[1] == '\0')
			break;

		/* First word is the command: GET, PUT, REMOVE, DELETE */
		span = strcspn(buffer, " \t");
		buffer[span++] = '\0';
		span += strspn(buffer+span, " \t");

		/* Second word is the key. */
		arg = buffer+span;
		span = strcspn(arg, " \t");
		arg[span++] = '\0';
		span += strspn(arg+span, " \t");
		new_row.key_size = TextCopy((char *) new_row.key_data, sizeof (new_row.key_data), arg);

		/* Optional remainder of input is the value. */
		arg = arg+span;
		if (*arg != '\0') {
			span = strcspn(arg, "\r\n");
			arg[span] = '\0';
			new_row.value_size = TextCopy((char *) new_row.value_data, sizeof (new_row.value_data), arg);
		}

		switch (tolower(*buffer)) {
		case 'q':
			goto break_input_loop;

		case 'g': case 'p': case 'r': case 'd':
			switch (mccGetKey(mcc, new_row.key_data, new_row.key_size, &old_row)) {
			case MCC_OK:
				old_row.key_data[old_row.key_size] = '\0';
				old_row.value_data[old_row.value_size] = '\0';
				printf("old key=\"%s\" value=\"%s\" hits=%u created=0x%lx\n", old_row.key_data, old_row.value_data, old_row.hits, (long) old_row.created);
				fflush(stdout);
				break;
			case MCC_ERROR:
				printf("GET error\n");
				fflush(stdout);
				continue;
			case MCC_NOT_FOUND:
				if (tolower(*buffer) == 'g') {
					printf("key=\"%s\" not found\n", new_row.key_data);
					fflush(stdout);
					continue;
				}

				old_row.hits = 0;
				old_row.created = (uint32_t) time(NULL);
				old_row.touched = old_row.created;
			}
		}

		switch (tolower(*buffer)) {
		case 'g':
			break;

		case 'p':
			new_row.hits = old_row.hits;
			new_row.created = old_row.created;
			new_row.touched = old_row.touched;
			new_row.expires = new_row.touched + ttl;

			switch (mccPutRow(mcc, &new_row)) {
			case MCC_OK:
				printf("new key=\"%s\" value=\"%s\" hits=%u created=0x%lx\n", new_row.key_data, new_row.value_data, new_row.hits, (long) new_row.created);
				fflush(stdout);
				break;
			case MCC_ERROR:
				printf("PUT error\n");
				fflush(stdout);
				break;
			}
			break;

		case 'r': case 'd':
			switch (mccDeleteRow(mcc, &new_row)) {
			case MCC_OK:
				printf("deleted key=\"%s\"\n", new_row.key_data);
				fflush(stdout);
				break;
			case MCC_ERROR:
				printf("DELETE error\n");
				fflush(stdout);
				break;
			}
			break;

		default:
			printf("input error\n");
			fflush(stdout);
		}
	}
break_input_loop:
error0:
	VectorDestroy(unicast_list);
	mccDestroy(mcc);

	return rc;
}
#endif /* TEST */

#elif !defined(HAVE_SQLITE3_H) && defined(TEST)

#include <stdio.h>

int
main(int argc, char **argv)
{
	printf("This program requires threaded SQLite3 support.\n");
	return EXIT_FAILURE;
}

#endif /* !defined(HAVE_SQLITE3_H) && defined(TEST) */

/***********************************************************************
 *** END
 ***********************************************************************/

