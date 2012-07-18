/*
 * sift.c
 *
 * Generic Log Trawler, Tracking, and Reporting
 *
 * Copyright 2012 by Anthony Howe.  All rights reserved.
 */

#ifndef _NAME
#define _NAME			"sift"
#endif

#ifndef LINE_SIZE
#define LINE_SIZE		1024
#endif

#ifndef DB_PATH_FMT
#define DB_PATH_FMT		"/tmp/sift-%d.sq3"
#endif

#ifndef DB_BUSY_MS
#define DB_BUSY_MS		15000
#endif

#ifndef REGEX_PARENS_SIZE
#define REGEX_PARENS_SIZE	10
#endif

#ifndef TOKEN_DELIMITER
#define TOKEN_DELIMITER		' '
#endif

#ifndef GROW_SIZE
#define GROW_SIZE		20
#endif

#ifndef SMTP_CONNECT_TO
#define SMTP_CONNECT_TO		30
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_REGEX_H
# include <regex.h>
#endif
#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/type/list.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/sqlite3.h>
#include <com/snert/lib/util/convertDate.h>

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

#define CRLF		"\r\n"

#define CREATE_TABLES \
"CREATE TABLE patterns (" \
" pattern TEXT UNIQUE ON CONFLICT IGNORE" \
");" \
"CREATE TABLE log (" \
" id_pattern INTEGER," \
" created INTEGER," \
" thread TEXT," \
" line TEXT UNIQUE ON CONFLICT IGNORE" \
");" \
"CREATE INDEX log_thread ON log(thread);" \
"CREATE TABLE limits (" \
" id_pattern INTEGER," \
" created INTEGER," \
" expires INTEGER," \
" updated INTEGER DEFAULT 0," \
" reported INTEGER DEFAULT 0," \
" counter INTEGER DEFAULT 1," \
" token TEXT," \
" PRIMARY KEY(id_pattern, token)" \
");" \
"CREATE TABLE limit_to_log (" \
" id_limit INTEGER," \
" id_log INTEGER" \
");" \
"CREATE TABLE log_last_oid (" \
" id_log INTEGER" \
");" \
"INSERT INTO log_last_oid VALUES(0);" \
"CREATE TRIGGER log_save_oid AFTER INSERT ON log BEGIN" \
" UPDATE log_last_oid SET id_log = NEW.oid WHERE oid=1;" \
"END;" \
"CREATE TRIGGER limit_oninsert AFTER INSERT ON limits BEGIN"\
" INSERT INTO limit_to_log VALUES(NEW.oid, (SELECT id_log FROM log_last_oid WHERE oid=1));" \
"END;" \
"CREATE TRIGGER limit_onupdate AFTER UPDATE ON limits BEGIN"\
" INSERT INTO limit_to_log VALUES(OLD.oid, (SELECT id_log FROM log_last_oid WHERE oid=1));" \
"END;" 

#define TABLES_EXIST    \
"SELECT name FROM sqlite_master WHERE type='table' AND name='log';"

#define RESET_LIMITS	\
"UPDATE limits SET expires=1;"

#define INSERT_LOG	\
"INSERT INTO log VALUES(?1,?2,?3,?4);"

#define THREAD_LOG	\
"SELECT * FROM log WHERE thread=?1 ORDER BY created ASC;"

#define INSERT_LIMIT	\
"INSERT INTO limits VALUES(?1,?2,?3,0,0,1,?4);"

#define UPDATE_LIMIT	\
"UPDATE limits SET expires=?3, updated=?4, reported=?5, counter=?6 WHERE id_pattern=?1 AND token=?2;"

#define SELECT_LIMIT	\
"SELECT oid,* FROM limits WHERE id_pattern=?1 AND token=?2;"

#define INCREMENT_LIMIT \
"UPDATE limits SET counter=counter+1 WHERE id_pattern=?1 AND token=?2;"

#define INSERT_PATTERN	\
"INSERT INTO patterns VALUES(?1);"

#define SELECT_PATTERN	\
"SELECT * FROM patterns WHERE oid=?1;"

#define FIND_PATTERN	\
"SELECT oid FROM patterns WHERE pattern=?1;"

#define SELECT_LIMIT_TO_LOG \
"SELECT line FROM limit_to_log, log WHERE limit_to_log.id_limit=?1 AND log.created>?2 AND limit_to_log.id_log=log.oid;"

struct sift_ctx {
	sqlite3 *db;
	sqlite3_stmt *insert_log;
	sqlite3_stmt *thread_log;
	sqlite3_stmt *insert_limit;
	sqlite3_stmt *update_limit;
	sqlite3_stmt *select_limit;
	sqlite3_stmt *increment_limit;
	sqlite3_stmt *insert_pattern;
	sqlite3_stmt *select_pattern;
	sqlite3_stmt *find_pattern;
	sqlite3_stmt *select_limit_to_log;
};

