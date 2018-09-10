/*
 * mcc.c
 *
 * Multicast / Unicast Cache
 *
 * Copyright 2006, 2014 by Anthony Howe. All rights reserved.
 */

#ifndef MCC_LISTENER_TIMEOUT
#define MCC_LISTENER_TIMEOUT	10000
#endif

#ifndef MCC_PORT
#define MCC_PORT		6920
#endif

#ifndef MCC_CACHE_TTL
#define MCC_CACHE_TTL		90000
#endif

#ifndef MCC_SQLITE_BUSY_MS
#define MCC_SQLITE_BUSY_MS	15000
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

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

/***********************************************************************
 ***
 ***********************************************************************/

static int debug;
static mcc_data cache;
static const char log_error[] = "%s(%u): %s (%d)";

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
		syslog(LOG_DEBUG, "mcc active ip=%s ppm=%lu max-ppm=%lu", ip, rate, entry->max_ppm);

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
mccRegisterKey(mcc_key_hook *tag_hook)
{
	return VectorAdd(cache.key_hooks, tag_hook);
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
	/* Using the newer sqlite_prepare_v2() interface will
	 * handle SQLITE_SCHEMA errors automatically.
	 */
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_SELECT_ONE, -1, &mcc->select_one, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_SELECT_ONE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_REPLACE, -1, &mcc->replace, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_REPLACE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_TRUNCATE, -1, &mcc->truncate, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_TRUNCATE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_EXPIRE, -1, &mcc->expire, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_EXPIRE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_DELETE, -1, &mcc->remove, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_DELETE, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_BEGIN, -1, &mcc->begin, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_BEGIN, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_COMMIT, -1, &mcc->commit, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_COMMIT, sqlite3_errmsg(mcc->db));
		return -1;
	}
	if (sqlite3_prepare_v2_blocking(mcc->db, MCC_SQL_ROLLBACK, -1, &mcc->rollback, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "mcc statement error: %s %s", MCC_SQL_ROLLBACK, sqlite3_errmsg(mcc->db));
		return -1;
	}

	if (cache.hook.prepare != NULL && (*cache.hook.prepare)(mcc, NULL)) {
		syslog(LOG_ERR, "mcc prepare hook failed");
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

int
mccSqlStep(mcc_handle *mcc, sqlite3_stmt *sql_stmt, const char *sql_stmt_text)
{
	int rc;

	PTHREAD_DISABLE_CANCEL();

	if (sql_stmt == mcc->commit || sql_stmt == mcc->rollback)
		mcc->is_transaction = 0;

	(void) sqlite3_busy_timeout(mcc->db, MCC_SQLITE_BUSY_MS);
	rc = sqlite3_step_blocking(sql_stmt);

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
mccPutRowLocal(mcc_handle *mcc, mcc_row *row)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || row == NULL)
		goto error0;

	if (1 < debug)
		syslog(
			LOG_DEBUG,
			"%s key=%d:" MCC_FMT_K " value=%d:" MCC_FMT_V " expires=" MCC_FMT_E " created=" MCC_FMT_C,
			__FUNCTION__,
			MCC_GET_K_SIZE(row), MCC_FMT_K_ARG(row),
			MCC_GET_V_SIZE(row), MCC_FMT_V_ARG(row),
			MCC_FMT_E_ARG(row), MCC_FMT_C_ARG(row)
		);

	if (sqlite3_bind_text(mcc->replace, 1, (const char *) MCC_PTR_K(row), MCC_GET_K_SIZE(row), SQLITE_TRANSIENT) != SQLITE_OK)
		goto error0;
	if (sqlite3_bind_text(mcc->replace, 2, (const char *) MCC_PTR_V(row), MCC_GET_V_SIZE(row), SQLITE_TRANSIENT) != SQLITE_OK)
		goto error1;
	if (sqlite3_bind_int(mcc->replace, 3, (int) row->expires) != SQLITE_OK)
		goto error1;
	if (sqlite3_bind_int(mcc->replace, 4, (int) row->created) != SQLITE_OK)
		goto error1;
	if (mccSqlStep(mcc, mcc->replace, MCC_SQL_REPLACE) == SQLITE_DONE)
		rc = MCC_OK;
error1:
	(void) sqlite3_clear_bindings(mcc->replace);
error0:
	return rc;
}

