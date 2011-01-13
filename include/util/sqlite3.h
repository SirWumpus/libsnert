/*
 * sqlite3.h
 *
 * Sqlite3 Support Functions
 *
 * Copyright 2011 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_util_sqlite3_h__
#define __com_snert_lib_util_sqlite3_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_SQLITE3_H

/***********************************************************************
 ***
 ***********************************************************************/

#include <sqlite3.h>

extern int sqlite3_prepare_v2_blocking(sqlite3 *db, const char *sql, int sql_len, sqlite3_stmt **stmt, const char **stop);
extern int sqlite3_step_blocking(sqlite3_stmt *stmt);

/***********************************************************************
 ***
 ***********************************************************************/

#endif /* HAVE_SQLITE3_H */

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_sqlite3_h__ */