struct pattern_rule {
	ListItem node;	
	char *pattern;
	char *report;		/* NULL or r= . */
	char *command;		/* NULL or c= . */
	char **limits;		/* NULL or NULL terminated array of l= strings. */
	long thread;		/* -1 or index */
	regex_t re;
	sqlite3_int64 id_pattern;
};

struct limit {
	sqlite3_int64 id_limit;
	sqlite3_int64 id_pattern;
	const char *token;
	int token_length;
	time_t created;
	time_t expires;
	time_t updated;
	int reported;
	int counter;
};

static int debug;
static int assumed_tz;
static int assumed_year;

static int follow_flag;

static Vector report;
static const char *report_to;
static const char *report_from;

static struct sift_ctx ctx;
static int db_reset;
static int db_delete;
static char *db_path;
static char db_user[32];

static List pattern_rules;
static const char *pattern_path;

static const char *smtp_host = "127.0.0.1:25";

static const char options[] = "fF:vDRd:r:s:y:z:";
static char usage[] =
"usage: sift [-fvD][-d db_path][-F mail][-r mail,...][-s host:ip][-y year]\n"
"            [-z gmtoff] pattern_file [log_file ...]\n"
"\n"
"-d filepath\tFile path of the sift database used for tracking and\n"
"\t\tcollecting threaded log lines. The default path is\n"
"\t\t/tmp/sift-$UID.sq3\n"
"\n"
"-D\t\tDelete the database before processing the log-files.\n"
"\n"
"-f\t\tFollow forever. Detects log file rotation based on size\n"
"\t\tshrinkage, inode, or device number change and reopens\n"
"\t\tlog-file. Can only be used with one log-file argument.\n"
"\n"
"-F from\t\tThe From: address for sending reports; default postmaster.\n"
"\n"
"-r mail,...\tA list of one or more mail addresses to send limit exceeded\n"
"\t\treports to. Individual pattern rules can override this with\n"
"\t\tr= action. If not specified, then no reports are sent.\n"
"\n"
#ifdef NOT_YET
"-R\t\tReset the database before processing the log-files.\n"
"\n"
#endif
"-s host:ip\tThe SMTP smart host to use for sending reports. The default\n"
"\t\tis \"127.0.0.1:25\".\n"
"\n"
"-v\t\tVerbose and/or debug information to standard output. -v write\n"
"\t\tcopy of limit exceeded reports to standard output; -vv include\n"
"\t\tSMTP debug; -vvv include regular expression debug.\n"
"\n"
"-y year\t\tYear when log_file was create. Default current year.\n"
"\n"
"-z gmtoff\tGMT offset when log_file was created. Default current time zone.\n"
"\n"
"One or more log files can be examined. When none are specified, then\n"
"standard input is used. When using -f only one log file may be given\n"
"or standard input used.\n"
"\n"
"The pattern-file is a text file containing one or more pattern rules,\n"
"blank lines, and/or comment lines that begin with a hash (#). The format\n"
"of a pattern rule line is:\n"
"\n"
"\t/RE/\taction [; action ...]\n"
"\n"
"RE is a POSIX extended regular expression. The actions are a semi-colon\n"
"separated list of key-value pairs. The following actions are possible:\n"
"\n"
"c=\"shell command\"\n"
"\tIf a limit is exceeded, execute the given shell command. Within\n"
"\tthe command string, instances of \"#N\" are replaced by the Nth\n"
"\tsub-expression found in pattern.\n"
"\n"
"l=index[,index ...],max/time[unit]\n"
"\tSpecifies one or more indices of sub-expressions for which this\n"
"\tlimit applies. The value max is an upper limit (inclusive) over\n"
"\ta time period (default seconds). The optional time unit can be a\n"
"\tsingle letter for (s)econds, (m)inutes, (h)ours, (d)ays, or\n"
"\t(w)eeks.\n"
"\n"
"\tThere can be multiple l= actions, but a sub-expression index can\n"
"\tonly be referenced by one l= action for any given pattern rule.\n"
"\tHowever, across pattern rules the same token (eg. an IP address,\n"
"\thost name, mail address, phrase) matched by sub-expressions can\n"
"\thave many different limits.\n"
"\n"
"r=mail,...\n"
"\tA comma separate list of one or more mail addresses to which a\n"
"\tpattern rule limit is reported. Overrides the global -r option\n"
"\tonly for the patten rule in question.\n"
"\n"
"t=index\n"
"\tIndex number of a sub-expression found in pattern used to thread\n"
"\trelated log lines.\n"
"\n"
"sift/1.0 Copyright 2012 by Anthony Howe. All rights reserved.\n"
;

/***********************************************************************
 *** 
 ***********************************************************************/

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

static void
sql_error(const char *file, int line, sqlite3_stmt *stmt)
{
	(void) fprintf(
		stderr, "%s/%d: %s (%d): %s\n", file, line, 
		sqlite3_errmsg(ctx.db), sqlite3_errcode(ctx.db), 
		sqlite3_sql(stmt)
	);
        (void) sqlite3_clear_bindings(stmt);
}

