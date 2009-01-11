/*
 * mcc.h
 *
 * Multicast Cache
 *
 * Copyright 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_mcc_h__
#define __com_snert_lib_type_mcc_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/sys/pthread.h>

#ifndef MCC_STACK_SIZE
# define MCC_STACK_SIZE			(32 * 1024)
#endif
#if MCC_STACK_SIZE < PTHREAD_STACK_MIN
# undef MCC_STACK_SIZE
# define MCC_STACK_SIZE		PTHREAD_STACK_MIN
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#ifdef HAVE_SQLITE3_H
# include <sqlite3.h>

# if SQLITE_VERSION_NUMBER < 3003008
#  error "Thread safe SQLite3 version 3.3.8 or better required."
# endif

#define MCC_OK					0
#define MCC_ERROR				(-1)
#define MCC_NOT_FOUND				(-2)

#define MCC_PKT_DIGEST				0
#define MCC_PKT_CREATED				16
#define MCC_PKT_TOUCHED				20
#define MCC_PKT_EXPIRES				24
#define MCC_PKT_HITS				28
#define MCC_PKT_COMMAND				32
#define MCC_PKT_SPARE				33
#define MCC_PKT_KEY_LENGTH			34
#define MCC_PKT_VALUE_LENGTH			35
#define MCC_PKT_KEY_DATA			36

/*
 * The key size was derived from the need to be able to save long
 * keys typically found with grey-listing.
 *
 * For the traditional grey-list tuple of { IP, sender, recipient }
 * that meant { IPV4_STRING_LENGTH (16) + SMTP_PATH_LENGTH (256) * 2 }
 * equals 528 bytes which exceeds the size of a UDP packet.
 *
 * Since typical mail addresses never use the max. possible, a more
 * conservative value was used { IPV6_STRING_LENGTH (40) + 128 * 2 }
 * equals 296. However, with configurable grey-listing keys (see
 * BarricadeMX), instead of an IP, a PTR might appear and/or a HELO
 * argumenent both, which are FQDN that could each be 256 bytes long.
 * Again PTR and HELO values seldom use the max. possible length,
 *
 * A comprimise was required in order to support large keys, yet still
 * fit in a UDP packet, leave some room for a value field, and extra
 * supporting data. The value 383 = 3 * 128 - 1 was used.
 */
#define MCC_MAX_KEY_SIZE			383
#define MCC_MAX_KEY_SIZE_S			"382"

#define MCC_MAX_VALUE_SIZE			92
#define MCC_MAX_VALUE_SIZE_S			"91"

#define MCC_ON_CORRUPT_EXIT			0
#define MCC_ON_CORRUPT_RENAME			1
#define MCC_ON_CORRUPT_REPLACE			2

#define MCC_SYNC_OFF				0
#define MCC_SYNC_NORMAL				1
#define MCC_SYNC_FULL				2

/*
 * A multicast cache packet cannot be more than 512 bytes.
 */
typedef struct {
	uint8_t  digest[16];				/* +0  */
	uint32_t created;				/* +16 assumes sizeof time_t == 4 */
	uint32_t touched;				/* +20 assumes sizeof time_t == 4 */
	uint32_t expires;				/* +24 assumes sizeof time_t == 4 */
	uint32_t hits;					/* +28 assumes sizeof int == 4 */
	uint16_t key_size;				/* +32 assumes sizeof short == 2 */
	uint8_t  value_size;				/* +34 */
	uint8_t  command;				/* +35 */
	uint8_t  key_data[MCC_MAX_KEY_SIZE];		/* +36 */
	uint8_t  value_data[MCC_MAX_VALUE_SIZE];
} mcc_row;

#define MCC_SQL_BEGIN		\
"BEGIN;"

#define MCC_SQL_COMMIT		\
"COMMIT;"

#define MCC_SQL_ROLLBACK	\
"ROLLBACK;"

#define MCC_SQL_TABLE_EXISTS	\
"SELECT name FROM sqlite_master WHERE type='table' AND name='mcc';"

#define MCC_SQL_CREATE_TABLE	\
"CREATE TABLE mcc( k VARCHAR(383) PRIMARY KEY, d VARCHAR(92), h INTEGER DEFAULT 1, c INTEGER, t INTEGER, e INTEGER );"

#define MCC_SQL_INDEX_EXISTS	\
"SELECT name FROM sqlite_master WHERE type='index' AND name='mcc_expire';"

#define MCC_SQL_CREATE_INDEX	\
"CREATE INDEX mcc_expire ON mcc(e);"

#define MCC_SQL_SELECT_ONE	\
"SELECT k,d,h,c,t,e FROM mcc WHERE k=?1;"

#define MCC_SQL_EXPIRE		\
"DELETE FROM mcc WHERE e<=?1;"

#define MCC_SQL_DELETE		\
"DELETE FROM mcc WHERE k=?1;"

