/*
 * sqlargs.c
 *
 * Copyright 2007 by Anthony Howe.  All rights reserved.
 *
 *
 * Description
 * -----------
 *
 *	sqlargs [-c command_template][-d delim][-m max] db_url
 *		[select_statement]
 *
 *
 * An xargs(1) like command-line tool that queries a database for
 * arguments to be used in a command substitution. Each row of the
 * result would invoke one instance of the command (default is to
 * to simply echo the row to standard output), with a limit on how
 * many instances of the command maybe be running at a time.
 *
 * -c command_template
 *
 *	A shell command line, where $1 through $n are replaced by the
 *	corresponding columns specified. The default is equivalent to
 *	"echo $@"
 *
 * -d delim
 *
 *	The column delimiter used to split CSV file.
 *
 * -m max
 *
 *	The maximum number of commands, specified by -c, that can be
 *	on going at any one time. The default is one (1).
 *
 * db_url is one of the following formats:
 *
 *	csv:/path/to/file.txt
 *	mysql://host[:port]
 *	postgresql://host[:port]
 *	sqlite:/path/to/db.sq3
 *
 * select_statement
 *
 *	The select statement to invoke. The order of columns returned
 *	will be the order for positional parameters used for substitution
 *	in the command_template string. This argument is ignored for csv:
 */

/*
 * Example:
 *
 *	sqlargs -c 'popin -h$1 -r $2 $3 | smtp2 -h localhost $4' -m 20 \
 *	sqlite:user_list.sq3 'select pophost, login, password, mailto from accounts;'
 */

#include <com/snert/lib/version.h>

#ifndef SHELL
# ifdef __unix__
#  define SHELL		"/bin/sh"
#  define SHELL_C_OPTION	"-c"
# endif
# ifdef __WIN32__
#  define SHELL		"C:/Windows/System32/cmd.exe"
#  define SHELL_C_OPTION	"/C"
# endif
#endif


#ifndef ROW_BUFFER_SIZE
#define ROW_BUFFER_SIZE		(4 * 1024)
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#ifdef HAVE_POLL_H
# include <poll.h>
# ifndef INFTIM
#  define INFTIM			(-1)
# endif
#endif
#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#ifndef __MINGW32__
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_SYS_WAIT_H)
#  include <sys/wait.h>
# endif
#else
extern unsigned int sleep(unsigned int);
#endif

#ifdef __WIN32__
# include <windows.h>
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_SQLITE3_H
# include <sqlite3.h>

# if SQLITE_VERSION_NUMBER < 3003008
#  error "Thread safe SQLite3 version 3.3.8 or better required."
# endif
#endif

#include <com/snert/lib/util/ProcTitle.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#define _NAME		"sqlargs"
#define _COPYRIGHT	"Copyright 2007 by Anthony Howe.  All rights reserved."

static const char usage[] =
"usage:	" _NAME " [-c command_template][-d delim][-m max] db_url\n"
"		[select_statement]\n"
"\n"
"An xargs(1) like command-line tool that queries a data source for\n"
"arguments to be used in a command substitution. Each row from the\n"
"data source will invoke one instance of the command (default is\n"
"to echo the row to standard output), with a limit as to how many\n"
"instances of the command may be running at a time.\n"
"\n"
"-c command_template\n"
"\tA shell command line, where $1 through $n are replaced\n"
"\tby the corresponding columns specified. This command is\n"
"\tpassed to /bin/sh using the -c flag; interpretation, if\n"
"\tany, is performed by the shell. The default is similar\n"
"\tto ''echo \"$@\"''\n"
"\n"
"-d delim\n"
"\tThe column delimiter used to split CSV file.\n"
"\n"
"-m max\n"
"\tThe maximum number of commands, specified by -c, that\n"
"\tcan be on going at any one time. The default is one (1).\n"
"\n"
"db_url\n"
"\tUse one of the following formats:\n"
"\n"
"\t\tcsv:/path/to/file.txt\n"
#ifdef HAVE_MYSQL_H
"\t\tmysql://host[:port]\n"
#endif
#ifdef HAVE_POSTGRESQL_H
"\t\tpostgresql://host[:port]\n"
#endif
#ifdef HAVE_SQLITE3_H
"\t\tsqlite:/path/to/db.sq3\n"
#endif
"\n"
"select_statement\n"
"\tThe select statement to invoke on the data source. The\n"
"\torder of columns returned will be the order used for\n"
"\tpositional parameters used for substitution in the\n"
"\tcommand template string. This argument is ignored for\n"
"\tcsv:\n"
"\n"
_COPYRIGHT "\n"
;