static int
sql_step(sqlite3 *db, sqlite3_stmt *sql_stmt)
{
        int rc;

//	PTHREAD_DISABLE_CANCEL();

        (void) sqlite3_busy_timeout(db, DB_BUSY_MS);
        rc = sqlite3_step_blocking(sql_stmt);

        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
//		(void) fprintf(stderr, "db step error: %s (%d)\n", sqlite3_errmsg(db), rc);
                if (rc == SQLITE_CORRUPT || rc == SQLITE_CANTOPEN) 
			abort();
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

//	PTHREAD_RESTORE_CANCEL();

        return rc;
}

static int
sql_count(void *data, int ncolumns, char **col_values, char **col_names)
{
        (*(int*) data)++;
        return 0;
}

static void
fini_db(void)
{
        if (ctx.insert_log != NULL) 
                (void) sqlite3_finalize(ctx.insert_log);
        if (ctx.thread_log != NULL) 
                (void) sqlite3_finalize(ctx.thread_log);

        if (ctx.insert_limit != NULL) 
                (void) sqlite3_finalize(ctx.insert_limit);
        if (ctx.update_limit != NULL) 
                (void) sqlite3_finalize(ctx.update_limit);
        if (ctx.select_limit != NULL) 
                (void) sqlite3_finalize(ctx.select_limit);
        if (ctx.increment_limit != NULL) 
                (void) sqlite3_finalize(ctx.increment_limit);

        if (ctx.insert_pattern != NULL) 
                (void) sqlite3_finalize(ctx.insert_pattern);
        if (ctx.select_pattern != NULL) 
                (void) sqlite3_finalize(ctx.select_pattern);
        if (ctx.find_pattern != NULL) 
                (void) sqlite3_finalize(ctx.find_pattern);

        if (ctx.select_limit_to_log != NULL) 
                (void) sqlite3_finalize(ctx.select_limit_to_log);

	sqlite3_close(ctx.db);

	memset(&ctx, 0, sizeof (ctx));
}

static int
init_db(const char *path)
{
	int count;
	char *error;
	const char *stop;

        if (sqlite3_open(path, &ctx.db) != SQLITE_OK) {
		(void) fprintf(stderr, "%s: %s\n", path, sqlite3_errmsg(ctx.db));
		goto error0;
        }

        count = 0;
        if (sqlite3_exec(ctx.db, TABLES_EXIST, sql_count, &count, &error) != SQLITE_OK) {
		(void) fprintf(stderr, "%s: %s\n", path, error);
		sqlite3_free(error);
		goto error1;
	}

        if (count != 1 && sqlite3_exec(ctx.db, CREATE_TABLES, NULL, NULL, &error) != SQLITE_OK) {
		(void) fprintf(stderr, "%s: %s\n", path, error);
		sqlite3_free(error);
		goto error1;
	}

        if (db_reset && sqlite3_exec(ctx.db, RESET_LIMITS, NULL, NULL, &error) != SQLITE_OK) {
		(void) fprintf(stderr, "%s: %s\n", path, error);
		sqlite3_free(error);
		goto error1;
	}

	if (sqlite3_prepare_v2_blocking(ctx.db, INSERT_LOG, -1, &ctx.insert_log, &stop) != SQLITE_OK)
		goto error2;
	if (sqlite3_prepare_v2_blocking(ctx.db, THREAD_LOG, -1, &ctx.thread_log, &stop) != SQLITE_OK)
		goto error2;

	if (sqlite3_prepare_v2_blocking(ctx.db, INSERT_LIMIT, -1, &ctx.insert_limit, &stop) != SQLITE_OK)
		goto error2;
	if (sqlite3_prepare_v2_blocking(ctx.db, UPDATE_LIMIT, -1, &ctx.update_limit, &stop) != SQLITE_OK)
		goto error2;
	if (sqlite3_prepare_v2_blocking(ctx.db, SELECT_LIMIT, -1, &ctx.select_limit, &stop) != SQLITE_OK)
		goto error2;
	if (sqlite3_prepare_v2_blocking(ctx.db, INCREMENT_LIMIT, -1, &ctx.increment_limit, &stop) != SQLITE_OK)
		goto error2;

	if (sqlite3_prepare_v2_blocking(ctx.db, INSERT_PATTERN, -1, &ctx.insert_pattern, &stop) != SQLITE_OK)
		goto error2;
	if (sqlite3_prepare_v2_blocking(ctx.db, SELECT_PATTERN, -1, &ctx.select_pattern, &stop) != SQLITE_OK)
		goto error2;
	if (sqlite3_prepare_v2_blocking(ctx.db, FIND_PATTERN, -1, &ctx.find_pattern, &stop) != SQLITE_OK)
		goto error2;

	if (sqlite3_prepare_v2_blocking(ctx.db, SELECT_LIMIT_TO_LOG, -1, &ctx.select_limit_to_log, &stop) != SQLITE_OK)
		goto error2;

	return 0;
error2:
	(void) fprintf(stderr, "%s/%d: %s: %s\n", __FILE__, __LINE__, sqlite3_errmsg(ctx.db), TextEmpty(stop));
error1:
	sqlite3_free(error);
	fini_db();
error0:
	return -1;
}

