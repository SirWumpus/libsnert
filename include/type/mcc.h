/*
 * mcc.h
 *
 * Multicast / Unicast Cache
 *
 * Copyright 2006, 2012 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_mcc_h__
#define __com_snert_lib_type_mcc_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/sqlite3.h>

#ifndef MCC_STACK_SIZE
# define MCC_STACK_SIZE		(64 * 1024)
#endif
#if MCC_STACK_SIZE < PTHREAD_STACK_MIN
# undef MCC_STACK_SIZE
# define MCC_STACK_SIZE		PTHREAD_STACK_MIN
#endif

/*
 * Must be a power of two.
 */
#ifndef MCC_HASH_TABLE_SIZE
#define MCC_HASH_TABLE_SIZE	512
#endif

#ifndef MCC_MAX_LINEAR_PROBE
#define MCC_MAX_LINEAR_PROBE	16
#endif


#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#ifdef HAVE_SQLITE3_H

# if SQLITE_VERSION_NUMBER < 3003008
#  error "Thread safe SQLite3 version 3.3.8 or better required."
# endif

typedef enum {
	MCC_OK		= 0,
	MCC_ERROR	= -1,
	MCC_NOT_FOUND	= -2,
} mcc_return;

#define MCC_PACKET_SIZE		512
#define MCC_DATA_SIZE		(MCC_PACKET_SIZE - MCC_HEAD_SIZE)

#define MCC_HEAD_SIZE		24
#define MCC_PACKET_LENGTH(p)	(MCC_HEAD_SIZE + MCC_GET_K_SIZE(p) + MCC_GET_V_SIZE(p))

/*
 * A multicast cache packet cannot be more than 512 bytes.
 */
typedef struct {
	/* Packet data. */
	uint8_t  digest[16];				/* +0  */
	uint32_t ttl;					/* +16 time-to-live relative a host's system clock */
	uint16_t k_size;				/* +20 command & key size: cccc ccc k kkkk kkkk */
	uint16_t v_size;				/* +22 zero & value size : 0000 000 v vvvv vvvv */
	uint8_t  data[MCC_DATA_SIZE];			/* +24 = MCC_HEAD_SIZE */

	/* Not part of the packet. */
	time_t created;
	time_t expires;
} mcc_row;

#define MCC_MASK_SIZE		0x01FF
#define MCC_MASK_EXTRA		0xFE00

#define MCC_SET_K_SIZE(p, s)	(p)->k_size = ((p)->k_size & MCC_MASK_EXTRA) | ((s) & MCC_MASK_SIZE)
#define MCC_SET_V_SIZE(p, s)	(p)->v_size = ((p)->v_size & MCC_MASK_EXTRA) | ((s) & MCC_MASK_SIZE)

#define MCC_GET_K_SIZE(p)	((p)->k_size & MCC_MASK_SIZE)
#define MCC_GET_V_SIZE(p)	((p)->v_size & MCC_MASK_SIZE)
#define MCC_GET_V_SPACE(p)	(MCC_DATA_SIZE - MCC_GET_K_SIZE(p))

#define MCC_SET_COMMAND(p, c)	(p)->k_size = ((c) << 9) | MCC_GET_K_SIZE(p)
#define MCC_GET_COMMAND(p)	((p)->k_size >> 9)
#define MCC_SET_EXTRA(p, c)	(p)->v_size = ((c) << 9) | MCC_GET_V_SIZE(p)
#define MCC_GET_EXTRA(p)	((p)->v_size >> 9)

#define MCC_PTR_K(x)		((x)->data)
#define MCC_PTR_V(x)		((x)->data + MCC_GET_K_SIZE(x))

#define MCC_FMT_K		"%.*s"
#define MCC_FMT_K_ARG(p)	MCC_GET_K_SIZE(p), MCC_PTR_K(p)
#define MCC_FMT_V		"%.*s"
#define MCC_FMT_V_ARG(p)	MCC_GET_V_SIZE(p), MCC_PTR_V(p)

#define MCC_SQL_CREATE_TABLE	\
"CREATE TABLE mcc( k TEXT PRIMARY KEY, v TEXT, e INTEGER, c INTEGER DEFAULT (strftime('%s', 'now')) );"