static int debug;
static char *c_option;
static char delim[] = ",";

#ifdef __WIN32__
static Vector pids;
#endif

static unsigned total_rows;
static unsigned max_processes = 1;
static unsigned active_processes;
static char row_buffer[ROW_BUFFER_SIZE];

typedef Vector (*ContextGetRowFn)(void *);
typedef int (*ContextDestroyFn)(void *);
typedef int (*ContextFinalizeFn)(void *);

typedef struct {
	ContextDestroyFn destroy;
	ContextFinalizeFn finalize;
	ContextGetRowFn getRow;
	const char *sql_stmt;
	void *stmt;
	void *db;
} Context;

typedef struct {
	ContextDestroyFn destroy;
	ContextFinalizeFn finalize;
	ContextGetRowFn getRow;
	const char *sql_stmt;
	void *stmt;
	FILE *db;
} ContextCSV;

#ifdef HAVE_SQLITE3_H
typedef struct {
	ContextDestroyFn destroy;
	ContextFinalizeFn finalize;
	ContextGetRowFn getRow;
	const char *sql_stmt;
	sqlite3_stmt *stmt;
	sqlite3 *db;
} ContextSqlite;
#endif

static Context *context;

/***********************************************************************
 ***
 ***********************************************************************/

static void
atExitCleanUp(void)
{
	if (context != NULL) {
		if (context->finalize != NULL)
			(void) (*context->finalize)(context->stmt);
		if (context->destroy != NULL)
			(void) (*context->destroy)(context->db);
		free(context);
	}

#ifdef __WIN32__
	VectorDestroy(pids);
#endif
}

/***********************************************************************
 *** Comma Separated Values
 ***********************************************************************/

/*
 *
 */