static void
free_rule(void *_rule)
{
	struct pattern_rule *rule = _rule;

	if (rule != NULL) {
		free(rule->pattern);
		free(rule->command);
		free(rule->limits);
		regfree(&rule->re);
		free(rule);
	}
}

/*
 * Pattern Rule Grammar
 * --------------------
 * 
 * line := blank | "#" comment | rule
 * 
 * rule := pattern *[ whitespace ] actions
 * 
 * whitespace := SPACE | TAB
 * 
 * pattern := "/" extended_regex "/"
 * 
 * actions := action [ ";" actions ]
 * 
 * action := thread | limit | report | command
 * 
 * thread := "t=" index
 * 
 * limit := "l=" 1*[ index "," ] max "/" period [ unit ]
 * 
 * report := "r=" mail *[ "," mail ]
 * 
 * command := "c=" quoted_shell_command
 * 
 */
static int
init_rule(char *line)
{
	Vector fields;
	const char *next;
	struct pattern_rule *rule;
	int i, rc = -1, err;
	char *field, error[128];

	if (*line != '/')
		goto error0;
	if ((rule = calloc(1, sizeof (*rule))) == NULL)
		goto error0;
	if ((rule->pattern = TokenNext(line, &next, "/", TOKEN_KEEP_ESCAPES)) == NULL) 
		goto error1;
	if ((fields = TextSplit(next, ";", TOKEN_KEEP_ESCAPES)) == NULL)
		goto error1;

	if ((err = regcomp(&rule->re, rule->pattern, REG_EXTENDED|REG_NEWLINE)) != 0) {
		(void) regerror(err, &rule->re, error, sizeof (error));
		(void) fprintf(stderr, "pattern /%s/: %s (%d)\n", rule->pattern, error, err);
		goto error2;
	}

	/* Assume new previously unknown pattern. */
	if ((err = sqlite3_bind_text(ctx.insert_pattern, 1, rule->pattern, -1, SQLITE_STATIC)) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_pattern);
		goto error2;
	}
	if ((err = sql_step(ctx.db, ctx.insert_pattern)) != SQLITE_DONE) {
		sql_error(__FILE__, __LINE__, ctx.insert_pattern);
		goto error2;	
	}

	rule->id_pattern = sqlite3_last_insert_rowid(ctx.db);
	(void) sqlite3_clear_bindings(ctx.insert_pattern);
	sqlite3_reset(ctx.insert_pattern);

	/* Last insert rowid is zero if no row was inserted, thus it
	 * already exists and we need to find the pattern number (OID).
	 */
	if (rule->id_pattern == 0) {
		if ((err = sqlite3_bind_text(ctx.find_pattern, 1, rule->pattern, -1, SQLITE_STATIC)) != SQLITE_OK) {
			sql_error(__FILE__, __LINE__, ctx.find_pattern);
			goto error2;
		}
		if ((err = sql_step(ctx.db, ctx.find_pattern)) != SQLITE_ROW) {
			sql_error(__FILE__, __LINE__, ctx.find_pattern);
			goto error2;	
		}
		if ((rule->id_pattern = sqlite3_column_int64(ctx.find_pattern, 0)) == 0) {
			sql_error(__FILE__, __LINE__, ctx.find_pattern);
			goto error2;	
		}
		(void) sqlite3_clear_bindings(ctx.find_pattern);
                sqlite3_reset(ctx.find_pattern);
	}

	for (i = 0; i < VectorLength(fields); i++) {
		field = VectorGet(fields, i);
		field += strspn(field, " \t");

		switch (*field) {
		case 'c': 
			if (field[1] == '=') {
				free(rule->command);
				rule->command = VectorReplace(fields, i, NULL);
			}
			break;
		case 'r': 
			if (field[1] == '=') {
				free(rule->report);
				rule->report = VectorReplace(fields, i, NULL);
			}
			break;
		case 't': 
			if (field[1] == '=')
				rule->thread = strtol(field+2, NULL, 10);
			break;
		case 'l':
			if (field[1] == '=')
				continue;
		}

		(void) VectorRemove(fields, i--);
	}

	field[strcspn(field, "\r\n")] = '\0';

	/* What remains of fields should be an empty vector or an array of l= actions. */
	rule->limits = (char **) VectorBase(fields);
	fields = NULL;

	rule->node.data = rule;
	rule->node.free = free_rule;
	listInsertAfter(&pattern_rules, NULL, &rule->node);

	rc = 0;
error2:
	VectorDestroy(fields);
error1:
	if (rc != 0)
		free_rule(rule);
error0:
	return rc;
}