int
mccSetMulticastTTL(int ttl)
{
	return socketMulticastTTL(cache.server, ttl);
}

int
mccSend(mcc_handle *mcc, mcc_row *row, uint8_t command)
{
	time_t now;
	md5_state_t md5;
	SocketAddress **table;
	int rc, packet_length;

	if (!cache.is_running)
		return MCC_OK;

	(void) time(&now);
	mccUpdateActive("127.0.0.1", (uint32_t *)&now);

	/* Covert some of the values to network byte order. */
	MCC_SET_COMMAND(row, command);
	packet_length = MCC_PACKET_LENGTH(row);

	row->ttl = htonl(row->ttl);
	row->k_size = htons(row->k_size);
	row->v_size = htons(row->v_size);

	md5_init(&md5);
	md5_append(&md5, (md5_byte_t *) row+sizeof (row->digest), packet_length - sizeof (row->digest));
	md5_append(&md5, (md5_byte_t *) cache.secret, cache.secret_length);
	md5_finish(&md5, (md5_byte_t *) row->digest);

	rc = MCC_OK;

	PTHREAD_MUTEX_LOCK(&cache.mutex);
	for (table = cache.unicast_ip; *table != NULL; table++) {
		if (socketWriteTo(cache.server, (unsigned char *) row, packet_length, *table) != packet_length)
			rc = MCC_ERROR;
	}
	PTHREAD_MUTEX_UNLOCK(&cache.mutex);

	/* Restore our record. */
	row->ttl = ntohl(row->ttl);
	row->k_size = ntohs(row->k_size);
	row->v_size = ntohs(row->v_size);

	if (1 < debug)
		syslog(LOG_DEBUG, "%s command=%c key=" MCC_FMT_K, __FUNCTION__, MCC_GET_COMMAND(row), MCC_FMT_K_ARG(row));

	return rc;
}