static Vector
contextGetRowCsv(Context *_ctx)
{
	int rc;
	Vector columns;
	ContextCSV *ctx = (ContextCSV *) _ctx;

	while ((rc = TextInputLine(ctx->db, row_buffer, sizeof (row_buffer))) == 0)
		;

	if (rc < 0)
		return NULL;

	if ((columns = TextSplit(row_buffer, delim, 0)) == NULL) {
		fprintf(stderr, "csv error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	return columns;
}

static Context *
contextOpenCsv(Context *_ctx, const char *db_url)
{
	ContextCSV *ctx = (ContextCSV *) _ctx;

	db_url += sizeof ("csv:")-1;

	if ((ctx->db = fopen(db_url, "r")) == NULL) {
		fprintf(stderr, "open \"%s\" error: %s (%d)\n", db_url, strerror(errno), errno);
		free(ctx);
		return NULL;
	}

	ctx->getRow = (ContextGetRowFn) contextGetRowCsv;
	ctx->destroy = (ContextDestroyFn) fclose;

	return _ctx;
}

/***********************************************************************
 *** SQLite3
 ***********************************************************************/

#ifdef HAVE_SQLITE3_H

static Vector
contextGetRowSqlite(Context *_ctx)
{
	char *column;
	Vector columns;
	int rc, i, count;
	ContextSqlite *ctx = (ContextSqlite *) _ctx;

	/* Using the newer sqlite_prepare_v2() interface means that
	 * sqlite3_step() will return more detailed error codes. See
	 * sqlite3_step() API reference.
	 */
	while ((rc = sqlite3_step(ctx->stmt)) == SQLITE_BUSY) {
		if (0 < debug)
			fprintf(stderr, "database busy\n");
		sleep(1);
	}

	if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
		fprintf(stderr, "sql \"%s\" error: %s\n", ctx->sql_stmt, sqlite3_errmsg(ctx->db));
		exit(1);
	}

	if (rc != SQLITE_ROW) {
		/* http://www.sqlite.org/cvstrac/wiki?p=DatabaseIsLocked
		 *
		 * "Sometimes people think they have finished with a SELECT statement
		 *  because sqlite3_step() has returned SQLITE_DONE. But the SELECT is
		 *  not really complete until sqlite3_reset() or sqlite3_finalize()
		 *  have been called.
		 */
		(void) sqlite3_reset(ctx->stmt);
		return NULL;
	}

	if ((columns = VectorCreate(5)) == NULL)
		goto error0;

	VectorSetDestroyEntry(columns, free);
	count = sqlite3_column_count(ctx->stmt);
	for (i = 0; i < count; i++) {
		if ((column = strdup((char *) sqlite3_column_text(ctx->stmt, i))) == NULL)
			goto error1;

		if (VectorAdd(columns, column))
			goto error2;
	}

	return columns;
error2:
	free(column);
error1:
	VectorDestroy(columns);
error0:
	fprintf(stderr, "sql \"%s\" error: %s (%d)\n", ctx->sql_stmt, strerror(errno), errno);
	exit(1);

	return NULL;
}

static Context *
contextOpenSqlite(Context *_ctx, const char *db_url, const char *sql_statement)
{
	struct stat sb;
	ContextSqlite *ctx = (ContextSqlite *) _ctx;

	db_url += sizeof ("sqlite:")-1;

	if (stat(db_url, &sb)) {
		fprintf(stderr, "open \"%s\" error: %s (%d)\n", db_url, strerror(errno), errno);
		goto error0;
	}

	if (sqlite3_open(db_url, &ctx->db) != SQLITE_OK) {
		fprintf(stderr, "open \"%s\" error: %s\n", db_url, sqlite3_errmsg(ctx->db));
		goto error0;
	}

	ctx->destroy = (ContextDestroyFn) sqlite3_close;

	if (sqlite3_prepare_v2(ctx->db, sql_statement, -1, &ctx->stmt, NULL) != SQLITE_OK) {
		fprintf(stderr, "statement \"%s\" error: %s\n", sql_statement, sqlite3_errmsg(ctx->db));
		goto error1;
	}

	ctx->finalize = (ContextFinalizeFn) sqlite3_finalize;
	ctx->getRow = (ContextGetRowFn) contextGetRowSqlite;
	ctx->sql_stmt = sql_statement;

	return _ctx;
error1:
	sqlite3_close(ctx->db);
error0:
	free(ctx);
	return NULL;
}

#endif /* HAVE_SQLITE3_H */

/***********************************************************************
 ***
 ***********************************************************************/

static Context *
contextOpen(const char *db_url, const char *sql_statement)
{
	Context *ctx;

	if ((ctx = calloc(1, sizeof (*ctx))) != NULL) {
		if (0 < TextInsensitiveStartsWith(db_url, "csv:"))
			ctx = contextOpenCsv(ctx, db_url);
#ifdef HAVE_SQLITE3_H
		else if (0 < TextInsensitiveStartsWith(db_url, "sqlite:"))
			ctx = contextOpenSqlite(ctx, db_url, sql_statement);
#endif
		else {
			fprintf(stderr, "unsupported data source: %s\n", db_url);
			free(ctx);
			ctx = NULL;
		}
	}

	return ctx;
}

static void
init(void)
{
#ifdef SIGPIPE
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		fprintf(stderr, "SIGPIPE error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}
#endif
#ifdef __WIN32__
	/* Create a Vector of process HANDLEs. This assumes
	 * that sizeof (HANDLE) == sizof (void *).
	 */
	if (sizeof (HANDLE) != sizeof (void *)) {
		fprintf(stderr, "assertion failed: sizeof (HANDLE) != sizeof (void *)\n");
		exit(1);
	}

	if ((pids = VectorCreate(max_processes)) == NULL) {
		fprintf(stderr, "VectorCreate error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	VectorSetDestroyEntry(pids, FreeStub);
#endif
	if (atexit(atExitCleanUp))
		exit(1);
}

/***********************************************************************
 ***
 ***********************************************************************/

void
startCommand(Vector args)
{
        char **base;

	if (c_option == NULL) {
		for (base = (char **) VectorBase(args); *base != NULL; base++) {
			printf("\"%s\" ", *base);
		}
		fputc('\n', stdout);
		return;
	}

#ifdef __unix__
{
        pid_t child;

        if ((child = fork()) == -1) {
		fprintf(stderr, "fork error: %s (%d)\n", strerror(errno), errno);
               	exit(1);
	}

	if (child == 0) {
		atExitCleanUp();

		if (VectorInsert(args, 0, SHELL)
		||  VectorInsert(args, 1, SHELL_C_OPTION)
		||  VectorInsert(args, 2, c_option)
		||  VectorInsert(args, 3, "(unknown)")
		) {
			fprintf(stderr, "fork error: %s (%d)\n", strerror(errno), errno);
			exit(1);
		}

		/* Time for a change of scenery. */
		(void) execv(SHELL, (char * const *) VectorBase(args));

		_exit(69);
	}
}
#endif
#ifdef __WIN32__
{
	char *cmdline;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	atExitCleanUp();

	if (VectorInsert(args, 0, SHELL)
	||  VectorInsert(args, 1, SHELL_C_OPTION)
	||  VectorInsert(args, 2, c_option)
	||  VectorInsert(args, 3, "(unknown)")
	) {
		fprintf(stderr, "fork error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	cmdline = TextJoin(" ", args);

	ZeroMemory(&si, sizeof (si));
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;

	if (CreateProcess(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi) == 0) {
		free(cmdline);
		errno = GetLastError();
		fprintf(stderr, "fork error: %s (%d)\n", strerror(errno), errno);
		exit(1);
	}

	free(cmdline);

	(void) VectorAdd(pids, pi.hProcess);
}
#endif
	active_processes++;
}

void
waitForAnyPid(void)
{
#ifdef __unix__
	while (waitpid(0, NULL, 0) == -1 && errno == EINTR)
		;
#endif
#ifdef __WIN32__
	DWORD wait_rc;

	wait_rc = WaitForMultipleObjects(VectorLength(pids), (HANDLE *) VectorBase(pids), FALSE, INFINITE);

	if (wait_rc < WAIT_ABANDONED_0)
		VectorRemove(pids, wait_rc - WAIT_OBJECT_0);
#endif
	active_processes--;
}

void
waitForAllPids(void)
{
	while (0 < active_processes)
		waitForAnyPid();
}

int
main(int argc, char **argv)
{
	int ch;
	Vector row;

	while ((ch = getopt(argc, argv, "c:d:m:v")) != -1) {
		switch (ch) {
		case 'c':
			c_option = optarg;
			break;
		case 'd':
			delim[0] = *optarg;
			break;
		case 'm':
			max_processes = (unsigned) strtol(optarg, NULL, 10);
			break;
		case 'v':
			debug++;
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

	if (TextInsensitiveStartsWith(argv[optind], "csv:") < 0 && argc < optind + 2) {
		(void) fprintf(stderr, usage);
		exit(64);
	}

	init();

	if ((context = contextOpen(argv[optind], argv[optind+1])) == NULL)
		exit(1);

	while ((row = (*context->getRow)(context)) != NULL) {
		total_rows++;
		if (max_processes <= active_processes) {
			/* Wait for one or more processess to
			 * terminate before starting a new one.
			 */
			 waitForAnyPid();
		}

		startCommand(row);
		VectorDestroy(row);
	}

	if (0 < debug)
		fprintf(stderr, "rows %u\n", total_rows);

	/* Wait for remaining processes to terminate. */
	waitForAllPids();

	return 0;
}