#define MCC_SQL_REPLACE		\
"INSERT OR REPLACE INTO mcc (k,v,e) VALUES(?1,?2,?3);"

#define MCC_SQL_BEGIN		\
"BEGIN IMMEDIATE;"

#define MCC_SQL_COMMIT		\
"COMMIT;"

#define MCC_SQL_ROLLBACK	\
"ROLLBACK;"

#define MCC_SQL_SELECT_ONE	\
"SELECT * FROM mcc WHERE k=?1;"

#define MCC_SQL_TABLE_EXISTS	\
"SELECT name FROM sqlite_master WHERE type='table' AND name='mcc';"

#define MCC_SQL_INDEX_EXISTS	\
"SELECT name FROM sqlite_master WHERE type='index' AND name='mcc_expire';"

#define MCC_SQL_CREATE_INDEX	\
"CREATE INDEX mcc_expire ON mcc(e);"

#define MCC_SQL_EXPIRE		\
"DELETE FROM mcc WHERE e<=?1;"

#define MCC_SQL_DELETE		\
"DELETE FROM mcc WHERE k=?1;"

#define MCC_SQL_TRUNCATE	\
"DELETE FROM mcc;"

#define MCC_SQL_PRAGMA_SYNC_OFF		\
"PRAGMA synchronous = OFF;"

#define MCC_SQL_PRAGMA_SYNC_NORMAL	\
"PRAGMA synchronous = NORMAL;"

#define MCC_SQL_PRAGMA_SYNC_FULL	\
"PRAGMA synchronous = FULL;"

/***********************************************************************
 *** Global Operations
 ***********************************************************************/

typedef struct mcc_ctx mcc_handle;
typedef struct mcc_ctx mcc_context;
typedef int (*mcc_hook)(mcc_context *, void *data);
typedef int (*mcc_hook_row)(mcc_context *, void *data, mcc_row *old_row, mcc_row *new_row);

typedef struct mcc_key_hook mcc_key_hook;
typedef void (*mcc_key_process)(mcc_context *, mcc_key_hook *, const char *ip, mcc_row *row_received);
typedef void (*mcc_key_cleanup)(void *data);

/*
 * Special hook definition applied by the mcc listener threads for MCC_CMD_OTHER.
 */
struct mcc_key_hook {
	void *data;
	const char *prefix;
	size_t prefix_length;
	mcc_key_process process;
	mcc_key_cleanup cleanup;
};

typedef struct {
	mcc_hook expire;		/* mccStartGc, mccExpireRows */
	mcc_hook prepare;		/* mccCreate, mcc_sql_recreate */
	mcc_hook finalize;		/* mccDestroy, mcc_sql_recreate */
	mcc_hook_row remote_remove;	/* mcc_listener_thread */
	mcc_hook_row remote_replace;	/* mcc_listener_thread */
} mcc_hooks;

#define MCC_WINDOW_SIZE		60				/* window in seconds */
#define	MCC_INTERVALS		10				/* ticks per window */
#define MCC_TICK		(MCC_WINDOW_SIZE/MCC_INTERVALS)	/* seconds per tick */

typedef struct {
	unsigned long ticks;
	unsigned long count;
} mcc_interval;

typedef struct mcc_string {
	struct mcc_string *next;
	char *string;
} mcc_string;

typedef struct {
	time_t touched;
	unsigned long max_ppm;
	char ip[IPV6_STRING_LENGTH];
	mcc_interval intervals[MCC_INTERVALS];
	mcc_string *notes;
} mcc_active_host;

#define MCC_ON_CORRUPT_EXIT			0
#define MCC_ON_CORRUPT_RENAME			1
#define MCC_ON_CORRUPT_REPLACE			2

extern void mccSetOnCorrupt(int level);

extern void mccSetDebug(int level);
extern int mccSetSecret(const char *secret);

extern int mccInit(const char *path, mcc_hooks *hooks);
extern void mccFini(void);

extern void mccStopGc(void);
extern int mccStartGc(unsigned seconds);