#define MCC_SQL_TRUNCATE	\
"DELETE FROM mcc;"

#define MCC_SQL_REPLACE		\
"INSERT OR REPLACE INTO mcc (k,d,h,c,t,e) VALUES(?1,?2,?3,?4,?5,?6);"

#define MCC_SQL_PRAGMA_SYNC_OFF		\
"PRAGMA synchronous = OFF;"

#define MCC_SQL_PRAGMA_SYNC_NORMAL	\
"PRAGMA synchronous = NORMAL;"

#define MCC_SQL_PRAGMA_SYNC_FULL	\
"PRAGMA synchronous = FULL;"

typedef struct {
	int port;
	volatile int is_running;
	Socket2 *socket;
#ifdef HAVE_PTHREAD_CREATE
	pthread_t thread;
#endif
} mcc_network;

typedef struct mcc mcc_handle;
typedef struct mcc mcc_context;
typedef int (*mcc_hook)(mcc_context *, void *data);
typedef int (*mcc_hook_row)(mcc_context *, void *data, mcc_row *old_row, mcc_row *new_row);

typedef struct {
	void *data;
	mcc_hook expire;		/* mccStartGc, mccExpireRows */
	mcc_hook prepare;		/* mccCreate, mcc_sql_recreate */
	mcc_hook finalize;		/* mccDestroy, mcc_sql_recreate */
	mcc_hook_row remote_remove;	/* mcc_listener_thread */
	mcc_hook_row remote_replace;	/* mcc_listener_thread */
} mcc_hooks;

struct mcc {
	int flags;
	char *path;
	char *secret;
	size_t secret_length;
	mcc_network unicast;
	mcc_network multicast;
	SocketAddress **unicast_ip;
	SocketAddress *multicast_ip;
	unsigned ttl;
	sqlite3 *db;
	sqlite3_stmt *select_one;
	sqlite3_stmt *select_all;
	sqlite3_stmt *truncate;
	sqlite3_stmt *replace;
	sqlite3_stmt *remove;
	sqlite3_stmt *expire;
	sqlite3_stmt *begin;
	sqlite3_stmt *commit;
	sqlite3_stmt *rollback;

	mcc_hooks hook;
#ifdef HAVE_PTHREAD_MUTEX_T
	time_t gc_next;
	pthread_t gc_thread;
	pthread_mutex_t mutex;
#endif
};

typedef struct {
	mcc_handle *mcc;
	mcc_network *listener;
} mcc_listener;

#ifdef NOT_READY
typedef struct mcc_status_t mcc_status;

struct mcc_status_t {
	mcc_status prev;
	mcc_status next;
	time_t last_packet;
	SocketAddress *host;
};
#endif

extern void mccDestroy(void *mcc);
extern void mccSetDebug(int level);
extern void mccSetOnCorrupt(int level);
extern mcc_handle *mccCreate(const char *path, int flags, mcc_hooks *hooks);
extern int mccSetSecret(mcc_handle *mcc, const char *secret);
extern int mccSetSync(mcc_handle *mcc, int level);
extern int mccSetSyncByName(mcc_handle *mcc, const char *name);
extern int mccGetRow(mcc_handle *mcc, mcc_row *row);
extern int mccGetKey(mcc_handle *mcc, const unsigned char *key, unsigned length, mcc_row *row);
extern int mccDeleteRow(mcc_handle *mcc, mcc_row *row);
extern int mccDeleteKey(mcc_handle *mcc, const unsigned char *key, unsigned length);
extern int mccPutRow(mcc_handle *mcc, mcc_row *row);
extern int mccPutRowLocal(mcc_handle *mcc, mcc_row *row, int touch);
extern int mccExpireRows(mcc_handle *mcc, time_t *when);
extern int mccDeleteAll(mcc_handle *mcc);

extern int mccSqlStep(mcc_handle *mcc, sqlite3_stmt *sql_stmt, const char *sql_stmt_text);

extern void mccStopGc(mcc_handle *mcc);
extern int mccStartGc(mcc_handle *mcc, unsigned seconds);

extern void mccStopMulticast(mcc_handle *mcc);
extern int mccSetMulticastTTL(mcc_handle *mcc, int ttl);
extern int mccStartMulticast(mcc_handle *mcc, const char *ip_group, int port);

extern void mccStopUnicast(mcc_handle *mcc);
extern int mccStartUnicast(mcc_handle *mcc, const char **ip_array, int port);
#if !defined(ENABLE_PDQ)
extern int mccStartUnicastDomain(mcc_handle *mcc, const char *mx_domain, int port);
#endif

extern void mccAtForkPrepare(mcc_handle *mcc);
extern void mccAtForkParent(mcc_handle *mcc);
extern void mccAtForkChild(mcc_handle *mcc);


#endif /* HAVE_SQLITE3_H */

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_mcc_h__ */