static int
init_rules(const char *path)
{
	int rc = -1;
	FILE *fp;
	char line[LINE_SIZE];

	if ((fp = fopen(path, "r")) == NULL) {
		(void) fprintf(stderr, "%s: %s (%d)\n", path, strerror(errno), errno);
		goto error0;
	}

	pattern_path = path;
	listInit(&pattern_rules);

	while (fgets(line, sizeof (line), fp) != NULL) {
		/* Ignore all lines except those starting with a pattern. */
		if (*line == '/') {
			if (init_rule(line))
				goto error1;
		}
	}

	rc = 0;
error1:
	(void) fclose(fp);
error0:
	return rc;
}

static void
limit_select(sqlite3_int64 id_pattern, const char *token, int length, struct limit *limit)
{
	/* Set defaults in case there is no existing limit. */
	(void) memset(limit, 0, sizeof (*limit));
	limit->id_pattern = id_pattern;
	limit->token_length = length;
	limit->token = token;

	if (sqlite3_bind_int64(ctx.select_limit, 1, id_pattern) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.select_limit);
		return;
	}
	if (sqlite3_bind_text(ctx.select_limit, 2, token, length, SQLITE_STATIC) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.select_limit);
		return;
	}
	if (sql_step(ctx.db, ctx.select_limit) != SQLITE_ROW) 
		return;

	/* Pull out existing limit details. */
	limit->id_limit = sqlite3_column_int(ctx.select_limit, 0);
	limit->created = (time_t) sqlite3_column_int(ctx.select_limit, 2);
	limit->expires = (time_t) sqlite3_column_int(ctx.select_limit, 3);
	limit->updated = (time_t) sqlite3_column_int(ctx.select_limit, 4);
	limit->reported = sqlite3_column_int(ctx.select_limit, 5);
	limit->counter = sqlite3_column_int(ctx.select_limit, 6);

	(void) sqlite3_clear_bindings(ctx.select_limit);
	sqlite3_reset(ctx.select_limit);
}

static void
limit_insert(struct limit *limit)
{
	if (sqlite3_bind_int64(ctx.insert_limit, 1, limit->id_pattern) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_limit);
		return;
	}
	if (sqlite3_bind_int(ctx.insert_limit, 2, limit->created) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_limit);
		return;
	}
	if (sqlite3_bind_int(ctx.insert_limit, 3, limit->expires) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_limit);
		return;
	}
	if (sqlite3_bind_text(ctx.insert_limit, 4, limit->token, limit->token_length, SQLITE_STATIC) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_limit);
		return;
	}
	if (sql_step(ctx.db, ctx.insert_limit) != SQLITE_DONE) {
		sql_error(__FILE__, __LINE__, ctx.insert_limit);
		return;
	}
	(void) sqlite3_clear_bindings(ctx.insert_limit);
	sqlite3_reset(ctx.insert_limit);
}