static void *
mcc_listener_thread(void *data)
{
	int cmd;
	long nbytes;
	md5_state_t md5;
	mcc_handle *mcc;
	int packet_length;
	SocketAddress from;
	mcc_row row, old_row;
	mcc_key_hook **hooks, *hook;
	unsigned char our_digest[16];
	char ip[IPV6_STRING_SIZE], *listen_addr;

	if ((mcc = mccCreate()) == NULL)
		goto error0;
	pthread_cleanup_push(mccDestroy, mcc);

	listen_addr = socketAddressToString(&cache.server->address);
	pthread_cleanup_push(free, listen_addr);

	syslog(LOG_INFO, "started mcc listener %s", listen_addr);

	/* Silience "may be used uninitialized in this function" warning. */
	nbytes = 0;

	for (cache.is_running = 1; cache.is_running; ) {
		if (!socketHasInput(cache.server, MCC_LISTENER_TIMEOUT)) {
			if (1 < debug)
				syslog(LOG_DEBUG, "mcc socket timeout: %s (%d)", strerror(errno), errno);
			continue;
		}

		PTHREAD_MUTEX_LOCK(&cache.mutex);
		nbytes = socketReadFrom(cache.server, (unsigned char *) &row, sizeof (row), &from);
		PTHREAD_MUTEX_UNLOCK(&cache.mutex);

		if (nbytes <= 0) {
			syslog(LOG_ERR, "mcc socket read error: %s (%d)", strerror(errno), errno);
			continue;
		}

		(void) socketAddressGetString(&from, 0, ip, sizeof (ip));

		row.k_size = ntohs(row.k_size);
		row.v_size = ntohs(row.v_size);
		packet_length = MCC_PACKET_LENGTH(&row);
		row.k_size = htons(row.k_size);
		row.v_size = htons(row.v_size);

		md5_init(&md5);
		md5_append(&md5, (md5_byte_t *) &row + sizeof (row.digest), packet_length - sizeof (row.digest));
		md5_append(&md5, (md5_byte_t *) cache.secret, cache.secret_length);
		md5_finish(&md5, (md5_byte_t *) our_digest);

		if (memcmp(our_digest, row.digest, sizeof (our_digest)) != 0) {
			syslog(LOG_ERR, "mcc digest error from [%s]", ip);
			mccNotesUpdate(ip, "md5=", "md5=N");
			continue;
		}
		mccNotesUpdate(ip, "md5=", "md5=Y");

		row.ttl = ntohl(row.ttl);
		row.k_size = ntohs(row.k_size);
		row.v_size = ntohs(row.v_size);

		(void) time(&row.expires);
		mccUpdateActive(ip, (uint32_t *)&row.expires);
		row.created = row.expires;
		row.expires += row.ttl;

		if (1 < debug) {
			syslog(
				LOG_DEBUG, "mcc from=[%s] cmd=%c key=" MCC_FMT_K,
				ip, MCC_GET_COMMAND(&row), MCC_FMT_K_ARG(&row)
			);
		}

		switch (cmd = MCC_GET_COMMAND(&row)) {
			long add;

		case MCC_CMD_ADD:
			if (MCC_DATA_SIZE-1 <= MCC_GET_K_SIZE(&row) + MCC_GET_V_SIZE(&row)) {
				syslog(LOG_ERR, "mcc size error key=" MCC_FMT_K, MCC_FMT_K_ARG(&row));
				continue;
			}
			MCC_PTR_V(&row)[MCC_GET_V_SIZE(&row)] = '\0';
			add = strtol((char *)MCC_PTR_V(&row), NULL, 10);
			/*@fallthrough@*/

			while (0)  {
		case MCC_CMD_INC: add = +1; break;
		case MCC_CMD_DEC: add = -1; break;
			}

			if (mccAddRowLocal(mcc, add, &row) != MCC_OK) {
				syslog(LOG_ERR, "mcc put error key=" MCC_FMT_K, MCC_FMT_K_ARG(&row));
				continue;
			}
			break;

		case MCC_CMD_PUT:
			/* Preserve created timestamp for an existing
			 * row (see smtpf grey-listing).  Ignore row
			 * not found and error; use the created value
			 * assigned above.
			 */
			if (mccGetKey(mcc, (const unsigned char *) MCC_PTR_K(&row), MCC_GET_K_SIZE(&row), &old_row) == MCC_OK) {
				row.created = old_row.created;
			}
			if (cache.hook.remote_replace != NULL
			&& (*cache.hook.remote_replace)(mcc, NULL, NULL, &row)) {
				continue;
			}
			if (mccPutRowLocal(mcc, &row) != MCC_OK) {
				syslog(LOG_ERR, "mcc put error key=" MCC_FMT_K, MCC_FMT_K_ARG(&row));
				continue;
			}
			break;

		case MCC_CMD_REMOVE:
			if (cache.hook.remote_remove != NULL
			&& (*cache.hook.remote_remove)(mcc, NULL, NULL, &row)) {
				continue;
			}
			if (mccDeleteKey(mcc, MCC_PTR_K(&row), MCC_GET_K_SIZE(&row)) != MCC_OK) {
				syslog(LOG_ERR, "mcc remove error key=" MCC_FMT_K, MCC_FMT_K_ARG(&row));
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

				if (hook->prefix_length <= MCC_GET_K_SIZE(&row)
				&& memcmp(MCC_PTR_K(&row), hook->prefix, hook->prefix_length) == 0) {
					(*hook->process)(mcc, hook, ip, &row);
					break;
				}
			}
			break;

		default:
			syslog(LOG_ERR, "mcc from=[%s] unknown cmd=%c", ip, cmd);
		}

	}

	syslog(LOG_INFO, "mcc listener %s thread exit", listen_addr);
	pthread_cleanup_pop(1);
	pthread_cleanup_pop(1);
error0:
	PTHREAD_END(NULL);
}

