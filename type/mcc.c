/*
 * mcc.c
 *
 * Multicast Cache
 *
 * Copyright 2006, 2010 by Anthony Howe. All rights reserved.
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

#ifndef MCC_SQLITE_BUSY_MS
#define MCC_SQLITE_BUSY_MS	6000
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

/***********************************************************************
 ***
 ***********************************************************************/

static int debug;
static mcc_data cache;
static const char log_error[] = "%s(%u): %s (%d)";

#define MCC_KEY_FMT	" key={%." MCC_MAX_KEY_SIZE_S "s}"
#define MCC_VALUE_FMT	" value={%." MCC_MAX_VALUE_SIZE_S "s}"

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
mccNotesUpdate(const char *ip, const char *find, const char *text)
{
	mcc_string *note;
	mcc_active_host *host;

	if (ip == NULL || find == NULL || text == NULL)
		return;

	PTHREAD_MUTEX_LOCK(&cache.active_mutex);

	host = mccFindActive(ip);
	note = mccNotesFind(host->notes, find);

	if (note == NULL) {
		if ((note = mccStringCreate(text)) != NULL) {
			note->next = host->notes;
			host->notes = note;
		}
	} else {
		mccStringReplace(note, text);
	}

	PTHREAD_MUTEX_UNLOCK(&cache.active_mutex);
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
mccFindActive(const char *ip)
{
	unsigned i;
	size_t ip_len;
	unsigned long hash;
	mcc_active_host *entry, *oldest;

	ip_len = strlen(ip);
	hash = djb_hash_index((const unsigned char *) ip, ip_len, MCC_HASH_TABLE_SIZE);
	oldest = &cache.active[hash];

	for (i = 0; i < MCC_MAX_LINEAR_PROBE; i++) {
		entry = &cache.active[(hash + i) & (MCC_HASH_TABLE_SIZE-1)];

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
mccUpdateActive(const char *ip, uint32_t *touched)
{
	unsigned long rate;
	mcc_active_host *entry;

	if (ip == NULL)
		return;

	PTHREAD_MUTEX_LOCK(&cache.active_mutex);

	entry = mccFindActive(ip);
	entry->touched = *touched;

	rate = mccUpdateRate(entry->intervals, *touched / MCC_TICK);
	if (entry->max_ppm < rate)
		entry->max_ppm = rate;

	if (0 < debug)
		syslog(LOG_DEBUG, "multi/unicast cache active ip=%s ppm=%lu max-ppm=%lu", ip, rate, entry->max_ppm);

	PTHREAD_MUTEX_UNLOCK(&cache.active_mutex);
}

Vector
mccGetActive(void)
{
	unsigned i;
	Vector hosts;
	mcc_active_host *entry;

	if ((hosts = VectorCreate(10)) == NULL)
		return NULL;

	VectorSetDestroyEntry(hosts, free);

	PTHREAD_MUTEX_LOCK(&cache.active_mutex);

	for (i = 0; i < sizeof (cache.active) / sizeof (mcc_active_host); i++) {
		if (cache.active[i].touched == 0)
			continue;

		if ((entry = malloc(sizeof (*entry))) != NULL) {
			*entry = cache.active[i];
			if (VectorAdd(hosts, entry))
				free(entry);
		}
	}

	PTHREAD_MUTEX_UNLOCK(&cache.active_mutex);

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
mccSetMulticastTTL(int ttl)
{
	if (socketMulticastTTL(cache.multicast.socket, ttl) == SOCKET_ERROR)
		return -1;

	return 0;
}

int
mccRegisterKey(mcc_key_hook *tag_hook)
{
	return VectorAdd(cache.key_hooks, tag_hook);
}

int
mccSend(mcc_handle *mcc, mcc_row *row, uint8_t command)
{
	int rc;
	md5_state_t md5;
	SocketAddress **unicast;

	if (!cache.multicast.is_running && !cache.unicast.is_running)
		return MCC_OK;

	row->command = command;

	mccUpdateActive("127.0.0.1", &row->touched);

	/* Covert some of the values to network byte order. */
	row->hits = htonl(row->hits);
	row->created = htonl(row->created);
	row->touched = htonl(row->touched);
	row->expires = htonl(row->expires);

	md5_init(&md5);
	md5_append(&md5, (md5_byte_t *) row+sizeof (row->digest), sizeof (*row)-sizeof (row->digest));
	md5_append(&md5, (md5_byte_t *) cache.secret, cache.secret_length);
	md5_finish(&md5, (md5_byte_t *) row->digest);

	rc = MCC_OK;

	if (cache.multicast.socket != NULL) {
		if (1 < debug)
			syslog(LOG_DEBUG, "mccSend multicast command=%c" MCC_KEY_FMT, row->command, row->key_data);
		PTHREAD_MUTEX_LOCK(&cache.mutex);
		if (socketWriteTo(cache.multicast.socket, (unsigned char *) row, sizeof (*row), cache.multicast_ip) != sizeof (*row))
			rc = MCC_ERROR;
		PTHREAD_MUTEX_UNLOCK(&cache.mutex);
	}

	if (cache.unicast.socket != NULL) {
		if (1 < debug)
			syslog(LOG_DEBUG, "mccSend unicast command=%c" MCC_KEY_FMT, row->command, row->key_data);
		PTHREAD_MUTEX_LOCK(&cache.mutex);
		for (unicast = cache.unicast_ip; *unicast != NULL; unicast++) {
			if (socketWriteTo(cache.unicast.socket, (unsigned char *) row, sizeof (*row), *unicast) != sizeof (*row))
				rc = MCC_ERROR;
		}
		PTHREAD_MUTEX_UNLOCK(&cache.mutex);
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
mcc_sql_create(mcc_handle *mcc)
{
	int count;
	char *error;

	count = 0;
	if (sqlite3_exec(mcc->db, MCC_SQL_TABLE_EXISTS, mcc_sql_count, &count, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc \"%s\" error %s: %s", cache.path, MCC_SQL_TABLE_EXISTS, error);
		sqlite3_free(error);
		return MCC_ERROR;
	}

	if (count != 1 && sqlite3_exec(mcc->db, MCC_SQL_CREATE_TABLE, NULL, NULL, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc \"%s\" error %s: %s", cache.path, MCC_SQL_CREATE_TABLE, error);
		sqlite3_free(error);
		return MCC_ERROR;
	}

	count = 0;
	if (sqlite3_exec(mcc->db, MCC_SQL_INDEX_EXISTS, mcc_sql_count, &count, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc \"%s\" error %s: %s", cache.path, MCC_SQL_INDEX_EXISTS, error);
		sqlite3_free(error);
		return MCC_ERROR;
	}

	if (count != 1 && sqlite3_exec(mcc->db, MCC_SQL_CREATE_INDEX, NULL, NULL, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc \"%s\" error %s: %s", cache.path, MCC_SQL_CREATE_INDEX, error);
		sqlite3_free(error);
		return MCC_ERROR;
	}

	return MCC_OK;
}

static int
mcc_sql_stmts_prepare(mcc_handle *mcc)
{
	if (cache.hook.prepare != NULL && (*cache.hook.prepare)(mcc, NULL)) {
		syslog(LOG_ERR, "mcc prepare hook failed");
		return -1;
	}

	/* Using the newer sqlite_prepare_v2() interface will
	 * handle SQLITE_SCHEMA errors automatically.
	 */
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_SELECT_ONE, -1, &mcc->select_one, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_SELECT_ONE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_REPLACE, -1, &mcc->replace, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_REPLACE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_TRUNCATE, -1, &mcc->truncate, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_TRUNCATE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_EXPIRE, -1, &mcc->expire, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_EXPIRE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_DELETE, -1, &mcc->remove, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_DELETE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_BEGIN, -1, &mcc->begin, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_BEGIN, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_COMMIT, -1, &mcc->commit, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_COMMIT, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2(mcc->db, MCC_SQL_ROLLBACK, -1, &mcc->rollback, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_ROLLBACK, sqlite3_errmsg(mcc->db));
		return -1;
	}

	if (mccSetSync(mcc, MCC_SYNC_OFF) == MCC_ERROR) {
		syslog(LOG_ERR, "mcc error %s: %s", synchronous_stmt[MCC_SYNC_OFF], sqlite3_errmsg(mcc->db));
		return -1;
	}

	return 0;
}

static void
mcc_sql_stmts_finalize(mcc_handle *mcc)
{
	if (cache.hook.finalize != NULL)
		(void) (*cache.hook.finalize)(mcc, NULL);

	if (mcc->select_one != NULL) {
		(void) sqlite3_finalize(mcc->select_one);
	}
	if (mcc->replace != NULL) {
		(void) sqlite3_finalize(mcc->replace);
	}
	if (mcc->truncate != NULL) {
		(void) sqlite3_finalize(mcc->truncate);
	}
	if (mcc->expire != NULL) {
		(void) sqlite3_finalize(mcc->expire);
	}
	if (mcc->remove != NULL) {
		(void) sqlite3_finalize(mcc->remove);
	}
	if (mcc->begin != NULL) {
		(void) sqlite3_finalize(mcc->begin);
	}
	if (mcc->commit != NULL) {
		(void) sqlite3_finalize(mcc->commit);
	}
	if (mcc->rollback != NULL) {
		(void) sqlite3_finalize(mcc->rollback);
	}
}

#ifdef NO_LONGER_USED
static int
mcc_sql_recreate(mcc_handle *mcc)
{
	int rc;

	syslog(LOG_ERR, "closing corrupted sqlite db %s...", cache.path);
	mcc_sql_stmts_finalize(mcc);
	sqlite3_close(mcc->db);

	if (on_corrupt == MCC_ON_CORRUPT_RENAME) {
		char *new_name = strdup(cache.path);
		if (new_name == NULL) {
			syslog(LOG_ERR, "mcc_sql_recreate: (%d) %s", errno, strerror(errno));
			exit(1);
		}
		new_name[strlen(new_name)-1] = 'X';
		(void) unlink(new_name);
		if (rename(cache.path, new_name)) {
			syslog(LOG_ERR, "sql=%s rename to %s error: (%d) %s", cache.path, new_name, errno, strerror(errno));
			free(new_name);
			exit(1);
		}
		free(new_name);
	} else if (on_corrupt == MCC_ON_CORRUPT_REPLACE) {
		if (unlink(cache.path)) {
			syslog(LOG_ERR, "sql=%s unlink error: (%d) %s", cache.path, errno, strerror(errno));
			goto error0;
		}
	}

	syslog(LOG_INFO, "creating new sqlite db %s...", cache.path);
	if ((rc = sqlite3_open(cache.path, &mcc->db)) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s open error: %s", cache.path, sqlite3_errmsg(mcc->db));
		goto error0;
	}

	if (mcc_sql_create(mcc))
		goto error1;

	if (mcc_sql_stmts_prepare(mcc))
		goto error2;

	syslog(LOG_INFO, "sqlite db %s ready", cache.path);

	return 0;
error2:
	mcc_sql_stmts_finalize(mcc);
error1:
	sqlite3_close(mcc->db);
error0:
	return -1;
}
#endif

int
mccSqlStep(mcc_handle *mcc, sqlite3_stmt *sql_stmt, const char *sql_stmt_text)
{
	int rc;

	PTHREAD_DISABLE_CANCEL();

	if (sql_stmt == mcc->commit || sql_stmt == mcc->rollback)
		mcc->is_transaction = 0;

	(void) sqlite3_busy_timeout(mcc->db, MCC_SQLITE_BUSY_MS);

	while ((rc = sqlite3_step(sql_stmt)) == SQLITE_BUSY && !mcc->is_transaction) {
		(void) sqlite3_reset(sql_stmt);
		sleep(1);
	}

	if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
		syslog(LOG_ERR, "mcc \"%s\" step error (%d): %s: %s", cache.path, rc, sqlite3_errmsg(mcc->db), TextEmpty(sqlite3_sql(sql_stmt)));

		if (rc == SQLITE_CORRUPT || rc == SQLITE_CANTOPEN) {
			if (on_corrupt == MCC_ON_CORRUPT_EXIT)
				abort();
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

	PTHREAD_RESTORE_CANCEL();

	return rc;
}

int
mccDeleteKey(mcc_handle *mcc, const unsigned char *key, unsigned length)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || key == NULL)
		goto error0;

	if (sqlite3_bind_text(mcc->remove, 1, (const char *) key, length, SQLITE_STATIC) != SQLITE_OK)
		goto error0;
	if (mccSqlStep(mcc, mcc->remove, MCC_SQL_DELETE) == SQLITE_DONE)
		rc = MCC_OK;
	(void) sqlite3_clear_bindings(mcc->remove);
error0:
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

	rc = MCC_ERROR;

	if (mcc == NULL || level < MCC_SYNC_OFF || MCC_SYNC_FULL < level)
		return MCC_ERROR;

	PTHREAD_DISABLE_CANCEL();
	if (sqlite3_exec(mcc->db, synchronous_stmt[level], NULL, NULL, &error) == SQLITE_OK) {
		rc = MCC_OK;
	} else {
		syslog(LOG_ERR, "sql=%s error %s: %s", cache.path, synchronous_stmt[level], error);
		sqlite3_free(error);
	}
	PTHREAD_RESTORE_CANCEL();

	return rc;
}

int
mccPutRowLocal(mcc_handle *mcc, mcc_row *row, int touch)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || row == NULL)
		goto error0;

	if (touch) {
		row->hits++;
		row->touched = (uint32_t) time(NULL);
	}

	if (1 < debug)
		syslog(LOG_DEBUG, "mccPutRowLocal" MCC_KEY_FMT MCC_VALUE_FMT, row->key_data, row->value_data);

	if (sqlite3_bind_text(mcc->replace, 1, (const char *) row->key_data, row->key_size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error0;

	if (sqlite3_bind_text(mcc->replace, 2, (const char *) row->value_data, row->value_size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_int(mcc->replace, 3, (int) row->hits) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_int(mcc->replace, 4, (int) row->created) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_int(mcc->replace, 5, (int) row->touched) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_int(mcc->replace, 6, (int) row->expires) != SQLITE_OK)
		goto error1;

	if (mccSqlStep(mcc, mcc->replace, MCC_SQL_REPLACE) == SQLITE_DONE)
		rc = MCC_OK;
error1:
	(void) sqlite3_clear_bindings(mcc->replace);
error0:
	return rc;
}

/*
 * This function is generic for both UDP unicast and multicast.
 */
static void *
mcc_listener_thread(void *data)
{
	int err;
	long nbytes;
	md5_state_t md5;
	mcc_handle *mcc;
	SocketAddress from;
	mcc_network *listener;
	mcc_row new_row, old_row;
	mcc_key_hook **hooks, *hook;
	unsigned char our_digest[16];
	char ip[IPV6_STRING_LENGTH], *listen_addr, *cast_name;

	if ((mcc = mccCreate()) == NULL)
		goto error0;
	pthread_cleanup_push(mccDestroy, mcc);

	listener = (mcc_network *) data;
	cast_name = (&cache.unicast == listener) ? "unicast" : "multicast";

	listen_addr = socketAddressToString(&listener->socket->address);
	pthread_cleanup_push(free, listen_addr);

	syslog(LOG_INFO, "started %s listener %s", cast_name, listen_addr);

	for (listener->is_running = 1; listener->is_running; ) {
		if (!socketHasInput(listener->socket, MCC_LISTENER_TIMEOUT)) {
			if (1 < debug)
				syslog(LOG_DEBUG, "%s socket timeout: %s (%d)", cast_name, strerror(errno), errno);
			continue;
		}

		PTHREAD_MUTEX_LOCK(&cache.mutex);
		nbytes = socketReadFrom(listener->socket, (unsigned char *) &new_row, sizeof (new_row), &from);
		PTHREAD_MUTEX_UNLOCK(&cache.mutex);

		if (nbytes <= 0) {
			syslog(LOG_ERR, "%s socket read error: %s (%d)", cast_name, strerror(errno), errno);
			continue;
		}

		(void) socketAddressGetString(&from, 0, ip, sizeof (ip));

		if (1 < debug)
			syslog(LOG_DEBUG, "%s listener=%s from=%s cmd=%c", cast_name, listen_addr, ip, new_row.command);

		md5_init(&md5);
		md5_append(&md5, (md5_byte_t *) &new_row + sizeof (new_row.digest), sizeof (new_row)-sizeof (new_row.digest));
		md5_append(&md5, (md5_byte_t *) cache.secret, cache.secret_length);
		md5_finish(&md5, (md5_byte_t *) our_digest);

		new_row.hits = ntohl(new_row.hits);
		new_row.created = ntohl(new_row.created);
		new_row.touched = ntohl(new_row.touched);
		new_row.expires = ntohl(new_row.expires);

		mccUpdateActive(ip, &new_row.touched);

		if (memcmp(our_digest, new_row.digest, sizeof (our_digest)) != 0) {
			syslog(LOG_ERR, "%s cache digest error from [%s]", cast_name, ip);
			mccNotesUpdate(ip, "md5=", "md5=N");
			continue;
		}

		mccNotesUpdate(ip, "md5=", "md5=Y");

		if (1 < debug) {
			if (new_row.key_size < MCC_MAX_KEY_SIZE)
				new_row.key_data[new_row.key_size] = '\0';
			syslog(LOG_DEBUG, "%s cache packet [%s] command=%c" MCC_KEY_FMT, cast_name, ip, new_row.command, new_row.key_data);
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
#define MCC_CMD_PUT_TRANSACTION
#ifdef MCC_CMD_PUT_TRANSACTION
			err = mccSqlStep(mcc, mcc->begin, MCC_SQL_BEGIN);
			if (err != SQLITE_DONE)
				continue;
#endif
			/* a) or b). */
			if (0 < new_row.hits && mccGetKey(mcc, new_row.key_data, new_row.key_size, &old_row) == MCC_OK) {
				/* f) Ignore updates of the same generation. */
				if (old_row.created == new_row.created && old_row.hits == new_row.hits) {
#ifdef MCC_CMD_PUT_TRANSACTION
					(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
#endif
					if (0 < debug) {
						old_row.key_data[old_row.key_size] = '\0';
						syslog(LOG_DEBUG, "%s ignore" MCC_KEY_FMT, cast_name, old_row.key_data);
					}
					continue;
				}

				/* c) & d) Broadcast older or more current local record. */
				if (old_row.created < new_row.created
				|| (old_row.created == new_row.created && new_row.hits < old_row.hits)) {
#ifdef MCC_CMD_PUT_TRANSACTION
					(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
#endif
					if (mccSend(mcc, &old_row, MCC_CMD_PUT) && 0 < debug) {
						old_row.key_data[old_row.key_size] = '\0';
						syslog(LOG_DEBUG, "%s broadcast correction" MCC_KEY_FMT, cast_name, old_row.key_data);
					}
					continue;
				}

				/* e) */
			}

			if (cache.hook.remote_replace != NULL
			&& (*cache.hook.remote_replace)(mcc, NULL, &old_row, &new_row)) {
#ifdef MCC_CMD_PUT_TRANSACTION
				(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
#endif
				continue;
			}

			if (mccPutRowLocal(mcc, &new_row, 0) != MCC_OK) {
				syslog(LOG_ERR, "%s put error" MCC_KEY_FMT, cast_name, new_row.key_data);
#ifdef MCC_CMD_PUT_TRANSACTION
				(void) mccSqlStep(mcc, mcc->rollback, MCC_SQL_ROLLBACK);
#endif
				continue;
			}

#ifdef MCC_CMD_PUT_TRANSACTION
			(void) mccSqlStep(mcc, mcc->commit, MCC_SQL_COMMIT);
#endif
			break;

		case MCC_CMD_REMOVE:
			if (cache.hook.remote_remove != NULL
			&& (*cache.hook.remote_remove)(mcc, NULL, NULL, &new_row)) {
				continue;
			}
			if (mccDeleteKey(mcc, new_row.key_data, new_row.key_size) != MCC_OK) {
				syslog(LOG_ERR, "%s remove error" MCC_KEY_FMT, cast_name, new_row.key_data);
				continue;
			}
			break;

		case MCC_CMD_OTHER:
			/* Look for a matching prefix.
			 *
			 * NOTE that mcc->mutex is NOT locked around the loop
			 * as the key_hooks that are called will manipulate
			 * the cache using the MCC API which lock mcc->mutex
			 * themselves.
			 */
			for (hooks = (mcc_key_hook **) VectorBase(cache.key_hooks); *hooks != NULL; hooks++) {
				hook = *hooks;

				if (hook->prefix_length <= new_row.key_size
				&& memcmp(new_row.key_data, hook->prefix, hook->prefix_length) == 0) {
					(*hook->process)(mcc, hook, ip, &new_row);
					break;
				}
			}
			break;

		default:
			syslog(LOG_ERR, "%s packet [%s] unknown command=%c", cast_name, ip, new_row.command);
		}
	}

	syslog(LOG_INFO, "%s listener %s thread exit", cast_name, listen_addr);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
error0:
	PTHREAD_END(NULL);
}

void
mccStopUnicast(void)
{
	void *rv;
	SocketAddress **unicast;

	if (0 < debug)
		syslog(LOG_DEBUG, "unicast listener port=%d running=%d", cache.unicast.port, cache.unicast.is_running);

	/* Do we already have a running listener? */
	if (cache.unicast.is_running) {
		/* Stop the listener thread... */
		cache.unicast.is_running = 0;

		/* Wait for the thread to exit. */
		(void) pthread_cancel(cache.unicast.thread);

		if (0 < debug)
			syslog(LOG_DEBUG, "waiting for unicast listener thread to terminate...");
		(void) pthread_join(cache.unicast.thread, &rv);
	}

	if (cache.unicast_ip != NULL) {
		cache.unicast.is_running = 0;

		for (unicast = cache.unicast_ip; *unicast != NULL; unicast++)
			free(*unicast);

		free(cache.unicast_ip);
		cache.unicast_ip = NULL;
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "closing unicast socket...");

	/* Now we can clean this up. */
	socketClose(cache.unicast.socket);
	cache.unicast.socket = NULL;

	if (0 < debug)
		syslog(LOG_DEBUG, "unicast listener port=%d stopped", cache.unicast.port);
}

int
mccStartUnicast(const char **unicast_ips, int port)
{
	int i, j, count;
	const char *this_host;
	SocketAddress *address;
	pthread_attr_t pthread_attr;

	mccStopUnicast();
	if (unicast_ips == NULL)
		return MCC_OK;

	cache.unicast.port = port;
	cache.unicast.is_running = 0;

	for (count = 0; unicast_ips[count] != NULL; count++)
		;

	if ((cache.unicast_ip = calloc(count+1, sizeof (*cache.unicast_ip))) == NULL)
		goto error0;

	for (i = j = 0; i < count; i++, j++) {
		cache.unicast_ip[j] = socketAddressCreate(unicast_ips[i], port);

		/* Avoid broadcast-to-self by discarding our own IP. */
		if (socketAddressIsLocal(cache.unicast_ip[j])) {
			syslog(LOG_WARN, "unicast address %s skipped", unicast_ips[i]);
			free(cache.unicast_ip[j]);
			j--;
		}
	}

	if (count <= 0 || cache.unicast_ip[0] == NULL) {
		syslog(LOG_ERR, "empty unicast address list");
		goto error1;
	}

	/* Assume that list of unicast addresses are all in the same family. */
	this_host = cache.unicast_ip[0]->sa.sa_family == AF_INET ? "0.0.0.0" : "::0";

	if ((address = socketAddressCreate(this_host, port)) == NULL) {
		syslog(LOG_ERR, "unicast address error: %s, (%d)", strerror(errno), errno);
		goto error1;
	}

	if ((cache.unicast.socket = socketOpen(address, 0)) == NULL) {
		syslog(LOG_ERR, "unicast socket error: %s, (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketSetNonBlocking(cache.unicast.socket, 1)) {
		syslog(LOG_ERR, "unicast socketSetNonBlocking(true) failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketSetReuse(cache.unicast.socket, 1)) {
		syslog(LOG_ERR, "unicast socketSetReuse(true) failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketBind(cache.unicast.socket, address)) {
		syslog(LOG_ERR, "unicast socketBind() failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

#ifdef HAVE_PTHREAD_ATTR_INIT
	if (pthread_attr_init(&pthread_attr))
		goto error2;

# if defined(HAVE_PTHREAD_ATTR_SETSCOPE)
	(void) pthread_attr_setscope(&pthread_attr, PTHREAD_SCOPE_SYSTEM);
# endif
# if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	(void) pthread_attr_setstacksize(&pthread_attr, MCC_STACK_SIZE);
# endif
#endif
	i = pthread_create(&cache.unicast.thread, &pthread_attr, mcc_listener_thread, &cache.unicast);
#ifdef HAVE_PTHREAD_ATTR_INIT
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (i != 0) {
		syslog(LOG_ERR, "unicast listener thread error: %s, (%d)", strerror(errno), errno);
		goto error2;
	}

	free(address);

	return MCC_OK;
error2:
	free(address);
error1:
	mccStopUnicast();
error0:
	return MCC_ERROR;
}

void
mccStopMulticast(void)
{
	void *rv;

	if (0 < debug)
		syslog(LOG_DEBUG, "multicast listener port=%d running=%d", cache.multicast.port, cache.multicast.is_running);

	/* Do we already have a running listener? */
	if (cache.multicast.is_running) {
		/* Stop the listener thread... */
		cache.multicast.is_running = 0;

		/* ... by closing both I/O channels of the socket. */
		socketShutdown(cache.multicast.socket, SHUT_RDWR);

		/* Wait for the thread to exit. */
		(void) pthread_cancel(cache.multicast.thread);

		if (0 < debug)
			syslog(LOG_DEBUG, "waiting for multicast listener thread to terminate...");
		(void) pthread_join(cache.multicast.thread, &rv);

		/* Now we can clean this up. */
		(void) socketMulticast(cache.multicast.socket, cache.multicast_ip, 0);
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "closing multicast socket...");
	socketClose(cache.multicast.socket);
	cache.multicast.socket = NULL;

	free(cache.multicast_ip);
	cache.multicast_ip = NULL;

	if (0 < debug)
		syslog(LOG_DEBUG, "multicast listener port=%d stopped", cache.multicast.port);
}

int
mccStartMulticast(const char *multicast_ip, int port)
{
	int rc;
	SocketAddress *any_address;
	pthread_attr_t pthread_attr;

	mccStopMulticast();
	if (multicast_ip == NULL)
		return MCC_OK;

	cache.multicast.port = port;
	cache.multicast.is_running = 0;

	if ((cache.multicast_ip = socketAddressCreate(multicast_ip, port)) == NULL) {
		syslog(LOG_ERR, "multicast address error: %s, (%d)", strerror(errno), errno);
		goto error0;
	}

	if ((cache.multicast.socket = socketOpen(cache.multicast_ip, 0)) == NULL) {
		syslog(LOG_ERR, "multicast socket error: %s, (%d)", strerror(errno), errno);
		goto error1;
	}

	if (socketSetReuse(cache.multicast.socket, 1)) {
		syslog(LOG_ERR, "multicast socketSetReuse(true) failed: %s (%d)", strerror(errno), errno);
		goto error1;
	}

	if ((any_address = socketAddressCreate(cache.multicast_ip->sa.sa_family == AF_INET ? "0.0.0.0" : "::0", port)) == NULL) {
		syslog(LOG_ERR, "multicast address error: %s, (%d)", strerror(errno), errno);
		goto error1;
	}

	if (socketBind(cache.multicast.socket, any_address)) {
		syslog(LOG_ERR, "multicast socketBind() failed: %s (%d)", strerror(errno), errno);
		free(any_address);
		goto error1;
	}

	free(any_address);

	if (socketMulticast(cache.multicast.socket, cache.multicast_ip, 1)) {
		syslog(LOG_ERR, "multicast socketMulticast() join group failed: %s (%d)", strerror(errno), errno);
		goto error1;
	}

	if (socketMulticastLoopback(cache.multicast.socket, 0)) {
		syslog(LOG_ERR, "multicast socketMulticastLoopback(false) failed: %s (%d)", strerror(errno), errno);
		goto error1;
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
	rc = pthread_create(&cache.multicast.thread, &pthread_attr, mcc_listener_thread, &cache.multicast);
#ifdef HAVE_PTHREAD_ATTR_INIT
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (rc != 0) {
		syslog(LOG_ERR, "multicast listener thread error: %s, (%d)", strerror(errno), errno);
		goto error1;
	}

	return MCC_OK;
error1:
	mccStopMulticast();
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
		syslog(LOG_DEBUG, "mccGetKey" MCC_KEY_FMT, key);

	if (sqlite3_bind_text(mcc->select_one, 1, (const char *) key, length, SQLITE_STATIC) != SQLITE_OK)
		goto error0;

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

	if (1 < debug)
		syslog(LOG_DEBUG, "mccExpireRows when=%lu", (unsigned long) *when);

	if (sqlite3_bind_int(mcc->expire, 1, (int)(uint32_t) *when) != SQLITE_OK)
		goto error0;
	if (cache.hook.expire != NULL && (*cache.hook.expire)(mcc, NULL))
		goto error1;
	if (mccSqlStep(mcc, mcc->expire, MCC_SQL_EXPIRE) == SQLITE_DONE)
		rc = MCC_OK;
error1:
	(void) sqlite3_clear_bindings(mcc->expire);
error0:
	return rc;
}

int
mccDeleteAll(mcc_handle *mcc)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL)
		goto error0;

	if (mccSqlStep(mcc, mcc->truncate, MCC_SQL_TRUNCATE) == SQLITE_DONE)
		rc = MCC_OK;
error0:
	return rc;
}

int
mccSetSecret(const char *secret)
{
	char *copy;

	if ((copy = strdup(secret)) == NULL)
		return MCC_ERROR;

	free(cache.secret);
	cache.secret = copy;
	cache.secret_length = strlen(copy);

	return MCC_OK;
}

static void *
mcc_expire_thread(void *ignore)
{
	time_t now;
	mcc_handle *mcc;

	if ((mcc = mccCreate()) == NULL) {
		syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		goto error0;
	}
	pthread_cleanup_push(mccDestroy, mcc);

	for (cache.gc_next = time(NULL); 0 < cache.gc_period; ) {
		pthread_testcancel();

		if (cache.gc_next <= (now = time(NULL))) {
			if (1 < debug)
				syslog(LOG_DEBUG, "mcc gc thread...");
			(void) mccExpireRows(mcc, &now);
			cache.gc_next = now + cache.gc_period;
		}
		pthreadSleep(cache.gc_next - now, 0);
	}
	pthread_cleanup_pop(1);
error0:
	PTHREAD_END(NULL);
}

void
mccStopGc(void)
{
	if (cache.gc_next != 0) {
		cache.gc_period = 0;
#if defined(HAVE_PTHREAD_CANCEL)
		(void) pthread_cancel(cache.gc_thread);
#endif
	}
}

int
mccStartGc(unsigned ttl)
{
	int rc;
	pthread_attr_t pthread_attr;

	cache.gc_period = ttl;

	if (ttl == 0 && cache.gc_next != 0) {
		mccStopGc();
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
	rc = pthread_create(&cache.gc_thread,  &pthread_attr, mcc_expire_thread, NULL);
	(void) pthread_detach(cache.gc_thread);
#ifdef HAVE_PTHREAD_ATTR_INIT
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (rc != 0) {
error1:
		syslog(LOG_ERR, "multi/unicast cache timer thread error: %s, (%d)", strerror(errno), errno);
		cache.gc_next = 0;
		cache.gc_period = 0;
		return MCC_ERROR;
	}

	return MCC_OK;
}

void
mccAtForkPrepare(void)
{
	(void) pthread_mutex_lock(&cache.mutex);
	(void) pthread_mutex_lock(&cache.active_mutex);
}

void
mccAtForkParent(void)
{
	(void) pthread_mutex_unlock(&cache.active_mutex);
	(void) pthread_mutex_unlock(&cache.mutex);
}

void
mccAtForkChild(void)
{
	(void) pthread_mutex_unlock(&cache.active_mutex);
	(void) pthread_mutex_unlock(&cache.mutex);

	(void) pthread_mutex_destroy(&cache.active_mutex);
	(void) pthread_mutex_destroy(&cache.mutex);

	cache.multicast.is_running = 0;
	socketClose(cache.multicast.socket);
	cache.multicast.socket = NULL;

	cache.unicast.is_running = 0;
	socketClose(cache.unicast.socket);
	cache.unicast.socket = NULL;
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

	if (2 < debug)
		syslog(LOG_DEBUG, "%s mcc=%lx", __FUNCTION__, (long) mcc);

	if (mcc != NULL) {
		PTHREAD_DISABLE_CANCEL();
		mcc_sql_stmts_finalize(mcc);
		if (mcc->db != NULL)
			sqlite3_close(mcc->db);
		free(mcc);
		PTHREAD_RESTORE_CANCEL();
	}
}

mcc_handle *
mccCreate(void)
{
	mcc_handle *mcc;

	PTHREAD_DISABLE_CANCEL();

	if ((mcc = calloc(1, sizeof (*mcc))) == NULL) {
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, strerror(errno), errno);
		goto error0;
	}

	if (sqlite3_open(cache.path, &mcc->db) != SQLITE_OK) {
		syslog(LOG_ERR, "multi/unicast cache \"%s\" open error: %s", cache.path, sqlite3_errmsg(mcc->db));
		goto error1;
	}

	/* Ignore errors if the table already exists. */
	(void) mcc_sql_create(mcc);

	if (mcc_sql_stmts_prepare(mcc)) {
error1:
		mccDestroy(mcc);
		mcc = NULL;
	}
error0:
	PTHREAD_RESTORE_CANCEL();

	if (2 < debug)
		syslog(LOG_DEBUG, "%s mcc=%lx", __FUNCTION__, (long) mcc);

	return mcc;
}

void
mccFini(void)
{
	if (0 < debug)
		syslog(LOG_DEBUG, "mccFini()");

	/* Stop these threads before releasing the rest. */
	mccStopMulticast();
	mccStopUnicast();
	mccStopGc();

	mcc_active_cleanup(cache.active);
	VectorDestroy(cache.key_hooks);
	free(cache.secret);

	(void) pthread_mutex_destroy(&cache.active_mutex);
	(void) pthread_mutex_destroy(&cache.mutex);
}

int
mccInit(const char *path, mcc_hooks *hooks)
{
	memset(&cache, 0, sizeof (cache));

	if ((cache.path = strdup(path)) == NULL) {
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, strerror(errno), errno);
		goto error0;
	}

	if (mccSetSecret("") == MCC_ERROR) {
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	if ((cache.key_hooks = VectorCreate(5)) == NULL) {
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	VectorSetDestroyEntry(cache.key_hooks, mcc_key_hook_free);

	/* Copy the mcc_hooks structure to the mcc_context. Normally these
	 * would be defined after the mccCreate, but we may need to prepare
	 * some SQL statement with respect to the database when we create
	 * the mcc_context.
	 */
	if (hooks != NULL)
		cache.hook = *hooks;

	if (pthread_mutex_init(&cache.mutex, NULL)) {
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	if (pthread_mutex_init(&cache.active_mutex, NULL)) {
		syslog(LOG_ERR, log_error, __FILE__, __LINE__, strerror(errno), errno);
		goto error1;
	}

	return MCC_OK;
error1:
	mccFini();
error0:
	return MCC_ERROR;
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

# include <com/snert/lib/sys/sysexits.h>
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

static char *multicast_ip;
static char *cache_secret;
static unsigned cache_ttl;
static unsigned gc_period;

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
	mcc_handle *mcc;
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
			gc_period = (unsigned) strtol(optarg, NULL, 10);
			break;
		case 'm':
			multicast_ip = optarg;;
			break;
		case 'M':
			multicast_listener_port = strtol(optarg, NULL, 10);
			break;
		case 's':
			cache_secret = optarg;
			break;
		case 't':
			cache_ttl = (unsigned) strtol(optarg, NULL, 10);
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
			exit(EX_USAGE);
		}
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, usage);
		exit(EX_USAGE);
	}

	if (0 < debug) {
		LogSetProgramName("mcc");
		LogOpen("(standard error)");
		LogSetLevel(LOG_DEBUG);
		socketSetDebug(1);
	}

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() %s (%d)", strerror(errno), errno);
		exit(EX_OSERR);
	}

#ifdef SIGUSR1
	if (signal(SIGUSR1, signalThreadExit) == SIG_ERR) {
		syslog(LOG_ERR, "SIGUSR1 error: %s (%d)", strerror(errno), errno);
		exit(1);
	}
#endif
	signal(SIGTERM, signalExit);
	signal(SIGINT, signalExit);

	/* Prepare global data. */
	if (mccInit(argv[optind], NULL) != 0)
		exit(EX_OSERR);
	if (0 < gc_period)
		mccStartGc(gc_period);
	if (cache_secret != NULL)
		mccSetSecret(cache_secret);
	if (multicast_ip != NULL && mccStartMulticast(multicast_ip, multicast_listener_port) == MCC_ERROR)
		goto error0;
	if (unicast_list != NULL && mccStartUnicast((const char **) VectorBase(unicast_list), unicast_listener_port) == MCC_ERROR)
		goto error0;

	/* Get a database handle. */
	if ((mcc = mccCreate()) == NULL)
		goto error1;

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
			new_row.expires = new_row.touched + cache_ttl;

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
error1:
	mccDestroy(mcc);
error0:
	VectorDestroy(unicast_list);
	mccFini();

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