typedef struct {
	char *path;
	char *secret;
	size_t secret_length;
	pthread_mutex_t mutex;

	mcc_hooks hook;
	Vector key_hooks;

	Socket2 *server;
	pthread_t listener;
	volatile int is_running;
	SocketAddress **unicast_ip;

	time_t gc_next;
	unsigned gc_period;
	pthread_t gc_thread;

	pthread_mutex_t active_mutex;
	mcc_active_host active[MCC_HASH_TABLE_SIZE];
} mcc_data;

extern int mccSetMulticastTTL(int ttl);

/**
 * @param ip_array
 *	One or more multicast and/or unicast IP addresses.
 *
 * @param port
 *	The port to listen on for broadcasts.
 *
 * @return 
 *	MCC_OK or MCC_ERROR.
 */
extern int mccStartListener(const char **ip_array, int port);

/**
 * Stop the listener thread.
 */
extern void mccStopListener(void);

extern Vector mccGetActive(void);
extern mcc_active_host *mccFindActive(const char *ip);
extern void mccUpdateActive(const char *ip, uint32_t *touched);
extern unsigned long mccGetRate(mcc_interval *intervals, unsigned long ticks);
extern unsigned long mccUpdateRate(mcc_interval *intervals, unsigned long ticks);
extern int mccRegisterKey(mcc_key_hook *tag_hook);

extern void mccStringFree(void *_note);
extern void mccStringReplace(mcc_string *note, const char *str);
extern mcc_string *mccStringCreate(const char *str);
extern mcc_string *mccNotesFind(mcc_string *notes, const char *substring);

extern void mccNotesUpdate(const char *ip, const char *find, const char *text);
extern void mccNotesFree(mcc_string *notes);

extern void mccAtForkPrepare(void);
extern void mccAtForkParent(void);
extern void mccAtForkChild(void);

/***********************************************************************
 *** Per Thread Operations
 ***********************************************************************/

struct mcc_ctx {
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
	int is_transaction;
	void *data;
};

#define MCC_SYNC_OFF				0
#define MCC_SYNC_NORMAL				1
#define MCC_SYNC_FULL				2

extern int mccSetSync(mcc_handle *mcc, int level);

#define MCC_CMD_ADD		'a'
#define MCC_CMD_DEC		'd'
#define MCC_CMD_INC		'i'
#define MCC_CMD_PUT		'p'
#define MCC_CMD_REMOVE		'r'
#define MCC_CMD_OTHER		'?'

extern int mccSend(mcc_handle *mcc, mcc_row *row, uint8_t command);

extern mcc_handle *mccCreate(void);
extern void mccDestroy(void *mcc);

extern int mccSetSyncByName(mcc_handle *mcc, const char *name);
extern int mccExpireRows(mcc_handle *mcc, time_t *when);
extern int mccSqlStep(mcc_handle *mcc, sqlite3_stmt *sql_stmt, const char *sql_stmt_text);

extern void mccSetExpires(mcc_row *row, unsigned long ttl);
extern int mccSetKey(mcc_row *row, const char *fmt, ...);
extern int mccSetValue(mcc_row *row, const char *fmt, ...);

extern int mccAddRow(mcc_handle *mcc, long add, mcc_row *row);
extern int mccAddRowLocal(mcc_handle *mcc, long add, mcc_row *row);

extern int mccDeleteAll(mcc_handle *mcc);
extern int mccDeleteRow(mcc_handle *mcc, mcc_row *row);
extern int mccDeleteRowLocal(mcc_handle *mcc, mcc_row *row);
extern int mccDeleteKey(mcc_handle *mcc, const unsigned char *key, unsigned length);

extern int mccGetRow(mcc_handle *mcc, mcc_row *row);
extern int mccGetKey(mcc_handle *mcc, const unsigned char *key, unsigned length, mcc_row *row);

extern int mccPutRow(mcc_handle *mcc, mcc_row *row);
extern int mccPutRowLocal(mcc_handle *mcc, mcc_row *row);
extern int mccPutKeyValue(mcc_handle *mcc, const char *key, const char *value, unsigned long ttl);

#endif /* HAVE_SQLITE3_H */

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_mcc_h__ */