static void
limit_update(struct limit *limit)
{
	if (sqlite3_bind_int64(ctx.update_limit, 1, limit->id_pattern) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	if (sqlite3_bind_text(ctx.update_limit, 2, limit->token, limit->token_length, SQLITE_STATIC) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	if (sqlite3_bind_int(ctx.update_limit, 3, limit->expires) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	if (sqlite3_bind_int(ctx.update_limit, 4, limit->updated) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	if (sqlite3_bind_int(ctx.update_limit, 5, limit->reported) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	if (sqlite3_bind_int(ctx.update_limit, 6, limit->counter) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	if (sql_step(ctx.db, ctx.update_limit) != SQLITE_DONE) {
		sql_error(__FILE__, __LINE__, ctx.update_limit);
		return;
	}
	(void) sqlite3_clear_bindings(ctx.update_limit);
	sqlite3_reset(ctx.update_limit);
}

static void
limit_increment(struct limit *limit)
{
	limit->counter++;

	if (sqlite3_bind_int64(ctx.increment_limit, 1, limit->id_pattern) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.increment_limit);
		return;
	}
	if (sqlite3_bind_text(ctx.increment_limit, 2, limit->token, limit->token_length, SQLITE_STATIC) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.increment_limit);
		return;
	}
	if (sql_step(ctx.db, ctx.increment_limit) != SQLITE_DONE) {
		sql_error(__FILE__, __LINE__, ctx.increment_limit);
		return;
	}
	(void) sqlite3_clear_bindings(ctx.increment_limit);
	sqlite3_reset(ctx.increment_limit);
}

static char *
buffer_grow(char *buffer, size_t grow, size_t length, size_t *size)
{
	char *copy;

	if (*size <= length) {
		if ((copy = realloc(buffer, length + grow)) == NULL) {
			free(buffer);
			return NULL; 	
		}
		*size = length + grow;
		buffer = copy;
	}

	return buffer;
}

char *
replace_references(int delim, const char *str, const char *source, regmatch_t *sub, size_t nsub)
{
	long index;
	const char *s;
	char *expand, *stop;
	size_t offset, size, length;

	if (str == NULL)
		return NULL;

	expand = NULL;
	offset = size = 0;

	for (s = str; *s != '\0'; s++) {
		if (*s == delim) {
			if (str < s && s[-1] == '\\') {
				/* Remove backslash from expansion. */
				expand[offset-1] = *s;
				continue;
			}
			index = strtol(s+1, &stop, 10);
			if (0 <= index && index <= nsub) {
				s = stop-1;
				length = sub[index].rm_eo - sub[index].rm_so;
				if ((expand = buffer_grow(expand, GROW_SIZE, offset+length, &size)) == NULL)
					return NULL;
				(void) memcpy(expand+offset, source+sub[index].rm_so, length); 
				offset += length;
			}
		} else {
			if ((expand = buffer_grow(expand, GROW_SIZE, offset, &size)) == NULL)
				return NULL;
			expand[offset++] = *s;
		}
	}
	if (expand != NULL)
		expand[offset] = '\0';

	return expand;
}

static void
limit_report(struct pattern_rule *rule, const char *action, struct limit *limit, const char *line, regmatch_t *parens)
{
        SMTP2 *smtp;
        Vector rcpts;
        int flags, err;
	const unsigned char *text;
        char *cmd, *expand, **table, buffer[SMTP_PATH_LENGTH];

	if (0 < debug) 
		(void) fprintf(
			stdout, "/%s/ %s: limit exceeded (%d)\n",
			rule->pattern, action, limit->reported + limit->counter
		);

	smtp = NULL;
	rcpts = NULL;
        if (report_to != NULL) {
		rcpts = TextSplit(rule->report != NULL ? strchr(rule->report, '=')+1 : report_to, ",; ", 0);
		 if (VectorLength(rcpts) <= 0) {
			(void) fprintf(stderr, "no report-to mail addresses\n");
			goto error0;
		}

		/* Try to connect to local smart host. */
		flags = SMTP_FLAG_LOG | (1 < debug ? SMTP_FLAG_DEBUG : 0);
		smtp = smtp2Open(smtp_host, SMTP_CONNECT_TO * UNIT_MILLI, SMTP_COMMAND_TO * UNIT_MILLI, flags);
		if (smtp == NULL) {
			(void) fprintf(stderr, "%s: %s (%d)\n", smtp_host, strerror(errno), errno);
			goto error1;
		}
		if (smtp2Mail(smtp, report_from) != SMTP_OK) {
			(void) fprintf(stderr, "%s: null sender not accepted\n", smtp_host);
			goto error2;
		}

		for (table = (char **) VectorBase(rcpts); *table != NULL; table++) {
			if (smtp2Rcpt(smtp, *table) != SMTP_OK)
				goto error2;
		}

		TimeStamp(&smtp->start, buffer, sizeof (buffer));
		(void) smtp2Printf(smtp, "Date: %s" CRLF, buffer);
		(void) smtp2Printf(smtp, "From: \"%s\" <%s>" CRLF, _NAME, smtp->sender);
		(void) smtp2Printf(smtp, "Message-ID: <%s@%s>" CRLF, smtp->id_string, smtp->local_ip);
		(void) smtp2Printf(smtp, "Subject: %s log limit exceeded %s" CRLF, _NAME, limit->token);
		(void) smtp2Print(smtp, CRLF, sizeof (CRLF)-1);
		(void) smtp2Printf(
			smtp, "/%s/ %s: limit exceeded (%d)" CRLF CRLF,
			rule->pattern, action, limit->reported + limit->counter
		);
	}

	if (sqlite3_bind_int64(ctx.select_limit_to_log, 1, limit->id_limit) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.increment_limit);
                goto error2;
	}
	if (sqlite3_bind_int64(ctx.select_limit_to_log, 2, limit->updated) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.increment_limit);
		goto error2;
	}

	while (sql_step(ctx.db, ctx.select_limit_to_log) == SQLITE_ROW) {
		if ((text = sqlite3_column_text(ctx.select_limit_to_log, 0)) == NULL)
			continue;
		if (0 < debug)
			(void) fprintf(stdout, "\t%s" CRLF, text);
		if (smtp != NULL)
			(void) smtp2Printf(smtp, "%s" CRLF, text);
	}

	if (rule->command != NULL) {
		/* Parse c="..." */
		cmd = TokenNext(strchr(rule->command, '=')+1, NULL, "\n", TOKEN_KEEP_BACKSLASH);

		/* Replace #n matched sub-expressions. */
		expand = replace_references('#', cmd, line, parens, rule->re.re_nsub);

		if (expand != NULL) {
			if ((err = system(expand)) < 0)
				(void) fprintf(stderr, "%s\n\t%s (%d)\n", expand, strerror(errno), errno);
			else if (0 < err)
				(void) fprintf(stderr, "%s\n\tcommand error %d\n", expand, err);
			free(expand);
		}
		free(cmd);
	}

	(void) sqlite3_clear_bindings(ctx.select_limit_to_log);
	sqlite3_reset(ctx.select_limit_to_log);
	if (smtp != NULL)
		(void) smtp2Dot(smtp);
error2:
	smtp2Close(smtp);
error1:
	VectorDestroy(rcpts);
error0:
	;
}

/* 
 * Parse and check a limit. Malformed limit actions are ignored.
 *
 * limit := "l=" 1*[ index "," ] max "/" period [ unit ]
 */ 
static void
check_limit(const char *action, const char *line, struct pattern_rule *rule, regmatch_t *parens, time_t tstamp)
{
	int unit;
	struct limit limit;
	char *lp, *stop, token[LINE_SIZE];
	long number, offset, length, seconds;

	/* Check for the l= prefix and is non-empty. */
	action += strspn(action, " \t");
	if (action[0] != 'l' || action[1] != '=' || action[2] == '\0') {
		(void) fprintf(stderr, "/%s/ %s: not a limit\n", rule->pattern, action);
		return;
	}

	/* Join selected limit sub-expressions into a token. */
	for (lp = (char *) action+2, offset = 0; offset < sizeof (token)-1; offset += length, lp = stop+1) {
		/* Get sub-expression index or limit max. */
		number = strtol(lp, &stop, 10);

		if (*stop != ',') {
			/* Remove trailing delimiter. */
			if (0 < offset)
				offset--;
			break;
		}
		if (rule->re.re_nsub < number) {
			(void) fprintf(
				stderr, "/%s/ %s: regex sub-expression index %ld out of bounds, max. %lu\n", 
				rule->pattern, action, number, (unsigned long) rule->re.re_nsub
			);
			return;
		}

		length = parens[number].rm_eo-parens[number].rm_so;
		(void) memcpy(token+offset, line+parens[number].rm_so, length);
		token[offset+length++] = TOKEN_DELIMITER;
	}
	if (lp == stop || *stop != '/') {
		(void) fprintf(stderr, "/%s/ %s: no limit specified\n", rule->pattern, action);
		return;
	}
	token[offset] = '\0';

	/* Get the limit time period. */
	seconds = strtol(lp = stop+1, &stop, 10);
	unit = *stop == '\0' ? 's' : *stop;

        switch (unit) {
        case 'w': seconds *= 7;
        case 'd': seconds *= 24;
        case 'h': seconds *= 60;
        case 'm': seconds *= 60;
	case 's': break;
	default:
		(void) fprintf(stderr, "/%s/ %s: invalid time unit (%c)\n", rule->pattern, action, unit);
        }

	if (2 < debug)
		(void) fprintf(stdout, "limit=%ld/%lds token=\"%s\"\n", number, seconds, token);

	limit_select(rule->id_pattern, token, offset, &limit);
	if (limit.expires <= tstamp) {
		if (limit.expires == 0) {
			/* New limit token. */
			limit.created = tstamp;
			limit.updated = tstamp;
			limit.expires = tstamp + seconds;
			limit_insert(&limit);
		} else {
			/* Report extra log lines since 1st report. */
			if (0 < limit.reported && limit.reported < limit.counter) 
				limit_report(rule, action, &limit, line, parens);

			/* Reset existing limit's expire time. */
			limit.updated = tstamp;
			limit.expires = tstamp + seconds;
			limit.reported = 0;
			limit.counter = 1;
			limit_update(&limit);
		}
	} else {
		limit_increment(&limit);
		if (number < limit.counter && limit.counter <= number + 1) {
			/* 1st limit exceeded report. */
			limit_report(rule, action, &limit, line, parens);

			/* Touch the record, leave the expires as is, remember how many reported. */
			limit.updated = tstamp;
			limit.reported = limit.counter;
			limit_update(&limit);
		}
	}
}

static void
check_limits(const char *line, struct pattern_rule *rule, regmatch_t *parens, time_t tstamp)
{
	char **entry;

	for (entry = rule->limits; *entry != NULL; entry++) {
		check_limit(*entry, line, rule, parens, tstamp);
	}
}

static sqlite3_int64
append_log(char *line, struct pattern_rule *rule, regmatch_t *parens, time_t tstamp)
{
	int err;
	size_t length;
	sqlite3_int64 last_log;

	if (2 < debug) {
		(void) fprintf(
			stdout, "match \"%.*s\"\n\tthread %ld \"%.*s\"\n", 
			(int) (parens[0].rm_eo - parens[0].rm_so), line + parens[0].rm_so,
			rule->thread,
			(int) (parens[rule->thread].rm_eo - parens[rule->thread].rm_so), 
			line + parens[rule->thread].rm_so
		);
	}

	if (sqlite3_bind_int(ctx.insert_log, 1, rule->id_pattern) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_log);
		goto error1;
	}
	if (sqlite3_bind_int(ctx.insert_log, 2, tstamp) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_log);
		goto error1;
	}
	err = sqlite3_bind_text(
		ctx.insert_log, 3, line + parens[rule->thread].rm_so, 
		parens[rule->thread].rm_eo - parens[rule->thread].rm_so, 
		SQLITE_STATIC
	);
	if (err != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_log);
		goto error1;
	}

	length = strlen(line);
	length = strlrspn(line, length, "\r\n");

	if (sqlite3_bind_text(ctx.insert_log, 4, line, length, SQLITE_STATIC) != SQLITE_OK) {
		sql_error(__FILE__, __LINE__, ctx.insert_log);
		goto error1;
	}
	if (sql_step(ctx.db, ctx.insert_log) != SQLITE_DONE) {
		sql_error(__FILE__, __LINE__, ctx.insert_log);
		goto error1;
	}

	last_log = sqlite3_last_insert_rowid(ctx.db);
	(void) sqlite3_clear_bindings(ctx.insert_log);
