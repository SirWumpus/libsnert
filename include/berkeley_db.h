/*
 * berkeley_db.h
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_berkeley_db_h__
#define __com_snert_lib_berkeley_db_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/version.h>

#if defined(HAVE_DB_H)
# include <db.h>
#endif

#if   DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR >= 1
# define DBTXN          (DB_TXN *) 0,
/* DB->associate, open, rename, remove require DBTXN *. */
# define DBTXN_ROAR     (DB_TXN *) 0,
#elif   DB_VERSION_MAJOR == 4 && DB_VERSION_MINOR == 0
# define DBTXN          (DB_TXN *) 0,
/* DB->associate, open, rename, remove do not require DBTXN *. */
# define DBTXN_ROAR
#elif DB_VERSION_MAJOR == 3
# define DBTXN
# define DBTXN_ROAR
#elif DB_VERSION_MAJOR == 2
# error "Berkeley DB 2.x not supported."
#elif defined(HAVE_DBOPEN)
# define DB_VERSION_MAJOR		1
# define DB_VERSION_MINOR		85
# define DB_NOTFOUND     		1
# define db_strerror(x)  		strerror(errno)
# include <fcntl.h>
#endif

#if defined(HAVE_DBOPEN)
typedef enum { DB185_BTREE, DB185_HASH, DB185_RECNO } DB185_TYPE;

# if defined(HAVE_DB_CREATE)
extern DB *dbopen(const char *, int, int, DB185_TYPE, const void *);
# endif

/* Routine flags. */
#define R_CURSOR        1               /* del, put, seq */
#define __R_UNUSED      2               /* UNUSED */
#define R_FIRST         3               /* seq */
#define R_IAFTER        4               /* put (RECNO) */
#define R_IBEFORE       5               /* put (RECNO) */
#define R_LAST          6               /* seq (BTREE, RECNO) */
#define R_NEXT          7               /* seq */
#define R_NOOVERWRITE   8               /* put */
#define R_PREV          9               /* seq (BTREE, RECNO) */
#define R_SETCURSOR     10              /* put (RECNO) */
#define R_RECNOSYNC     11              /* sync (RECNO) */

/*
 * Note that DBT 4.4 has the same initial structure as 1.85.
 */
typedef struct dbt_185 {
        void    *data;                  /* data */
        size_t   size;                  /* data length */
} DBT185;

typedef struct db_185 {
        DB185_TYPE type;
        int (*close)(struct db_185 *);
        int (*del)(const struct db_185 *, const DBT185 *, unsigned int);
        int (*get)(const struct db_185 *, const DBT185 *, DBT185 *, unsigned int);
        int (*put)(const struct db_185 *, DBT185 *, const DBT185 *, unsigned int);
        int (*seq)(const struct db_185 *, DBT185 *, DBT185 *, unsigned int);
        int (*sync)(const struct db_185 *, unsigned int);
        void *internal;                 	/* Access method private. */
        int (*fd)(const struct db_185 *);
} DB185;
#endif

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_berkeley_db_h__ */