void
mccStopListener(void)
{
	int port;
	void *rv;
	SocketAddress **table;

	if (cache.server == NULL)
		return;

	port = socketAddressGetPort(&cache.server->address);

	/* Do we already have a running listener? */
	if (cache.is_running) {
		/* Stop the listener thread... */
		cache.is_running = 0;

		/* Wait for the thread to exit. */
		(void) pthread_cancel(cache.listener);

		if (0 < debug)
			syslog(LOG_DEBUG, "waiting for mcc listener thread to terminate...");
		(void) pthread_join(cache.listener, &rv);
	}

	if (cache.unicast_ip != NULL) {
		for (table = cache.unicast_ip; *table != NULL; table++)
			free(*table);

		free(cache.unicast_ip);
		cache.unicast_ip = NULL;
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "closing mcc listener socket...");

	socketClose(cache.server);
	cache.server = NULL;

	if (0 < debug)
		syslog(LOG_DEBUG, "mcc listener port=%d stopped", port);
}

int
mccStartListener(const char **ip_array, int port)
{
	int i, j, count;
	const char *this_host;
	SocketAddress *address;
	pthread_attr_t pthread_attr;

	if (ip_array == NULL)
		return MCC_OK;

	mccStopListener();

	for (count = 0; ip_array[count] != NULL; count++)
		;
	if ((cache.unicast_ip = calloc(count+1, sizeof (*cache.unicast_ip))) == NULL)
		goto error0;

	for (i = j = 0; i < count; i++, j++) {
		cache.unicast_ip[j] = socketAddressCreate(ip_array[i], port);

		/* Avoid broadcast-to-self by discarding our own IP. */
		if (socketAddressIsLocal(cache.unicast_ip[j])) {
			syslog(LOG_WARN, "mcc address %s skipped", ip_array[i]);
			free(cache.unicast_ip[j]);
			j--;
		}
	}

	/* Assert the array is NULL terminated. */
	cache.unicast_ip[j] = NULL;

	if (count <= 0 || cache.unicast_ip[0] == NULL) {
		syslog(LOG_ERR, "mcc listener empty IP address list");
		goto error1;
	}

	/* Assume that list of unicast addresses are all in the same family. */
	this_host = cache.unicast_ip[0]->sa.sa_family == AF_INET ? "0.0.0.0" : "::0";

	if ((address = socketAddressCreate(this_host, port)) == NULL) {
		syslog(
			LOG_ERR, "mcc listener address: %s, (%d)",
			strerror(errno), errno
		);
		goto error1;
	}

	cache.server = socketOpen(address, 0);
	free(address);

	if (cache.server == NULL) {
		syslog(
			LOG_ERR, "mcc listener socket: %s, (%d)",
			strerror(errno), errno
		);
		goto error1;
	}

	if (socketSetNonBlocking(cache.server, 1)) {
		syslog(
			LOG_ERR, "mcc listener non-blocking: %s (%d)",
			strerror(errno), errno
		);
		goto error1;
	}
	if (socketSetReuse(cache.server, 1)) {
		syslog(
			LOG_ERR, "mcc listener socketSetReuse(%d, 1): %s (%d)",
			socketGetFd(cache.server), strerror(errno), errno
		);
		goto error1;
	}
	if (socketBind(cache.server, &cache.server->address)) {
		syslog(
			LOG_ERR, "mcc listener bind: %s (%d)",
			strerror(errno), errno
		);
		goto error1;
	}
	if (socketMulticastTTL(cache.server, 1)) {
		syslog(
			LOG_ERR, "mcc listener hops: %s (%d)",
			strerror(errno), errno
		);
		goto error1;
	}
	if (socketMulticastLoopback(cache.server, 0)) {
		syslog(
			LOG_ERR, "mcc listener disable loopback: %s (%d)",
			strerror(errno), errno
		);
		goto error1;
	}

	for (i = 0; i < count; i++) {
		if (!isReservedIP(ip_array[i], IS_IP_MULTICAST))
			continue;
		if ((address = socketAddressNew(ip_array[i], port)) == NULL)
			continue;
		j = socketMulticast(cache.server, address, 1);
		free(address);
		if (j != 0) {
			syslog(
				LOG_ERR, "mcc listener %s join: %s (%d)",
				ip_array[i], strerror(errno), errno
			);
			goto error1;
		}
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
	i = pthread_create(&cache.listener, &pthread_attr, mcc_listener_thread, NULL);
#ifdef HAVE_PTHREAD_ATTR_INIT
	(void) pthread_attr_destroy(&pthread_attr);
#endif
	if (i != 0) {
		syslog(
			LOG_ERR, "mcc listener thread: %s, (%d)",
			strerror(errno), errno
		);
		goto error1;
	}

	return MCC_OK;
error1:
	mccStopListener();
error0:
	return MCC_ERROR;
}

int
mccGetKey(mcc_handle *mcc, const unsigned char *key, unsigned length, mcc_row *row)
{
	int rc;

	rc = MCC_ERROR;

	if (mcc == NULL || key == NULL || row == NULL)
		goto error0;

	if (1 < debug)
		syslog(LOG_DEBUG, "%s key=%.*s", __FUNCTION__, length, key);

	if (sqlite3_bind_text(mcc->select_one, 1, (const char *) key, length, SQLITE_STATIC) != SQLITE_OK)
		goto error0;

	switch (mccSqlStep(mcc, mcc->select_one, MCC_SQL_SELECT_ONE)) {
		time_t now;

	case SQLITE_DONE:
		rc = MCC_NOT_FOUND;
		break;
	case SQLITE_ROW:
		MCC_SET_COMMAND(row, '\0');
		MCC_SET_K_SIZE(row, sqlite3_column_bytes(mcc->select_one, 0));
		(void) memcpy(MCC_PTR_K(row), sqlite3_column_text(mcc->select_one, 0), MCC_GET_K_SIZE(row));

		MCC_SET_EXTRA(row, '\0');
		MCC_SET_V_SIZE(row, sqlite3_column_bytes(mcc->select_one, 1));
		(void) memcpy(MCC_PTR_V(row), sqlite3_column_text(mcc->select_one, 1), MCC_GET_V_SIZE(row));

		row->expires = (uint32_t) sqlite3_column_int(mcc->select_one, 2);
		row->created = (uint32_t) sqlite3_column_int(mcc->select_one, 3);

		(void) time(&now);
		row->ttl = now < row->expires ? row->expires - now : 0;
		rc = MCC_OK;

		/* There should be only one row for the key so we
		 * can reset the statement's state machine.
		 */
		(void) sqlite3_reset(mcc->select_one);

		if (1 < debug) {
			syslog(
				LOG_DEBUG,
				"%s key=%d:" MCC_FMT_K " value=%d:" MCC_FMT_V
				" expires=" MCC_FMT_E " created=" MCC_FMT_C " ttl=" MCC_FMT_TTL,
				__FUNCTION__,
				MCC_GET_K_SIZE(row), MCC_FMT_K_ARG(row),
				MCC_GET_V_SIZE(row), MCC_FMT_V_ARG(row),
				MCC_FMT_E_ARG(row), MCC_FMT_C_ARG(row),
				MCC_FMT_TTL_ARG(row)
			);
		}
	}
	(void) sqlite3_clear_bindings(mcc->select_one);
error0:
	return rc;
}

int
mccGetRow(mcc_handle *mcc, mcc_row *row)
{
	return mccGetKey(mcc, (const unsigned char *) MCC_PTR_K(row), MCC_GET_K_SIZE(row), row);
}

int
mccPutRow(mcc_handle *mcc, mcc_row *row)
{
	int rc;

	if ((rc = mccPutRowLocal(mcc, row)) == MCC_OK) {
		(void) mccSend(mcc, row, MCC_CMD_PUT);
	}

	return rc;
}

int
mccPutKeyValue(mcc_handle *mcc, const char *key, const char *value, unsigned long ttl)
{
	int length;
	mcc_row row;

	row.ttl = ttl;
	row.expires = time(NULL) + ttl;

	length = snprintf((char *)MCC_PTR_K(&row), MCC_DATA_SIZE, "%s", (char *)key);
	MCC_SET_K_SIZE(&row, length);

	length = snprintf((char *)MCC_PTR_V(&row), MCC_GET_V_SPACE(&row), "%s", value);
	MCC_SET_V_SIZE(&row, length);

	return mccPutRow(mcc, &row);
}

/**
 * @param mcc
 *	Pointer to a multicast context.
 *
 * @param add
 *	Signed increment to add to the cached row's value.
 *
 * @param row
 *	Pointer to a cached row, with key and expires set. When no row
 *	exists, assume a value of zero. On return updated local value
 *	will be set.
 *
 * @return
 *	MCC_OK or MCC_ERROR.
 */
int
mccAddRowLocal(mcc_handle *mcc, long add, mcc_row *row)
{
	int length;
	time_t expires;
	long number = 0;

	/* Save expires timestamp for mccPutRowLocal(). */
	expires = row->expires;

	switch (mccGetRow(mcc, row)) {
	case MCC_OK:
		/* Room enough for a terminating NUL byte? */
		if (MCC_DATA_SIZE-1 <= MCC_GET_K_SIZE(row) + MCC_GET_V_SIZE(row))
			return MCC_ERROR;

		/* Get the current value. */
		MCC_PTR_V(row)[MCC_GET_V_SIZE(row)] = '\0';
		number = strtol((char *)MCC_PTR_V(row), NULL, 10);
		/*@fallthrough@*/

	case MCC_NOT_FOUND:
		number += add;
		length = snprintf((char *)MCC_PTR_V(row), MCC_GET_V_SPACE(row), "%ld", number);
		MCC_SET_V_SIZE(row, length);

		/* Restore expires timestamp for updated row. */
		row->expires = expires;

		return mccPutRowLocal(mcc, row);
	}

	return MCC_ERROR;
}

int
mccAddRow(mcc_handle *mcc, long add, mcc_row *row)
{
	int length;

	/* Broadcast the adjustment. */
	length = snprintf((char *)MCC_PTR_V(row), MCC_GET_V_SPACE(row), "%+ld", add);
	MCC_SET_V_SIZE(row, length);
	(void) mccSend(mcc, row, MCC_CMD_ADD);

	/* Adjust the local copy and update row's value. */
	return mccAddRowLocal(mcc, add, row);
}

void
mccSetExpires(mcc_row *row, unsigned long ttl)
{
	row->ttl = ttl;
	row->expires = time(NULL) + ttl;
}

int
mccSetKey(mcc_row *row, const char *fmt, ...)
{
	int length;
	va_list args;

	va_start(args, fmt);
	length = vsnprintf((char *)MCC_PTR_K(row), MCC_DATA_SIZE, fmt, args);
	MCC_SET_K_SIZE(row, length);
	va_end(args);

	return length;
}

int
mccSetValue(mcc_row *row, const char *fmt, ...)
{
	int length;
	va_list args;

	va_start(args, fmt);
	length = vsnprintf((char *)MCC_PTR_V(row), MCC_GET_V_SPACE(row), fmt, args);
	MCC_SET_V_SIZE(row, length);
	va_end(args);

	return length;
}

int
mccDeleteRowLocal(mcc_handle *mcc, mcc_row *row)
{
	return mccDeleteKey(mcc, MCC_PTR_K(row), MCC_GET_K_SIZE(row));
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
		syslog(LOG_DEBUG, "%s when=%lu", __FUNCTION__, (unsigned long) *when);

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
		syslog(LOG_ERR, "mcc timer thread error: %s, (%d)", strerror(errno), errno);
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

	cache.is_running = 0;
	socketClose(cache.server);
	cache.server = NULL;
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
		syslog(LOG_ERR, "mcc \"%s\" open error: %s", cache.path, sqlite3_errmsg(mcc->db));
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
		syslog(LOG_DEBUG, "%s", __FUNCTION__);

	/* Stop these threads before releasing the rest. */
	mccStopListener();
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

#if defined(__sun__) && !defined(_POSIX_PTHREAD_SEMANTICS)
#  define _POSIX_PTHREAD_SEMANTICS
# endif
# include <signal.h>

# include <com/snert/lib/sys/sysexits.h>
# include <com/snert/lib/util/Text.h>
# include <com/snert/lib/util/getopt.h>

#undef MCC_CACHE_TTL
#define MCC_CACHE_TTL		300

static const char usage_opt[] = "Lg:i:p:s:t:v";

static char usage[] =
"usage: mcc [-Lv][-g seconds][-i list][-p port][-s secret][-t seconds] db.sq3\n"
"\n"
"-g seconds\tGC thread interval\n"
"-i list\t\tcomma separated list of multicast and/or unicast hosts\n"
"-L\t\tallow multicast loopback\n"
"-p port\t\tmcc listener port; default " QUOTE(MCC_PORT) "\n"
"-s secret\tshared secret for packet validation\n"
"-t seconds\tcache time-to-live in seconds per record; default " QUOTE(MCC_CACHE_TTL) "\n"
"-v\t\tverbose logging to the user log\n"
"\n"
"Standard input are commands of the form:\n"
"\n"
"GET key\n"
"PUT key value\n"
"RESET key value\n"
"DEL key\n"
"ADD key number\n"
"DEC key\n"
"INC key\n"
"QUIT\n"
"\n"
"Note that a key cannot contain whitespace, while the value may.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

/* 232.173.190.239 , FF02::DEAD:BEEF */

static char *cache_secret;
static unsigned gc_period;
static int multicast_loopback;

static Vector unicast_list = NULL;
static unsigned cache_ttl = MCC_CACHE_TTL;
static long unicast_listener_port = MCC_PORT;

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
#undef syslog
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
		case 'L':
			multicast_loopback = 1;
			break;
		case 's':
			cache_secret = optarg;
			break;
		case 't':
			cache_ttl = (unsigned) strtol(optarg, NULL, 10);
			break;
		case 'i':
			if ((unicast_list = TextSplit(optarg, ",", 0)) == NULL) {
				syslog(LOG_ERR, "memory error: %s (%d)", strerror(errno), errno);
				exit(71);
			}
			break;
		case 'p':
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
		closelog();
		LogSetProgramName("mcc");
		LogOpen("(standard error)");
		setlogmask(LOG_UPTO(LOG_INFO));
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

	/* Get a database handle.  If database doesn't exist it will
	 * be created while we're still single threaded.  mccCreate is
	 * also called by mcc_listener_thread to get a handle, however,
	 * the database should already be created to avoid locking
	 * conflict between threads.
	 */
	if ((mcc = mccCreate()) == NULL)
		goto error0;

	if (0 < gc_period)
		mccStartGc(gc_period);
	if (cache_secret != NULL)
		mccSetSecret(cache_secret);
	if (mccStartListener((const char **) VectorBase(unicast_list), unicast_listener_port) == MCC_ERROR)
		goto error1;
	if (multicast_loopback)
		(void) socketMulticastLoopback(cache.server, 1);
	syslog(LOG_INFO, "mcc " LIBSNERT_COPYRIGHT);

	rc = EXIT_SUCCESS;

	for (lineno = 1; 0 <= (length = TextInputLine2(stdin, buffer, sizeof (buffer), 0)); lineno++) {
		enum { IDX_QUIT, IDX_GET, IDX_PUT, IDX_RESET, IDX_DEL, IDX_ADD, IDX_INC, IDX_DEC };
		static char *commands[] = {
			"quit", "get", "put", "reset", "del", "add", "inc", "dec",  NULL
		};
		char **cmd;

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
		MCC_SET_K_SIZE(&new_row, span);
		(void) memcpy(MCC_PTR_K(&new_row), arg, span);

		/* Optional remainder of input is the value. */
		if (arg[span] != '\0') {
			arg += span;
			arg += strspn(arg, " \t");
			span = (int) strlen(arg);
			MCC_SET_V_SIZE(&new_row, span);
			(void) memcpy(MCC_PTR_V(&new_row), arg, span);
		}

		MCC_SET_COMMAND(&new_row, 0);
		MCC_SET_EXTRA(&new_row, 0);

		for (cmd = commands; *cmd != NULL; cmd++) {
			if (strcmp(buffer, *cmd) == 0)
				break;
		}
		if (*cmd == NULL) {
			printf("input error\n");
			fflush(stdout);
			continue;
		}
		if (cmd == commands)
			break;

		switch (mccGetKey(mcc, MCC_PTR_K(&new_row), MCC_GET_K_SIZE(&new_row), &old_row)) {
		case MCC_OK:
			printf(
				"old key=%d:" MCC_FMT_K " value=%d:" MCC_FMT_V " ttl=" MCC_FMT_TTL " expires=" MCC_FMT_E " created=" MCC_FMT_C "\n",
				MCC_GET_K_SIZE(&old_row), MCC_FMT_K_ARG(&old_row),
				MCC_GET_V_SIZE(&old_row), MCC_FMT_V_ARG(&old_row),
				MCC_FMT_TTL_ARG(&old_row),
				MCC_FMT_E_ARG(&old_row), MCC_FMT_C_ARG(&old_row)
			);
			fflush(stdout);
			break;
		case MCC_ERROR:
			printf("GET error\n");
			fflush(stdout);
			continue;
		case MCC_NOT_FOUND:
			if (tolower(*buffer) == 'g') {
				printf("key=" MCC_FMT_K " not found\n", MCC_FMT_K_ARG(&new_row));
				fflush(stdout);
				continue;
			}
			new_row.ttl = 0;
			(void) time(&new_row.created);
			new_row.expires = new_row.created;
		}

		switch (cmd - commands) {
		default: /* quit, get */
			break;

		case IDX_ADD: /* add */
			mccSetExpires(&new_row, cache_ttl);
			if (mccSend(mcc, &new_row, MCC_CMD_ADD) == MCC_ERROR) {
				printf("error %s\n", buffer);
				fflush(stdout);
			}
			break;

		case IDX_INC: case IDX_DEC: /* inc, dec */
			MCC_SET_V_SIZE(&new_row, 0);
			mccSetExpires(&new_row, cache_ttl);
			if (mccSend(mcc, &new_row, tolower(*buffer)) == MCC_ERROR) {
				printf("error %s\n", buffer);
				fflush(stdout);
			}
			break;

		case IDX_RESET: /* reset */
			mccSetExpires(&new_row, cache_ttl);
			/*@fallthrough@*/

		case IDX_PUT: /* put */
			/* PUT preserves the ttl and expires fields.  RSET does not. */
			switch (mccPutRow(mcc, &new_row)) {
			case MCC_OK:
				printf(
					"new key=%d:" MCC_FMT_K " value=%d:" MCC_FMT_V " ttl=" MCC_FMT_TTL " expires=" MCC_FMT_E " created=" MCC_FMT_C "\n",
					MCC_GET_K_SIZE(&new_row), MCC_FMT_K_ARG(&new_row),
					MCC_GET_V_SIZE(&new_row), MCC_FMT_V_ARG(&new_row),
					MCC_FMT_TTL_ARG(&new_row),
					MCC_FMT_E_ARG(&new_row), MCC_FMT_C_ARG(&new_row)
				);
				fflush(stdout);
				break;
			case MCC_ERROR:
				printf("PUT error\n");
				fflush(stdout);
				break;
			}
			break;

		case IDX_DEL: /* del */
			switch (mccDeleteRow(mcc, &new_row)) {
			case MCC_OK:
				printf("deleted key=" MCC_FMT_K "\n", MCC_FMT_K_ARG(&new_row));
				fflush(stdout);
				break;
			case MCC_ERROR:
				printf("DELETE error\n");
				fflush(stdout);
				break;
			}
			break;
		}
	}
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