error1:
	return last_log;
}

static void
process_rules(char *line)
{
	time_t tstamp;
	ListItem *item;
	struct pattern_rule *rule;
	regmatch_t parens[REGEX_PARENS_SIZE];
	long month, day, hour, minute, second;

	(void) convertSyslog(line, &month, &day, &hour, &minute, &second, NULL);
	convertToGmt(assumed_year, month, day, hour, minute, second, assumed_tz, &tstamp);

	for (item = pattern_rules.head; item != NULL; item = item->next) {
		rule = item->data;
		if (regexec(&rule->re, line, REGEX_PARENS_SIZE, parens, 0) == 0) {
			(void) append_log(line, rule, parens, tstamp);
			if (rule->limits[0] != NULL)
				check_limits(line, rule, parens, tstamp);
			break;
		}
	}
}

static int
process_stream(FILE *fp)
{
	int follow;
	struct stat sb;
	dev_t last_dev = 0;
	ino_t last_ino = 0;
	size_t last_size = 0;
	char line[LINE_SIZE];

	follow = follow_flag && fileno(fp) != STDIN_FILENO;

	while (fgets(line, sizeof (line), fp) != NULL) {
		process_rules(line);

		if (follow) {
			if (fstat(fileno(fp), &sb) 
			|| sb.st_size < last_size 
			|| sb.st_ino != last_ino 
			|| sb.st_dev != last_dev)
				return -1;

			last_ino = sb.st_ino;
			last_dev = sb.st_dev;
			last_size = sb.st_size;
		}
	}

	return 0;
}

static void
process_file(const char *file)
{
	int rc;
	FILE *fp;

	/* Check for standard input. */
	if (file[0] == '-' && file[1] == '\0')
		(void) process_stream(stdin);

	/* Otherwise open the log file. */
	else {
		/* Handle re-opening of log file for -f option. */
		for (rc = -1; rc && (fp = fopen(file, "rb")) != NULL; ) {
			rc = process_stream(fp);
			(void) fclose(fp);
		}
			
 		if (fp == NULL) 
			(void) fprintf(stderr, "%s: %s (%d)\n", file, strerror(errno), errno);
	}
}

static void
at_exit_cleanup(void)
{
	listFini(&pattern_rules);
	VectorDestroy(report);
	LogClose();
	fini_db();
}

static void
init_today(void)
{
	time_t now;
	struct tm today;

	(void) time(&now);
	(void) localtime_r(&now, &today);
	assumed_year = today.tm_year + 1900;
	assumed_tz = today.tm_gmtoff;
}
	
int
main(int argc, char **argv)
{
	int ch, argi;

	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
		case 'f':
			follow_flag = 1;
			break;

		case 'F':
			report_from = optarg;
			if (*report_from == '\0')
				report_from = NULL;
			break;

		case 'v':
			debug++;
			break;

		case 'd':
			db_path = optarg;
			break;

		case 'D':
			db_delete = 1;
			break;

		case 'r':
			report_to = optarg;
			break;
			
		case 'R':
			db_reset = 1;
			break;

		case 's':
			smtp_host = optarg;
			break;
			
		case 'y':
			assumed_year = strtol(optarg, NULL, 10);
			break;
			
		case 'z':
			assumed_tz = strtol(optarg, NULL, 10);
			break;
			
		default:
			(void) printf(usage);
			return EXIT_USAGE;
		}
	}
	
	if (argc <= optind || (follow_flag && optind+2 < argc)) {
		(void) printf(usage);
		return EXIT_USAGE;
	}

	if (1 < debug)
		LogOpen(NULL);

	if (db_path == NULL) {
		(void) snprintf(db_user, sizeof (db_user), DB_PATH_FMT, getuid());
		db_path = db_user;
	}
	
	if (db_delete)
		(void) unlink(db_path);

	init_today();
		
	if (atexit(at_exit_cleanup) || init_db(db_path) || init_rules(argv[optind]))
		return EX_SOFTWARE;
	
	if ((report = TextSplit(report_to, ", ", 0)) == NULL)
		return EX_SOFTWARE;

	if (optind+1 == argc) {
		process_file("-");
	} else {
		for (argi = optind+1; argi < argc; argi++) 
			process_file(argv[argi]);
	}

	return 0;
}

/***********************************************************************
 *** END
 ***********************************************************************/

