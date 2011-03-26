/*
 * smtpe.c
 *
 * SMTP using IO events; a SocketEvent test.
 *
 * Copyright 2011 by Anthony Howe & Fort Systems Ltd. All rights reserved.
 */

#define _NAME				"smtpe"
#define _VERSION			"0.1"
#define _COPYRIGHT			LIBSNERT_COPYRIGHT
#define API_VERSION			"0.1"

#if !defined(CF_DIR)
# if defined(__WIN32__)
#  define CF_DIR			"."
# else
#  define CF_DIR			"/etc/" _NAME
# endif
#endif

#if !defined(CF_FILE)
# if defined(__WIN32__)
#  define CF_FILE			CF_DIR "/" _NAME ".cf"
# else
#  define CF_FILE			CF_DIR "/" _NAME ".cf"
# endif
#endif

#if !defined(CF_LUA)
# if defined(__WIN32__)
#  define CF_LUA			CF_DIR "/" _NAME ".lua"
# else
#  define CF_LUA			CF_DIR "/" _NAME ".lua"
# endif
#endif

#if !defined(PID_FILE)
# if defined(__WIN32__)
#  undef PID_FILE
# else
#  define PID_FILE			"/var/run/" _NAME ".pid"
# endif
#endif

#if !defined(MAIL_DIR)
# if defined(__WIN32__)
#  define MAIL_DIR			"/" _NAME "/mail"
# else
#  define MAIL_DIR			"/var/mail"
# endif
#endif

#ifndef SAFE_PATH
# if defined(__WIN32__)
#  define SAFE_PATH			CF_DIR "/bin"
# else
#  define SAFE_PATH			"/bin:/usr/bin"
# endif
#endif

#if !defined(WORK_DIR)
# if defined(__WIN32__)
#  define WORK_DIR			"/" _NAME "/mail"
# else
#  define WORK_DIR			"/var/tmp"
# endif
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#ifdef NDEBUG
# define NVALGRIND
#endif
#include <org/valgrind/valgrind.h>
#include <org/valgrind/memcheck.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>
#ifdef HAVE_SYS_TYPE_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif
#if defined(HAVE_SYSLOG_H)
# include <syslog.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/pt/pt.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/util/time62.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef HAVE_RAND_R
# define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand_r(&rand_seed) / (RAND_MAX+1.0)))
#else
# define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand() / (RAND_MAX+1.0)))
#endif

#ifndef RAND_MSG_COUNT
#define RAND_MSG_COUNT		RANDOM_NUMBER(62.0*62.0)
#endif

#define SMTP_PIPELINING_TIMEOUT	300
#define PEEK_DELAY_NS		(SMTP_PIPELINING_TIMEOUT * UNIT_MICRO)

typedef struct stmp_ctx SmtpCtx;
typedef pt_word_t (*SmtpCmdHook)(SocketEvents *loop, SocketEvent *event);

typedef struct {
	size_t size;
	long length;
	long offset;
	char *data;
} Buffer;

typedef enum {
	Lua_OK		= 0,
	Lua_YIELD	= LUA_YIELD,
	Lua_ERRRUN	= LUA_ERRRUN,
	Lua_ERRSYNTAX	= LUA_ERRSYNTAX,
	Lua_ERRMEM	= LUA_ERRMEM,
	Lua_ERRERR	= LUA_ERRERR,
} LuaCode;

typedef struct {
	pt_t pt;
} LuaSmtpHook;

typedef int (*LuaSmtpHookInit)(lua_State *, SmtpCtx *);

#define SMTP_CMD_HOOK(fn)	cmd_ ## fn
#define SMTP_CMD_DEF(fn)	PT_THREAD( SMTP_CMD_HOOK(fn)(SocketEvents *loop, SocketEvent *event) )
#define SMTP_CMD_DO(fn)		(*cmd_ ## fn)(loop, event)

typedef struct {
	const char *cmd;
	SmtpCmdHook hook;
} Command;

typedef struct {
	pt_t pt;
	size_t size;
	long length;
	int line_no;
	int line_max;
	char **lines;
	SMTP_Reply_Code smtp_rc;
} SmtpLines;

#define ID_SIZE		19

struct stmp_ctx {
	int lua_thread;
	char id_sess[ID_SIZE];
	char id_trans[ID_SIZE];
	int transaction_count;
	JMP_BUF on_error;
	ParsePath *sender;
	Vector recipients;
	long mail_size;		/* RFC 1870 */
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	Buffer path;		/* PATH_MAX */
	Buffer addr;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer host;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer helo;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer auth;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer work;		/* SMTP_TEXT_LINE_LENGTH+1 */
	Buffer reply;		/* SMTP_TEXT_LINE_LENGTH+1 */
	Buffer pipe;		/* SMTP_MINIMUM_MESSAGE_LENGTH */
	Buffer input;		/* Sliding window over pipe buffer. */

#define SMTP_CTX_SIZE		(sizeof (SmtpCtx) 		\
				+ PATH_MAX			\
				+ 4 * (SMTP_DOMAIN_LENGTH+1)	\
				+ 2 * (SMTP_TEXT_LINE_LENGTH+1)	\
				+ SMTP_MINIMUM_MESSAGE_LENGTH)

	/* Coroutine & hook state. */
	pt_t pt;		/* Protothread state of current command. */
	LuaSmtpHook lua;
	ParsePath **rcpt;	/* For interating over recipients list. */
	SMTP_Reply_Code smtp_rc;

	/* SMTP state */
	int is_dot;		/* 0 no trailing dot, else length of "dot" tail */
	unsigned eoh;		/* Offset to end-of-header seperator. */
	SmtpCmdHook state;
	SmtpCmdHook state_helo;
	SocketEvents *loop;	/* Convience data for Lua / C interface. */
	SocketEvent *event;	/* Convience data for Lua / C interface. */

	Socket2 *mx;
	SmtpLines mx_read;
	SocketEvent mx_event;

	Socket2 *client;
	FILE *spool_fp;
	int is_enabled;
	int is_pipelining;

	/* DNS lookup state. */
	PDQ *pdq;
	int pdq_wait_all;
	PDQ_rr *pdq_answer;
	Socket2 *pdq_socket;
	SocketEvent pdq_event;
	time_t pdq_stop_after;	/* overall timeout for PDQ lookup */
};

#define SETJMP_PUSH(this_jb) \
	{ JMP_BUF jb; memcpy(&jb, this_jb, sizeof (jb))

#define SETJMP_POP(this_jb) \
	memcpy(this_jb, &jb, sizeof (jb)); }

/***********************************************************************
 *** Strings
 ***********************************************************************/

#define STRLEN(const_str)	(sizeof (const_str)-1)
#define CRLF	"\r\n"
#define LF	"\n"

#define LOG_FMT			"%s "
#define LOG_LINE		__FILE__, __LINE__
#define LOG_ID(ctx)		(ctx)->id_sess
#define LOG_TRAN(ctx)		(ctx)->id_trans
#define LOG_INT(ctx)		LOG_ID(ctx), LOG_LINE

#define TRACE_FN(n)		if (verb_trace.value) syslog(LOG_DEBUG, "%s", __FUNCTION__)
#define TRACE_CTX(s, n)		if (verb_trace.value) syslog(LOG_DEBUG, LOG_FMT "%s", (s)->id_sess, __FUNCTION__)

static const char empty[] = "";

static const char log_init[] = "init error %s(%d): %s (%d)";
static const char log_oom[] = LOG_FMT "out of memory %s(%d)";
static const char log_internal[] = LOG_FMT "internal error %s(%d)";
static const char log_buffer[] = LOG_FMT "buffer overflow %s(%d)";
static const char log_error[] = LOG_FMT "error %s(%d): %s (%d)";

#define FMT(n)			" %sE" #n " "

static const char fmt_ok[] = "250 2.0.0 OK\r\n";
static const char fmt_welcome[] = "220 %s ESMTP %s\r\n";
static const char fmt_quit[] = "221 2.0.0 %s closing connection %s\r\n";
static const char fmt_pipeline[] = "550 5.3.3" FMT(000) "pipelining not allowed\r\n";
static const char fmt_no_rcpts[] = "554 5.5.0" FMT(000) "no recipients\r\n";
static const char fmt_no_piping[] = "%d 5.5.0" FMT(000) "pipeline data after %s command\r\n";
static const char fmt_unknown[] = "502 5.5.1" FMT(000) "%s command unknown\r\n";
static const char fmt_out_seq[] = "503 5.5.1" FMT(000) "%s out of sequence\r\n";
static const char fmt_data[] = "354 enter mail, end with \".\" on a line by itself\r\n";
static const char fmt_auth_already[] = "503 5.5.1" FMT(000) "already authenticated\r\n";
static const char fmt_auth_mech[] = "504 5.5.4" FMT(000) "unknown AUTH mechanism\r\n";
static const char fmt_auth_ok[] = "235 2.0.0" FMT(000) "authenticated\r\n";
static const char fmt_syntax[] = "501 5.5.2" FMT(000) "syntax error\r\n";
static const char fmt_bad_args[] = "501 5.5.4" FMT(000) "invalid argument %s\r\n";
static const char fmt_internal[] = "421 4.3.0" FMT(000) "internal error, %s\r\n";
static const char fmt_buffer[] = "421 4.3.0" FMT(000) "buffer overflow\r\n";
static const char fmt_mail_parse[] = "%d %s" FMT(000) "\r\n";
static const char fmt_mail_size[] = "552 5.3.4" FMT(000) "message size exceeds %ld\r\n";
static const char fmt_mail_ok[] = "250 2.1.0 sender <%s> OK\r\n";
static const char fmt_rcpt_parse[] = "%d %s" FMT(000) "\r\n";
static const char fmt_rcpt_null[] = "550 5.7.1" FMT(000) "null recipient invalid\r\n";
static const char fmt_rcpt_ok[] = "250 2.1.0 recipient <%s> OK\r\n";
static const char fmt_try_again[] = "451 4.4.5" FMT(000) "try again later\r\n";
static const char fmt_msg_ok[] = "250 2.0.0 message %s accepted\r\n";
static const char fmt_msg_reject[] = "550 5.7.0" FMT(000) "message %s rejected\r\n";

static const char fmt_helo[] = "250 Hello %s (%s, %s)\r\n";

static const char fmt_ehlo[] =
"250-Hello %s (%s, %s)\r\n"
"250-ENHANCEDSTATUSCODES\r\n"	/* RFC 2034 */
"%s"				/* XCLIENT */
"%s"				/* RFC 2920 pipelining */
"250-AUTH PLAIN\r\n"		/* RFC 2554 */
"250 SIZE %ld\r\n"		/* RFC 1870 */
;

static const char fmt_help[] =
"214-2.0.0 ESMTP supported commands:\r\n"
"214-2.0.0     AUTH    DATA    EHLO    HELO    HELP\r\n"
"214-2.0.0     NOOP    MAIL    RCPT    RSET    QUIT\r\n"
"214-2.0.0\r\n"
"214-2.0.0 ESMTP commands not implemented:\r\n"
"214-2.0.0     ETRN    EXPN    TURN    VRFY\r\n"
"214-2.0.0\r\n"
"214-2.0.0 Administration commands:\r\n"
"214-2.0.0     VERB    XCLIENT\r\n"
"214-2.0.0\r\n"
#ifdef ADMIN_CMDS
"214-2.0.0 Administration commands:\r\n"
"214-2.0.0     CONN    CACHE   INFO    KILL    LKEY    OPTN\r\n"
"214-2.0.0     STAT    VERB    XCLIENT\r\n"
"214-2.0.0\r\n"
#endif
"214 2.0.0 End\r\n"
;

/***********************************************************************
 *** Common Server Options
 ***********************************************************************/

static const char usage_title[] =
  "\n"
"# " _NAME " " _VERSION ", " LIBSNERT_STRING "\n"
"# \n"
"# " LIBSNERT_COPYRIGHT "\n"
"#"
;
static const char usage_syntax[] =
  "Option Syntax\n"
"# \n"
"# Options can be expressed in four different ways. Boolean options\n"
"# are expressed as +option or -option to turn the option on or off\n"
"# respectively. Numeric, string, and list options are expressed as\n"
"# option=value to set the option or option+=value to append to a\n"
"# list. Note that the +option and -option syntax are equivalent to\n"
"# option=1 and option=0 respectively. String values containing white\n"
"# space must be quoted using single (') or double quotes (\"). Option\n"
"# names are case insensitive.\n"
"# \n"
"# Some options, like +help or -help, are treated as immediate\n"
"# actions or commands. Unknown options are ignored and not reported.\n"
"# The first command-line argument is that which does not adhere to\n"
"# the above option syntax. The special command-line argument -- can\n"
"# be used to explicitly signal an end to the list of options.\n"
"# \n"
"# The default options, as shown below, can be altered by specifying\n"
"# them on the command-line or within an option file, which simply\n"
"# contains command-line options one or more per line and/or on\n"
"# multiple lines. Comments are allowed and are denoted by a line\n"
"# starting with a hash (#) character. If the file option is defined\n"
"# and not empty, then it is parsed first, followed by the command\n"
"# line options.\n"
"#"
;
static const char usage_daemon[] =
  "Start as a background daemon or foreground application."
;
static const char usage_file[] =
  "Read option file before command line options.\n"
"#"
;
static const char usage_help[] =
  "Write the option summary to standard output and exit. The output\n"
"# is suitable for use as an option file. For Windows this option\n"
"# can be assigned a file path string to save the output to a file,\n"
"# eg. help=./" _NAME ".cf.txt\n"
"#"
;
static const char usage_info[] =
  "Write the configuration and compile time options to standard output\n"
"# and exit.\n"
"#"
;
static const char usage_quit[] =
  "Quit an already running instance and exit.\n"
"#"
;
static const char usage_restart[] =
  "Terminate an already running instance before starting.\n"
"#"
;
static const char usage_restart_if[] =
  "Only restart when there is a previous instance running.\n"
"#"
;
static const char usage_service[] =
  "Remove or add Windows service.\n"
"#"
;

Option opt_title		= { "",				NULL,		usage_title };
Option opt_syntax		= { "",				NULL,		usage_syntax };
Option opt_daemon		= { "daemon",			"+",		usage_daemon };
Option opt_file			= { "file", 			CF_FILE,	usage_file };
Option opt_help			= { "help", 			NULL,		usage_help };
Option opt_info			= { "info", 			NULL,		usage_info };
#ifdef NOT_YET
Option opt_quit			= { "quit", 			NULL,		usage_quit };
Option opt_restart		= { "restart", 			NULL,		usage_restart };
Option opt_restartIf		= { "restart-if", 		NULL,		usage_restart_if };
Option opt_service		= { "service",			NULL,		usage_service };
#endif
Option opt_version		= { "version",			NULL,		"Show version and copyright." };

Option opt_script		= { "script",			CF_LUA,		"Pathname of Lua script." };

/***********************************************************************
 *** Common SMTP Server Options
 ***********************************************************************/

static const char usage_smtp_command_timeout[] =
  "SMTP command timeout in seconds.\n"
"#"
;
static const char usage_smtp_accept_timeout[] =
  "SMTP client connection timeout in seconds.\n"
"#"
;
static const char usage_smtp_data_line_timeout[] =
  "SMTP data line timeout in seconds after DATA while collecting\n"
"# message content.\n"
"#"
;
static const char usage_smtp_dot_timeout[] =
  "Timeout in seconds to wait for a reply to the SMTP final dot sent\n"
"# to the forward hosts.\n"
"#"
;
static const char usage_smtp_error_url[] =
  "Specify the base URL to include in SMTP error replies. Used to\n"
"# direct the sender to a more complete description of the error.\n"
"# The URL is immediately followed by \"Ennn\", where nnn is the\n"
"# error message number. This URL should be as short as possible.\n"
"# Set to the empty string to disable.\n"
"#"
;
static const char usage_smtp_server_port[] =
  "SMTP server port number to listen on.\n"
"#"
;
static const char usage_smtp_server_queue[] =
  "SMTP server connection queue size. This setting is OS specific and\n"
"# tells the kernel how many unanswered connections on the socket it\n"
"# should allow.\n"
"#"
;
static const char usage_smtp_smart_host[] =
  "Host name or address and optional port number of where to forward\n"
"# all SMTP traffic for inbound delivery or outbound routing.\n"
"#"
;
static const char usage_smtp_max_size[] =
  "Maximum size in bytes a message can be. Specify zero to disable.\n"
"#"
;
static const char usage_smtp_xclient[] =
  "When set, enable SMTP XCLIENT support.\n"
"#"
;
static const char usage_spool_dir[] =
  "When defined, spool messages to this directory.\n"
"#"
;

Option opt_smtp_accept_timeout	= { "smtp-accept-timeout",	"60",				usage_smtp_accept_timeout };
Option opt_smtp_command_timeout	= { "smtp-command-timeout",	QUOTE(SMTP_COMMAND_TO),		usage_smtp_command_timeout };
Option opt_smtp_data_timeout 	= { "smtp-data-timeout",	QUOTE(SMTP_DATA_BLOCK_TO),	usage_smtp_data_line_timeout };
Option opt_smtp_dot_timeout	= { "smtp-dot-timeout",		QUOTE(SMTP_DOT_TO), 		usage_smtp_dot_timeout };
Option opt_smtp_error_url	= { "smtp-error-url",		"",				usage_smtp_error_url };
Option opt_smtp_max_size	= { "smtp-max-size",		"0",				usage_smtp_max_size };
Option opt_smtp_server_port	= { "smtp-server-port",		QUOTE(SMTP_PORT), 		usage_smtp_server_port };
Option opt_smtp_server_queue	= { "smtp-server-queue",	"20",				usage_smtp_server_queue };
Option opt_smtp_smart_host	= { "smtp-smart-host", 		"",		 		usage_smtp_smart_host };
Option opt_smtp_xclient		= { "smtp-xclient", 		"-", 				usage_smtp_xclient };
Option opt_spool_dir		= { "spool-dir",		"/tmp",				usage_spool_dir };

static const char usage_smtp_default_at_dot[] =
  "The default reply to send at dot, when no alternative reply given.\n"
"# The option can be set to 250 (accept), 451 (try again later), or\n"
"# 550 (reject). This option is intended for testing and will typically\n"
"# be overridden by the Lua hook.dot() function.\n"
"#"
;
Option opt_smtp_default_at_dot	= { "smtp-default-at-dot",	QUOTE(451),		usage_smtp_default_at_dot };

static const char usage_rfc2920_pipelining[] =
  "Enables support for RFC 2920 SMTP command pipelining when the client\n"
"# sends EHLO.\n"
"#"
;
static const char usage_rfc2920_pipelining_reject[] =
  "When set and there is early input before the welcome banner, or HELO\n"
"# is used and commands are pipelined, or EHLO PIPELINING is disabled\n"
"# and commands are pipelined, then reject and drop the connection.\n"
"#"
;
static const char usage_rfc2821_angle_brackets[] =
  "Strict RFC 2821 grammar requirement for mail addresses be surrounded\n"
"# by angle brackets in MAIL FROM: and RCPT TO: commands.\n"
"#"
;
static const char usage_rfc2821_local_length[] =
  "Strict RFC 2821 local-part length limit."
;
static const char usage_rfc2821_domain_length[] =
  "Strict RFC 2821 domain name length limit."
;
static const char usage_rfc2821_literal_plus[] =
  "Treat plus-sign as itself; not a sendmail plussed address."
;

Option opt_rfc2920_pipelining		= { "rfc2920-pipelining-enable","-",		usage_rfc2920_pipelining };
Option opt_rfc2920_pipelining_reject	= { "rfc2920-pipelining-reject","-",		usage_rfc2920_pipelining_reject };
Option opt_rfc2821_angle_brackets	= { "rfc2821-angle-brackets",	"+", 		usage_rfc2821_angle_brackets };
Option opt_rfc2821_local_length		= { "rfc2821-local-length", 	"-", 		usage_rfc2821_local_length };
Option opt_rfc2821_domain_length	= { "rfc2821-domain-length", 	"-", 		usage_rfc2821_domain_length };
Option opt_rfc2821_literal_plus		= { "rfc2821-literal-plus", 	"-", 		usage_rfc2821_literal_plus };

/***********************************************************************
 *** Common Verbose Settings
 ***********************************************************************/

static const char usage_verbose[] =
  "What to write to mail log. Specify a white space separated list of words:"
;

Option opt_verbose	= { "verbose",	"+warn +info", usage_verbose };

/* Verbose levels */
Option verb_warn	= { "warn",		"-", empty };
Option verb_info	= { "info",		"-", empty };
Option verb_trace	= { "trace",		"-", empty };
Option verb_debug	= { "debug",		"-", empty };

/* Verbose SMTP command */
Option verb_connect	= { "connect",		"-", empty };
Option verb_helo	= { "helo",		"-", empty };
Option verb_auth	= { "auth",		"-", empty };
Option verb_mail	= { "mail",		"-", empty };
Option verb_rcpt	= { "rcpt",		"-", empty };
Option verb_data	= { "data",		"-", empty };
Option verb_noop	= { "noop",		"-", empty };
Option verb_rset	= { "rset",		"-", empty };

/* Verbose SMTP client. */
Option verb_smtp	= { "smtp",		"-", empty };
Option verb_dns		= { "dns",		"-", empty };
#ifdef NOT_USED
Option verb_smtp_data	= { "smtp-data",	"-", empty };
Option verb_smtp_dot	= { "smtp-dot",		"-", empty };
#endif

/***********************************************************************
 ***
 ***********************************************************************/

Option *opt_table[] = {
	&opt_title,
	&opt_syntax,
	&opt_daemon,
	&opt_file,
	&opt_help,
	&opt_info,
#ifdef NOT_YET
	&opt_quit,
	&opt_restart,
	&opt_restartIf,
	&opt_service,
#endif
	&opt_script,
	&opt_version,

	PDQ_OPTIONS_TABLE,

	&opt_rfc2920_pipelining,
	&opt_rfc2920_pipelining_reject,
	&opt_rfc2821_angle_brackets,
	&opt_rfc2821_local_length,
	&opt_rfc2821_domain_length,
	&opt_rfc2821_literal_plus,

	&opt_smtp_accept_timeout,
	&opt_smtp_command_timeout,
	&opt_smtp_data_timeout,
	&opt_smtp_default_at_dot,
	&opt_smtp_dot_timeout,
	&opt_smtp_error_url,
	&opt_smtp_max_size,
	&opt_smtp_server_port,
	&opt_smtp_server_queue,
	&opt_smtp_smart_host,
	&opt_smtp_xclient,

	&opt_spool_dir,
	&opt_verbose,

	NULL
};

Option *verb_table[] = {
	&verb_warn,
	&verb_info,
	&verb_trace,
	&verb_debug,

	&verb_connect,
	&verb_helo,
	&verb_auth,
	&verb_mail,
	&verb_rcpt,
	&verb_data,
	&verb_noop,
	&verb_rset,

	&verb_dns,
	&verb_smtp,
#ifdef NOT_USED
	&verb_smtp_data,
	&verb_smtp_dot,
#endif
	NULL
};

static unsigned rand_seed;
static int parse_path_flags;
static SocketEvents main_loop;
static char my_host_name[SMTP_DOMAIN_LENGTH+1];
void client_send(SmtpCtx *ctx, const char *fmt, ...);

static lua_State *lua_live;	/* Current master state. */
static lua_State *lua_dying;	/* Previous master state to gc once all threads complete. */

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

void
dns_io_cb(SocketEvents *loop, SocketEvent *event)
{
	SocketEvent *client_event = event->data;
	SmtpCtx *ctx = client_event->data;

	TRACE_CTX(ctx, 000);

	SETJMP_PUSH(&ctx->on_error);
	if (SIGSETJMP(ctx->on_error, 1) != 0) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "close %s cc=%ld", LOG_ID(ctx), ctx->addr.data, VectorLength(loop->events)-1);
		if (client_event->on.error != NULL)
			(*client_event->on.error)(loop, client_event);
	} else {
		(*ctx->state)(loop, client_event);
	}
	SETJMP_POP(&ctx->on_error);

}

void
dns_error_cb(SocketEvents *loop, SocketEvent *event)
{
	long ms;
	time_t now;
	SocketEvent *client_event = event->data;
	SmtpCtx *ctx = client_event->data;

	TRACE_CTX(ctx, 000);

	if (errno == ETIMEDOUT) {
		/* Double the timeout for next iteration. */
		ms = socketGetTimeout(ctx->pdq_socket);
		ms += ms;

		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ms=%ld", LOG_ID(ctx), __FUNCTION__, ms);

		(void) time(&now);
		socketEventExpire(event, &now, ms);
		socketSetTimeout(ctx->pdq_socket, ms);

		(*ctx->state)(loop, client_event);
	}
}

int
dns_wait(SmtpCtx *ctx, int wait_all)
{
	PDQ_rr *head;

	errno = 0;

	TRACE_CTX(ctx, 000);

	if (pdqQueryIsPending(ctx->pdq)) {
		if ((head = pdqPoll(ctx->pdq, 10)) != NULL) {
			if (head->rcode == PDQ_RCODE_TIMEDOUT) {
				if (verb_debug.value)
					pdqListLog(head);
				pdqListFree(head);
			} else {
				ctx->pdq_answer = pdqListAppend(ctx->pdq_answer, head);
			}
		}
		if (pdqQueryIsPending(ctx->pdq) && (wait_all || ctx->pdq_answer == NULL))
			return errno = EAGAIN;
	}

	ctx->pdq_stop_after = 0;

	return errno;
}

void
dns_reset(SmtpCtx *ctx)
{
	socketSetTimeout(ctx->pdq_socket, PDQ_TIMEOUT_START * 1000);
	ctx->pdq_stop_after = time(NULL) + pdqGetTimeout(ctx->pdq);
	pdqListFree(ctx->pdq_answer);
	ctx->pdq_answer = NULL;
}

int
dns_open(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	if ((ctx->pdq = pdqOpen()) == NULL) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error0;
	}

	/* Create a Socket2 wrapper for socketEventInit(). */
	if ((ctx->pdq_socket = socketFdOpen(pdqGetFd(ctx->pdq))) == NULL) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error1;
	}

	socketSetTimeout(ctx->pdq_socket, PDQ_TIMEOUT_START * 1000);

	/* Create an event for the DNS lookup. */
	socketEventInit(&ctx->pdq_event, ctx->pdq_socket, SOCKET_EVENT_READ);

	ctx->pdq_event.data = event;
	ctx->pdq_event.on.io = dns_io_cb;
	ctx->pdq_event.on.error = dns_error_cb;

	if (socketEventAdd(loop, &ctx->pdq_event)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		goto error2;
	}

	/* Disable the client event until the DNS lookup completes. */
	ctx->is_enabled = socketEventGetEnable(event);
	socketEventSetEnable(event, 0);
	ctx->pdq_answer = NULL;

	return 0;
error2:
	/* Don't use socketFdClose() or socketClose(). */
	free(ctx->pdq_socket);
	ctx->pdq_socket = NULL;
error1:
	pdqClose(ctx->pdq);
	ctx->pdq = NULL;
error0:
	return -1;
}

void
dns_close(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	if (ctx->pdq != NULL) {
		socketEventSetEnable(event, ctx->is_enabled);
		pdqListFree(ctx->pdq_answer);

		/* Don't use socketFdClose() or socketClose(). pdqClose() will
		 * close the socket file descriptor, so just free the Socket2
		 * wrapper.
		 */
		ctx->pdq_event.socket = NULL;
		socketEventRemove(loop, &ctx->pdq_event);
		free(ctx->pdq_socket);

		pdqClose(ctx->pdq);
		ctx->pdq = NULL;
	}
}

/***********************************************************************
 *** SMTP Client Support
 ***********************************************************************/

static
PT_THREAD(mx_read(SmtpCtx *ctx))
{
	int ch;
	long length;
	size_t offset;
	char *buffer, **table;

	if (SMTP_IS_ERROR(ctx->mx_read.smtp_rc))
		PT_EXIT(&ctx->mx_read.pt);

	PT_BEGIN(&ctx->mx_read.pt);

	ctx->mx_read.size = 0;
	ctx->mx_read.length = 0;
	ctx->mx_read.line_no = 0;
	ctx->mx_read.line_max = 0;
	ctx->mx_read.lines = NULL;
	ctx->mx_read.smtp_rc = 0;

	do {
		do {
			/* Enlarge table/buffer? */
			if (ctx->mx_read.line_max <= ctx->mx_read.line_no
			|| ctx->mx_read.size <= ctx->mx_read.length + SMTP_REPLY_LINE_LENGTH) {
				if ((table = realloc(ctx->mx_read.lines, sizeof (char *) * (ctx->mx_read.line_max + 11) + ctx->mx_read.size + SMTP_REPLY_LINE_LENGTH)) == NULL)
					goto error1;

				table[0] = 0;
				ctx->mx_read.lines = table;
				memmove(&table[ctx->mx_read.line_max + 11], &table[ctx->mx_read.line_max + 1], ctx->mx_read.length);

				ctx->mx_read.line_max += 10;
				ctx->mx_read.size += SMTP_REPLY_LINE_LENGTH;
			}

			/* Wait for input ready. */
			PT_YIELD(&ctx->mx_read.pt);

			/* Read input. */
			buffer = (char *) &ctx->mx_read.lines[ctx->mx_read.line_max + 1];
			length = socketRead(ctx->mx, buffer+ctx->mx_read.length, ctx->mx_read.size-ctx->mx_read.length);

			switch (length) {
			case SOCKET_EOF:
				ctx->mx_read.smtp_rc = SMTP_ERROR_EOF;
				goto error1;
			case SOCKET_ERROR:
				ctx->mx_read.smtp_rc = SMTP_ERROR;
				goto error1;
			}

			ctx->mx_read.length += length;

			/* Wait for a complete line. */
		} while (buffer[ctx->mx_read.length-1] != '\n');

		offset = (size_t) ctx->mx_read.lines[ctx->mx_read.line_no];

		/* Identify start of each line in the chunk read. */
		do {
			length = strcspn(buffer+offset, "\r\n");
			ch = buffer[offset+3];

			switch (buffer[offset+length]) {
			case '\r':
				if (buffer[offset+length+1] == '\n') {
					buffer[offset+length++] = '\0';
			case '\n':
					buffer[offset+length++] = '\0';
				}
			}
			if (verb_smtp.value)
				syslog(LOG_DEBUG, LOG_FMT "<< %lu:%s", LOG_TRAN(ctx), length, buffer+offset);
			/* Save only the offset of the line in the buffer, since
			 * the line pointer table and buffer might be reallocated
			 */
			ctx->mx_read.lines[ctx->mx_read.line_no++] = (char *) offset;
			offset += length;
		} while (offset < ctx->mx_read.length);
	} while (ch == '-');

	/* Add in the base of the buffer to each line's offset. */
	for (ch = 0; ch < ctx->mx_read.line_no; ch++) {
		ctx->mx_read.lines[ch] = buffer + (int) ctx->mx_read.lines[ch];
	}
	ctx->mx_read.lines[ch] = NULL;

	if (0 < ctx->mx_read.line_no) {
		ctx->mx_read.smtp_rc = strtol(ctx->mx_read.lines[0], NULL, 10);
		PT_EXIT(&ctx->mx_read.pt);
	}
error1:
	free(ctx->mx_read.lines);
	ctx->mx_read.lines = NULL;

	PT_END(&ctx->mx_read.pt);
}

void
mx_io_cb(SocketEvents *loop, SocketEvent *event)
{
	SocketEvent *client_event = event->data;
	SmtpCtx *ctx = client_event->data;

	TRACE_CTX(ctx, 000);

	SETJMP_PUSH(&ctx->on_error);
	if (SIGSETJMP(ctx->on_error, 1) != 0) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "close %s cc=%ld", LOG_ID(ctx), ctx->addr.data, VectorLength(loop->events)-1);
		if (client_event->on.error != NULL)
			(*client_event->on.error)(loop, client_event);
	} else {
		(*ctx->state)(loop, client_event);
	}
	SETJMP_POP(&ctx->on_error);
}

void
mx_error_cb(SocketEvents *loop, SocketEvent *event)
{
	SocketEvent *client_event = event->data;
	SmtpCtx *ctx = client_event->data;

	TRACE_CTX(ctx, 000);

	ctx->mx_read.smtp_rc = SMTP_ERROR_IO;
	(*ctx->state)(loop, client_event);
}

static int
mx_open(SmtpCtx *ctx, const char *host)
{
	if ((ctx->mx = socketConnect(host, SMTP_PORT, opt_smtp_accept_timeout.value)) == NULL) {
		syslog(LOG_ERR, LOG_FMT "%s: %s (%d)", LOG_TRAN(ctx), host, strerror(errno), errno);
		return -1;
	}

	(void) fileSetCloseOnExec(socketGetFd(ctx->mx), 1);
	(void) socketSetNonBlocking(ctx->mx, 1);
	(void) socketSetLinger(ctx->mx, 0);

	/* Create an event for the forward host. */
	socketEventInit(&ctx->mx_event, ctx->mx, SOCKET_EVENT_READ);

	ctx->mx_event.data = ctx->event;
	ctx->mx_event.on.io = mx_io_cb;
	ctx->mx_event.on.error = mx_error_cb;

	if (socketEventAdd(ctx->loop, &ctx->mx_event)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		socketClose(ctx->mx);
		ctx->mx = NULL;
		return -1;
	}

	/* Disable the client event until the forwarding completes. */
	ctx->is_enabled = socketEventGetEnable(ctx->event);
	socketEventSetEnable(ctx->event, 0);

	return 0;
}

static void
mx_close(SmtpCtx *ctx)
{
	TRACE_CTX(ctx, 000);

	if (ctx->mx != NULL) {
		socketEventSetEnable(ctx->event, ctx->is_enabled);
		socketEventRemove(ctx->loop, &ctx->mx_event);
		ctx->mx = NULL;
	}
}

static long
mx_print(SmtpCtx *ctx, const char *line, size_t length)
{
	long sent;

	/* Have we already had an error? */
	if (SMTP_IS_ERROR(ctx->mx_read.smtp_rc))
		return ctx->mx_read.smtp_rc;

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT ">> %lu:%s", LOG_TRAN(ctx), (unsigned long) length, line);

	if ((sent = socketWrite(ctx->mx, (char *) line, length)) < 0) {
		syslog(LOG_ERR, LOG_FMT "%s: %s (%d)", LOG_TRAN(ctx), opt_smtp_smart_host.string, strerror(errno), errno);
		ctx->mx_read.smtp_rc = SMTP_ERROR;
	}

	return sent;
}

static long
mx_vprintf(SmtpCtx *ctx, const char *fmt, va_list args)
{
	int length;
	char line[SMTP_TEXT_LINE_LENGTH+1];

	length = vsnprintf(line, sizeof (line), fmt, args);

	return mx_print(ctx, line, length);
}

static long
mx_printf(SmtpCtx *ctx, const char *fmt, ...)
{
	long rc;
	va_list args;

	va_start(args, fmt);
	rc = mx_vprintf(ctx, fmt, args);
	va_end(args);

	return rc;
}

/***********************************************************************
 *** Lua support functions
 ***********************************************************************/

static void
lua_table_set_integer(lua_State *L, int table_index, const char *name, lua_Integer value)
{
	lua_pushinteger(L, value);
	lua_setfield(L, table_index - (table_index < 0), name);
}

static void
lua_table_set_string(lua_State *L, int table_index, const char *name, const char *value)
{
	lua_pushstring(L, value);
	lua_setfield(L, table_index - (table_index < 0), name);
}

static void
lua_table_clear(lua_State *L, int table_index, const char *name)
{
	lua_pushnil(L);
	lua_setfield(L, table_index - (table_index < 0), name);
}

/*
 * Remove the last element from array.
 */
static void
lua_array_pop(lua_State *L, int table_index)
{
	size_t size;

	lua_pushnil(L);
	size = lua_objlen(L, table_index - (table_index < 0));
	lua_rawseti(L, table_index - (table_index < 0), size);
}

/*
 * Push the top of the stack onto the end of the array.
 */
static void
lua_array_push(lua_State *L, int table_index)
{
	size_t size;

	size = lua_objlen(L, table_index);
	lua_rawseti(L, table_index, size+1);
}

static void
lua_array_push_integer(lua_State *L, int table_index, lua_Integer value)
{
	size_t size;

	lua_pushinteger(L, value);
	size = lua_objlen(L, table_index - (table_index < 0));
	lua_rawseti(L, table_index - (table_index < 0), size+1);
}

static void
lua_array_push_string(lua_State *L, int table_index, const char *value)
{
	size_t size;

	lua_pushstring(L, value);
	size = lua_objlen(L, table_index - (table_index < 0));
	lua_rawseti(L, table_index - (table_index < 0), size+1);
}

static void
lua_pushthread2(lua_State *L, lua_State *thread)
{
	/* lua_pushthread is really wierd pushing itself onto its
	 * own stack instead of a specific stack. So I move it.
	 */
	(void) lua_pushthread(thread);
	lua_xmove(thread, L, 1);
}

static SmtpCtx *
lua_getctx(lua_State *L)
{
	SmtpCtx *ctx;

	lua_getglobal(L, "__ctx");
	ctx = lua_touserdata(L, -1);
	lua_pop(L, 1);

	return ctx;
}

/***********************************************************************
 *** Lua Syslog API
 ***********************************************************************/

struct map_integer {
	const char *name;
	lua_Integer value;
};

static struct map_integer syslog_constants[] = {
	{ "LOG_EMERG", 		LOG_EMERG },
	{ "LOG_ALERT", 		LOG_ALERT },
	{ "LOG_CRIT", 		LOG_CRIT },
	{ "LOG_ERR", 		LOG_ERR },
	{ "LOG_WARNING", 	LOG_WARNING },
	{ "LOG_NOTICE", 	LOG_NOTICE },
	{ "LOG_INFO", 		LOG_INFO },
	{ "LOG_DEBUG", 		LOG_DEBUG },

	{ "LOG_KERN", 		LOG_KERN },
	{ "LOG_USER", 		LOG_USER },
	{ "LOG_MAIL", 		LOG_MAIL },
	{ "LOG_DAEMON", 	LOG_DAEMON },
	{ "LOG_AUTH", 		LOG_AUTH },
	{ "LOG_SYSLOG", 	LOG_SYSLOG },
	{ "LOG_LPR", 		LOG_LPR },
	{ "LOG_NEWS", 		LOG_NEWS },
	{ "LOG_UUCP", 		LOG_UUCP },
	{ "LOG_CRON", 		LOG_CRON },
	{ "LOG_AUTHPRIV", 	LOG_AUTHPRIV },
	{ "LOG_FTP", 		LOG_FTP },
	{ "LOG_LOCAL0", 	LOG_LOCAL0 },
	{ "LOG_LOCAL1", 	LOG_LOCAL1 },
	{ "LOG_LOCAL2", 	LOG_LOCAL2 },
	{ "LOG_LOCAL3", 	LOG_LOCAL3 },
	{ "LOG_LOCAL4", 	LOG_LOCAL4 },
	{ "LOG_LOCAL5", 	LOG_LOCAL5 },
	{ "LOG_LOCAL6", 	LOG_LOCAL6 },
	{ "LOG_LOCAL7", 	LOG_LOCAL7 },

	{ "LOG_PID", 		LOG_PID },
	{ "LOG_CONS", 		LOG_CONS },
	{ "LOG_ODELAY", 	LOG_ODELAY },
	{ "LOG_NDELAY", 	LOG_NDELAY },
	{ "LOG_NOWAIT", 	LOG_NOWAIT },
	{ "LOG_PERROR", 	LOG_PERROR },

	{ NULL, 0 }
};

/**
 * syslog.openlog(string, syslog.LOG_PID or syslog.LOG_NDELAY, syslog.LOG_USER);
 */
static int
lua_openlog(lua_State *L)
{
	int options = luaL_optint(L, 2, LOG_PID);
	int facility = luaL_optint(L, 3, LOG_USER);
	const char *ident = luaL_checkstring(L, 1);

	openlog(ident, options, facility);

	return 0;
}

/**
 * syslog.syslog(syslog.LOG_INFO, string);
 */
static int
lua_syslog(lua_State *L)
{
	int level;
	SmtpCtx *ctx;

	level = luaL_optint(L, 1, LOG_DEBUG);

	if ((ctx = lua_getctx(L)) == NULL) {
		/* Master state has no __ctx. */
		syslog(level, "%s", luaL_checkstring(L, 2));
	} else {
		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d L=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua_thread, (long) L);
		syslog(
			level, LOG_FMT "%s",
			(*ctx->id_trans == '\0') ? LOG_ID(ctx) : LOG_TRAN(ctx),
			luaL_checkstring(L, 2)
		);
	}

	return 0;
}

/**
 * syslog.error(message);
 *
 * This function can be used for lua_pcall() errfunc.
 */
static int
lua_log_error(lua_State *L)
{
	lua_pushinteger(L, LOG_ERR);	/* msg -- msg LOG_ERR */
	lua_insert(L, -2);		/* msg LOG_ERR -- LOG_ERR msg */

	return lua_syslog(L);
}

/**
 * syslog.info(message);
 *
 * This function can be used for lua_pcall() errfunc.
 */
static int
lua_log_info(lua_State *L)
{
	lua_pushinteger(L, LOG_INFO);	/* msg -- msg LOG_INFO */
	lua_insert(L, -2);		/* msg LOG_INFO -- LOG_INFO msg */

	return lua_syslog(L);
}

/**
 * syslog.debug(message);
 *
 * This function can be used for lua_pcall() errfunc.
 */
static int
lua_log_debug(lua_State *L)
{
	lua_pushinteger(L, LOG_DEBUG);	/* msg -- msg LOG_DEBUG */
	lua_insert(L, -2);		/* msg LOG_DEBUG -- LOG_DEBUG msg */

	return lua_syslog(L);
}

/**
 * syslog.closelog();
 */
static int
lua_closelog(lua_State *L)
{
	closelog();

	return 0;
}

static const luaL_Reg lua_log_package[] = {
	{ "open", 	lua_openlog },
	{ "log", 	lua_syslog },
	{ "close", 	lua_closelog },
	{ "error", 	lua_log_error },
	{ "info",	lua_log_info },
	{ "debug",	lua_log_debug },
	{ NULL, NULL },
};

static void
lua_define_syslog(lua_State *L)
{
	struct map_integer *map;

	luaL_register(L, "syslog", lua_log_package);	/* syslog */

	for (map = syslog_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);
	}

	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua DNS API
 ***********************************************************************/

int dns_open(SocketEvents *loop, SocketEvent *event);
void dns_reset(SmtpCtx *ctx);
int dns_wait(SmtpCtx *ctx, int wait_all);

static struct map_integer dns_class_constants[] = {
	{ "IN",			PDQ_CLASS_IN },
	{ "CS",			PDQ_CLASS_CS },
	{ "CH",			PDQ_CLASS_CH },
	{ "HS",			PDQ_CLASS_HS },
	{ "ANY",		PDQ_CLASS_ANY	},
	{ NULL,			0 }
};

static struct map_integer dns_type_constants[] = {
	{ "A",			PDQ_TYPE_A	},
	{ "NS",			PDQ_TYPE_NS	},
	{ "CNAME",		PDQ_TYPE_CNAME	},
	{ "SOA",		PDQ_TYPE_SOA	},
	{ "NULL",		PDQ_TYPE_NULL	},
	{ "PTR",		PDQ_TYPE_PTR	},
	{ "HINFO",		PDQ_TYPE_HINFO	},
	{ "MINFO",		PDQ_TYPE_MINFO	},
	{ "MX",			PDQ_TYPE_MX	},
	{ "TXT",		PDQ_TYPE_TXT	},
	{ "AAAA",		PDQ_TYPE_AAAA	},
	{ "DNAME",		PDQ_TYPE_DNAME	},
	{ "ANY",		PDQ_TYPE_ANY	},
	{ NULL, 		0 }
};

static struct map_integer dns_rcode_constants[] = {
	{ "OK",			PDQ_RCODE_OK			},
	{ "FORMAT",		PDQ_RCODE_FORMAT		},
	{ "SERVFAIL",		PDQ_RCODE_SERVER		},
	{ "NXDOMAIN",		PDQ_RCODE_UNDEFINED		},
	{ "NOT_IMPLEMENTED",	PDQ_RCODE_NOT_IMPLEMENTED	},
	{ "REFUSED",		PDQ_RCODE_REFUSED		},
	{ "ERRNO",		PDQ_RCODE_ERRNO			},
	{ "TIMEDOUT",		PDQ_RCODE_TIMEDOUT		},
	{ NULL,			0 }
};

/**
 * dns.classname(code)
 */
static int
lua_dns_classname(lua_State *L)
{
	lua_pushstring(L, pdqClassName(luaL_checkint(L, 1)));

	return 1;
}

/**
 * dns.typename(code)
 */
static int
lua_dns_typename(lua_State *L)
{
	lua_pushstring(L, pdqTypeName(luaL_checkint(L, 1)));

	return 1;
}


/**
 * dns.rcodename(code)
 */
static int
lua_dns_rcodename(lua_State *L)
{
	lua_pushstring(L, pdqRcodeName(luaL_checkint(L, 1)));

	return 1;
}

/**
 * dns.open()
 */
static int
lua_dns_open(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_getctx(L)) != NULL)
		(void) dns_open(ctx->loop, ctx->event);

	return 0;
}

/**
 * dns.reset()
 */
static int
lua_dns_reset(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_getctx(L)) != NULL)
		dns_reset(ctx);

	return 0;
}

/**
 * dns.query(class, type, name)
 */
static int
lua_dns_query(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_getctx(L)) == NULL)
		return 0;

	if (pdqQuery(ctx->pdq, luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkstring(L, 3), NULL)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
	}

	return 0;
}

/**
 * rcode, table = dns.wait(all_flag)
 */
static int
lua_dns_wait(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_getctx(L)) != NULL)
		ctx->pdq_wait_all = luaL_checkint(L, 1);

	return lua_yield(L, 0);
}

static const luaL_Reg lua_dns_package[] = {
	{ "open", 	lua_dns_open },
	{ "reset", 	lua_dns_reset },
	{ "query", 	lua_dns_query },
	{ "wait", 	lua_dns_wait },
	{ "classname",	lua_dns_classname },
	{ "typename",	lua_dns_typename },
	{ "rcodename",	lua_dns_rcodename },
	{ NULL, NULL },
};

static void
lua_define_dns(lua_State *L)
{
	struct map_integer *map;

	luaL_register(L, "dns", lua_dns_package);	/* dns */

	lua_newtable(L);				/* dns class */
	for (map = dns_class_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);
	}
	lua_setfield(L, -2, "class");			/* dns */

	lua_newtable(L);				/* dns type */
	for (map = dns_type_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);
	}
	lua_setfield(L, -2, "type");			/* dns */

	lua_newtable(L);				/* dns rcode */
	for (map = dns_rcode_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);
	}
	lua_setfield(L, -2, "rcode");			/* dns */

	lua_pop(L, 1);					/* -- */
}

static int
lua_dns_getresult(lua_State *L, SmtpCtx *ctx)
{
	PDQ_rr *rr;

	if (ctx->pdq_answer == NULL) {
		lua_pushinteger(L, PDQ_RCODE_ERRNO);
		errno = EFAULT;
		return 1;
	}

	lua_pushinteger(L, ctx->pdq_answer->rcode);	/* rcode */

	lua_newtable(L);	/* rcode table */
	lua_newtable(L);	/* rcode table extra */
	lua_newtable(L);	/* rcode table extra authority */
	lua_newtable(L);	/* rcode table extra authority answer */

	for (rr = ctx->pdq_answer; rr != NULL; rr = rr->next) {
		if (rr->rcode == PDQ_RCODE_OK) {
			lua_newtable(L);	/* rcode table extra authority answer rr */

			lua_pushlstring(L, rr->name.string.value, rr->name.string.length);
			lua_setfield(L, -2, "name");
			lua_pushinteger(L, rr->class);
			lua_setfield(L, -2, "class");
			lua_pushinteger(L, rr->type);
			lua_setfield(L, -2, "type");
			lua_pushinteger(L, rr->ttl);
			lua_setfield(L, -2, "ttl");

			switch (rr->type) {
			case PDQ_TYPE_A:
			case PDQ_TYPE_AAAA:
				lua_pushlstring(L, ((PDQ_A *) rr)->address.string.value, ((PDQ_A *) rr)->address.string.length);
				lua_setfield(L, -2, "value");
				break;

			case PDQ_TYPE_MX:
				lua_pushinteger(L, ((PDQ_MX *) rr)->preference);
				lua_setfield(L, -2, "preference");
				/*@fallthrough@*/

			case PDQ_TYPE_CNAME:
			case PDQ_TYPE_DNAME:
			case PDQ_TYPE_PTR:
			case PDQ_TYPE_NS:
				lua_pushlstring(L, ((PDQ_NS *) rr)->host.string.value, ((PDQ_NS *) rr)->host.string.length);
				lua_setfield(L, -2, "value");
				break;

			case PDQ_TYPE_TXT:
				lua_pushlstring(L, ((PDQ_TXT *) rr)->text.value, ((PDQ_TXT *) rr)->text.length);
				lua_setfield(L, -2, "value");
				break;

			case PDQ_TYPE_SOA:
				lua_pushlstring(L, ((PDQ_SOA *) rr)->mname.string.value, ((PDQ_SOA *) rr)->mname.string.length);
				lua_setfield(L, -2, "mname");
				lua_pushlstring(L, ((PDQ_SOA *) rr)->rname.string.value, ((PDQ_SOA *) rr)->rname.string.length);
				lua_setfield(L, -2, "rname");
				lua_pushinteger(L, ((PDQ_SOA *) rr)->serial);
				lua_setfield(L, -2, "serial");
				lua_pushinteger(L, ((PDQ_SOA *) rr)->refresh);
				lua_setfield(L, -2, "refresh");
				lua_pushinteger(L, ((PDQ_SOA *) rr)->retry);
				lua_setfield(L, -2, "retry");
				lua_pushinteger(L, ((PDQ_SOA *) rr)->expire);
				lua_setfield(L, -2, "expire");
				lua_pushinteger(L, ((PDQ_SOA *) rr)->minimum);
				lua_setfield(L, -2, "minimum");
				break;
			}

			/* See PDQ_SECTION_ indices for why this works. */
			lua_array_push(L, -rr->section); /* rcode table extra authority answer rr */
		}
	}

	lua_setfield(L, -4, "answer");		/* rcode table extra authority */
	lua_setfield(L, -3, "authority");	/* rcode table extra */
	lua_setfield(L, -2, "extra");		/* rcode table */

	return 2;
}

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * string = net.reverse_ip(address, suffix_flag)
 */
static int
lua_net_reverseip(lua_State *L)
{
	long length;
	char buffer[DOMAIN_STRING_LENGTH];

	length = reverseIp(luaL_checkstring(L, 1), buffer, sizeof (buffer), luaL_checkint(L, 2));
	lua_pushlstring(L, buffer, length);

	return 1;
}

static const luaL_Reg lua_net_package[] = {
	{ "reverseip", 	lua_net_reverseip },
	{ NULL, NULL },
};

static void
lua_define_net(lua_State *L)
{
	luaL_register(L, "net", lua_net_package);	/* net */
	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua Interface
 ***
 *** Setup a master state (interpreter) within which we create all our
 *** globals and "spawn" Lua threads per client connection.
 ***********************************************************************/

#define LUA_CMD_INIT(fn)	lua_hook_init ## fn
#define LUA_CMD_DEF(fn)		static int LUA_CMD_INIT(fn)(lua_State *L1, SmtpCtx *ctx)

#define LUA_PT_CALL_INIT(fn, init) \
	PT_SPAWN(&ctx->pt, &ctx->lua.pt, lua_hook_do(lua_live, ctx, # fn, init)); \
	/* Assert that the DNS is closed after Lua. */	\
	dns_close(loop, event)

#define LUA_CALL_INIT(fn, init) \
	PT_INIT(&ctx->lua.pt); \
	while (PT_SCHEDULE(lua_hook_do(lua_live, ctx, #fn, init))); \
	dns_close(ctx->loop, ctx->event)

#define LUA_PT_CALL0(fn)	LUA_PT_CALL_INIT(fn, lua_hook_noargs)
#define LUA_PT_CALL(fn)		LUA_PT_CALL_INIT(fn, LUA_CMD_INIT(fn))

#define LUA_CALL0(fn)		LUA_CALL_INIT(fn, lua_hook_noargs)
#define LUA_CALL(fn)		LUA_CALL_INIT(fn, LUA_CMD_INIT(fn))

#define LUA_HOOK_DEFAULT(x)	((x) < 200)
#define LUA_HOOK_OK(x)		(LUA_HOOK_DEFAULT(x) || SMTP_IS_OK(x))

static int
lua_hook_gethook(lua_State *L, const char *fn)
{
	int is_defined;

	lua_getglobal(L, "hook");		/* hook */	\
	lua_getfield(L, -1, fn);		/* hook func */	\
	lua_remove(L, -2);			/* func  */

	if (!(is_defined = lua_isfunction(L, -1)))
		lua_pop(L, 1);			/* -- */

	return is_defined;
}

static lua_State *
lua_hook_getthread(lua_State *L, SmtpCtx *ctx)
{
	lua_State *thread;

	lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua_thread);
	thread = lua_tothread(L, -1);
	lua_pop(L, 1);

	if (thread == NULL)
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua_thread);

	return thread;
}

static int
lua_hook_noargs(lua_State *L, SmtpCtx *ctx)
{
	return 0;
}

static
PT_THREAD(lua_hook_do(lua_State *L, SmtpCtx *ctx, const char *hook, LuaSmtpHookInit initfn))
{
	int nargs;
	LuaCode rc;
	lua_State *L1;
	size_t reply_len;
	const char *reply, *crlf;

	if (L == NULL || (L1 = lua_hook_getthread(L, ctx)) == NULL) {
		PT_EXIT(&ctx->lua.pt);
	}

	PT_BEGIN(&ctx->lua.pt);

	ctx->smtp_rc = 0;
	lua_getglobal(L1, "hook");		/* hook */
	lua_getfield(L1, -1, hook);		/* hook func */

	nargs = (*initfn)(L1, ctx);		/* hook func ... */

	if (!lua_isfunction(L1, -1 - nargs)) {
		lua_pop(L1, lua_gettop(L1));
		PT_EXIT(&ctx->lua.pt);
	}

	lua_pushvalue(L1, LUA_GLOBALSINDEX);
	(void) lua_setfenv(L1, -2 - nargs);

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-before=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua_thread, lua_gettop(L1), (long) L1);

	while ((rc = lua_resume(L1, nargs)) == Lua_YIELD) {
		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-yield=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua_thread, lua_gettop(L1), (long) L1);

		/* Not expecting anything from the yield(). */
		lua_pop(L1, lua_gettop(L1));

		PT_YIELD_UNTIL(&ctx->lua.pt, dns_wait(ctx, ctx->pdq_wait_all) != EAGAIN);
		nargs = lua_dns_getresult(L1, ctx);

		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-resume=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua_thread, lua_gettop(L1), (long) L1);
	}

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-after=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua_thread, lua_gettop(L1), (long) L1);

	if (rc != Lua_OK) {
		syslog(LOG_ERR, LOG_FMT "hook.%s: %s", LOG_ID(ctx), hook, lua_tostring(L1, -1));
		ctx->smtp_rc = SMTP_ERROR;
	} else if (0 < lua_gettop(L1) && (reply = lua_tolstring(L1, -1, &reply_len)) != NULL) {
		crlf = "";
		/* Is the reply missing a CRLF? */
		if (!(1 < reply_len && reply[reply_len-1] == '\n' && reply[reply_len-2] == '\r'))
			crlf = CRLF;
		ctx->smtp_rc = strtol(reply, NULL, 10);
		if (SMTP_IS_VALID(ctx->smtp_rc))
			ctx->reply.length = snprintf(ctx->reply.data, ctx->reply.size, "%s%s", reply, crlf);
	}

	lua_pop(L1, lua_gettop(L1));

	PT_END(&ctx->lua.pt);
}

static LuaCode
lua_hook_endthread(lua_State *L, SmtpCtx *ctx)
{
	/* Unanchor client Lua thread so it can be gc'ed. */
	luaL_unref(L, LUA_REGISTRYINDEX, ctx->lua_thread);

	return Lua_OK;
}

/*
 * value = __index(table, key)

     function gettable_event (table, key)
       local h
       if type(table) == "table" then
         local v = rawget(table, key)
         if v ~= nil then return v end
         h = metatable(table).__index
         if h == nil then return nil end
       else
         h = metatable(table).__index
         if h == nil then
           error(···)
         end
       end
       if type(h) == "function" then
         return (h(table, key))     -- call the handler
       else return h[key]           -- or repeat operation on it
       end
     end


 */
//	if (lua_type(L, -2) == LUA_TTABLE) {		/* table key */
//		lua_pushvalue(L, -1);			/* table key key */
//		lua_rawget(L, -3);			/* table key value */
//		if (!lua_isnil(L, -1))
//			return 1;			/* table key value */
//
//		lua_pop(L, 1);				/* table key */
//		if (!luaL_getmetafield(L, -2, "__index")) {
//			lua_pushnil(L);			/* table key nil */
//			return 1;
//		}
//	} else if (!luaL_getmetafield(L, -2, "__index")) {
//		luaL_where(L, 0);
//		luaL_error(L, "%s: attempt to index global '%s' (a nil value)", luaL_checkstring(L, 2));
//	}
//
////	/* Pull the global environment from the state. */
////	lua_pushvalue(L, LUA_GLOBALSINDEX);		/* table key __index _G */
////	if (!lua_rawequal(L, -2, -1)) {
////		if (luaL_getmetafield(L, -1, "__index")) {	/* table key __index _G _G__index */
////			lua_remove(L, -2);		/* table key __index _G__index */
////			lua_remove(L, -2);		/* table key _G__index */
////		}
////	}
//	if (lua_type(L, -1) == LUA_TFUNCTION) {		/* table key __index */
//		lua_pushvalue(L, -3);			/* table key __index table */
//		lua_pushvalue(L, -3);			/* table key __index table key */
//		if (lua_pcall(L, 2, 1, 0) != 0)
//			lua_error(L);
//		return 1;
//	}
//
//	lua_pop(L, 1);					/* table key */
//	lua_pushvalue(L, -1);				/* table key key */
//	lua_rawget(L, -3);				/* table key value */

//	return 1;

/*
 * value = __index(table, key)
 */
static int
lua_thread_inherit(lua_State *L)
{
	int is_master;

	is_master = lua_pushthread(L);			/* table key L */
	lua_pop(L, 1);					/* table key */

	if (is_master) {
		lua_pushnil(L);				/* table key nil */
	} else {
		lua_pushvalue(L, -1);			/* table key key */
		lua_rawget(L, -3);			/* table key value */
	}

	return 1;
}

static void
lua_thread_setindex(lua_State *L)
{
	lua_pushvalue(L, LUA_GLOBALSINDEX);		/* _G */
	if (!lua_getmetatable(L, -1)) {
		lua_pushvalue(L, -1);			/* _G _G */
		lua_setmetatable(L, -2);		/* _G */
	}
	lua_pushcfunction(L, lua_thread_inherit);	/* _G func */
	lua_setfield(L, -2, "__index");
}

static void
lua_thread_newenv(lua_State *L)
{
	lua_newtable(L); 				/* new_G  */
	lua_pushvalue(L, -1);				/* new_G new_G */
	lua_pushliteral(L, "__index");			/* new_G new_G __index */
	lua_pushvalue(L, LUA_GLOBALSINDEX);		/* new_G new_G __index old_G */
	lua_settable(L, -3);				/* new_G new_G */
	lua_setmetatable(L, -2);			/* new_G */
	lua_replace(L, LUA_GLOBALSINDEX);		/* -- */
}

static void
lua_table_inherit(lua_State *L, int index)
{
	lua_newtable(L);				/* meta */
	lua_pushliteral(L, "__index");			/* meta __index */
	lua_pushvalue(L, LUA_GLOBALSINDEX);		/* meta __index _G */
	lua_settable(L, -3);				/* meta */
	lua_setmetatable(L, index);			/* -- */
}

static LuaCode
lua_hook_newthread(lua_State *L, SmtpCtx *ctx)
{
	lua_State *L1;

	if (L == NULL)
		return Lua_OK;

	/* Create new Lua thread and anchor it. */
	L1 = lua_newthread(L);				/* L1 */
	ctx->lua_thread = luaL_ref(L, LUA_REGISTRYINDEX); /* -- */

	if (L1 == NULL) {
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));
		return Lua_ERRERR;
	}

	lua_thread_newenv(L1);

	/* Add client's context to the new thread for use by C API. */
	lua_pushlightuserdata(L1, ctx);			/* ctx */
	lua_setglobal(L1, "__ctx");			/* -- */

	/* Create client connection table. */
	lua_newtable(L1);				/* client */
//	lua_table_inherit(L, -1);
	lua_table_set_string(L1, -1, "id_sess", ctx->id_sess);
	lua_setglobal(L1, "client");			/* -- */

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s top=%d", LOG_ID(ctx), __FUNCTION__, lua_gettop(L1));

	return Lua_OK;
}

LUA_CMD_DEF(accept)
{
	lua_getglobal(L1, "client");				/* func client */
	if (lua_isnil(L1, -1))
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));

	lua_table_set_integer(L1, -1, "thread", ctx->lua_thread);
	lua_table_set_integer(L1, -1, "port", socketAddressGetPort(&ctx->client->address));

	lua_pushlstring(L1, ctx->addr.data, ctx->addr.length);	/* func client ip */
	lua_pushvalue(L1, -1);					/* func client ip ip */
	lua_setfield(L1, -3, "address");			/* func client ip */

	lua_pushlstring(L1, ctx->host.data, ctx->host.length);	/* func client ip host */
	lua_pushvalue(L1, -1);					/* func client ip host host */
	lua_setfield(L1, -4, "host");				/* func client ip host */
	lua_remove(L1, -3);					/* func ip host */

	return 2;
}

LUA_CMD_DEF(helo)
{
	lua_pushlstring(L1, ctx->helo.data, ctx->helo.length);		/* func arg */
	return 1;
}

LUA_CMD_DEF(ehlo)
{
	lua_pushlstring(L1, ctx->helo.data, ctx->helo.length);		/* func arg */
	return 1;
}

LUA_CMD_DEF(auth)
{
	lua_pushlstring(L1, ctx->auth.data, ctx->auth.length);		/* func arg */
	return 1;
}

LUA_CMD_DEF(mail)
{
	lua_getglobal(L1, "client");		/* func client */
	lua_table_set_string(L1, -1, "id_trans", ctx->id_trans);
	lua_pushlstring(L1, ctx->path.data, ctx->path.length);
	lua_setfield(L1, -2, "msg_file");
	lua_pop(L1, 1);

	lua_pushlstring(L1, ctx->sender->address.string, ctx->sender->address.length);	/* func arg */
	return 1;
}

LUA_CMD_DEF(rcpt)
{
	lua_pushlstring(L1, (*ctx->rcpt)->address.string, (*ctx->rcpt)->address.length);	/* func arg */
	return 1;
}

LUA_CMD_DEF(reset)
{
	lua_getglobal(L1, "client");		/* func client */
	lua_table_clear(L1, -1, "id_trans");
	lua_table_clear(L1, -1, "msg_file");
	lua_pop(L1, 1);

	return 0;
}

LUA_CMD_DEF(unknown)
{
	lua_pushlstring(L1, ctx->input.data, ctx->input.length);
	return 1;
}

LUA_CMD_DEF(content)
{
	lua_pushlstring(L1, ctx->input.data, ctx->input.length);
	return 1;
}

#define STARTS_WITH(s)	(STRLEN(s) <= ctx->input.length && strncasecmp(ctx->input.data+ctx->input.offset, s, STRLEN(s)) == 0)

LUA_CMD_DEF(header)
{
	int span, is_crlf;

	/* Find end of header line. */
	for (span = 0; ; span++) {
		span = strcspn(ctx->input.data+ctx->input.offset+span, LF);
		if (ctx->input.length <= ctx->input.offset+span)
			break;
		if (!isblank(ctx->input.data[ctx->input.offset+span+1]))
			break;
	}

	/* Backup one byte if end of line is CRLF. */
	span -= (is_crlf = (0 < span && ctx->input.data[ctx->input.offset+span-1] == '\r'));

	/* Push the line. */
	lua_pushlstring(L1, ctx->input.data+ctx->input.offset, span);

	/* Skip over the newline. */
	ctx->input.offset += span + is_crlf + 1;

	/* Next line is EOH? */
	if (STARTS_WITH(CRLF)) {
		ctx->input.offset += STRLEN(CRLF);
		ctx->eoh = ctx->input.offset;
	} else if (STARTS_WITH(LF)) {
		ctx->input.offset += STRLEN(LF);
		ctx->eoh = ctx->input.offset;
	}

	return 1;
}

LUA_CMD_DEF(body)
{
	int span, is_crlf;

	/* Find end of line. */
	span = strcspn(ctx->input.data+ctx->input.offset, LF);

	/* Backup one if end of line is CRLF. */
	span -= (is_crlf = (0 < span && ctx->input.data[ctx->input.offset+span-1] == '\r'));

	/* Push the line. */
	lua_pushlstring(L1, ctx->input.data+ctx->input.offset, span);

	/* Skip over the newline. */
	ctx->input.offset += span + is_crlf + 1;

	return 1;
}

LUA_CMD_DEF(dot)
{
	lua_pushlstring(L1, ctx->path.data, ctx->path.length);
	return 1;
}

LUA_CMD_DEF(forward)
{
	lua_pushlstring(L1, ctx->input.data, ctx->input.length);
	return 1;
}

LUA_CMD_DEF(error)
{
	lua_pushinteger(L1, errno);
	lua_pushstring(L1, strerror(errno));
	return 2;
}

static lua_State *
lua_hook_init(void)
{
	lua_State *L;

	if ((L = luaL_newstate()) == NULL)
		goto error0;

//	lua_thread_setindex(L);

	lua_newtable(L);
	lua_pushliteral(L, _VERSION);
	lua_setfield(L, -2, "bin_version");
	lua_pushliteral(L, API_VERSION);
	lua_setfield(L, -2, "api_version");
	lua_pushliteral(L, _COPYRIGHT);
	lua_setfield(L, -2, "copyright");
	lua_pushstring(L, my_host_name);
	lua_setfield(L, -2, "host");
	lua_setglobal(L, _NAME);

	lua_newtable(L);
//	lua_table_inherit(L, -1);
	lua_setglobal(L, "hook");

	lua_define_dns(L);
	lua_define_net(L);
	lua_define_syslog(L);

	lua_getglobal(L, "syslog");	/* syslog */
	lua_getfield(L, -1, "error");	/* syslog errfn */
	lua_remove(L, -2);		/* errfn */

	switch (luaL_loadfile(L, opt_script.string)) {	/* errfn file */
	case LUA_ERRFILE:
	case LUA_ERRMEM:
	case LUA_ERRSYNTAX:
		syslog(LOG_ERR, "%s", lua_tostring(L, -1));
		goto error1;
	}

	/* Stop collector during initialization. */
	lua_gc(L, LUA_GCSTOP, 0);
	luaL_openlibs(L);
	lua_gc(L, LUA_GCRESTART, 0);

	if (lua_pcall(L, 0, LUA_MULTRET, -2)) {	/* errfn file */
		syslog(LOG_ERR, "%s init: %s", opt_script.string, lua_tostring(L, -1)); /* errfn errmsg */
		goto error1;
	}
	lua_pop(L, lua_gettop(L));	/* -- */

	return L;
error1:
	lua_close(L);
error0:
	return NULL;
}

/***********************************************************************
 ***
 ***********************************************************************/

#define LINE_WRAP 70

void
printVar(int columns, const char *name, const char *value)
{
	int length;
	Vector list;
	const char **args;

	if (columns <= 0)
		printf("%s=\"%s\"\n",  name, value);
	else if ((list = TextSplit(value, " \t", 0)) != NULL && 0 < VectorLength(list)) {
		args = (const char **) VectorBase(list);

		length = printf("%s=\"'%s'", name, *args);
		for (args++; *args != NULL; args++) {
			/* Line wrap. */
			if (columns <= length + strlen(*args) + 4) {
				(void) printf("\n\t");
				length = 8;
			}
			length += printf(" '%s'", *args);
		}
		if (columns <= length + 1) {
			(void) printf("\n");
		}
		(void) printf("\"\n");

		VectorDestroy(list);
	}
}

void
printVersion(void)
{
	printf(_NAME " " _VERSION " " _COPYRIGHT "\n");
	printf("LibSnert %s %s", LIBSNERT_VERSION, LIBSNERT_COPYRIGHT "\n");
#ifdef _BUILT
	printf("Built on " _BUILT "\n");
#endif
}

void
printInfo(void)
{
#ifdef _NAME
	printVar(0, "_NAME", _NAME);
#endif
#ifdef _VERSION
	printVar(0, "_VERSION", _VERSION);
#endif
#ifdef _COPYRIGHT
	printVar(0, "_COPYRIGHT", _COPYRIGHT);
#endif
#ifdef _CONFIGURE
	printVar(LINE_WRAP, "_CONFIGURE", _CONFIGURE);
#endif
#ifdef _BUILT
	printVar(0, "_BUILT", _BUILT);
#endif
#ifdef LIBSNERT_VERSION
	printVar(0, "LIBSNERT_VERSION", LIBSNERT_VERSION);
#endif
#ifdef LIBSNERT_BUILD_HOST
	printVar(LINE_WRAP, "LIBSNERT_BUILD_HOST", LIBSNERT_BUILD_HOST);
#endif
#ifdef LIBSNERT_CONFIGURE
	printVar(LINE_WRAP, "LIBSNERT_CONFIGURE", LIBSNERT_CONFIGURE);
#endif
#ifdef SQLITE_VERSION
	printVar(0, "SQLITE3_VERSION", SQLITE_VERSION);
#endif
#ifdef _CFLAGS
	printVar(LINE_WRAP, "CFLAGS", _CFLAGS);
#endif
#ifdef _LDFLAGS
	printVar(LINE_WRAP, "LDFLAGS", _LDFLAGS);
#endif
#ifdef _LIBS
	printVar(LINE_WRAP, "LIBS", _LIBS);
#endif
}

/*
 * Set the next session-id.
 *
 * The session-id is composed of
 *
 *	ymd HMS ppppp nnnnn 00
 */
static void
next_session(char buffer[ID_SIZE])
{
	time_t now;
	static unsigned short count = 0;

	if (++count == 0)
		count = 1;

	(void) time(&now);
	time62Encode(now, buffer);
	(void) snprintf(
		buffer+TIME62_BUFFER_SIZE,
		ID_SIZE-TIME62_BUFFER_SIZE,
		"%05u%05u00", getpid(), count
	);
}

/*
 * Set the next message-id.
 *
 * The message-id is composed of
 *
 *	ymd HMS ppppp nnnnn cc
 *ymdHMSpppppnnnnncc
 * cc is Base63 number excluding 00.
 */
static void
next_transaction(SmtpCtx *ctx)
{
	long length;

	if (62 * 62 <= ctx->transaction_count++)
		ctx->transaction_count = 1;

	length = TextCopy(ctx->id_trans, sizeof (ctx->id_trans), ctx->id_sess);
	ctx->id_trans[length-2] = base62[ctx->transaction_count / 62];
	ctx->id_trans[length-1] = base62[ctx->transaction_count % 62];
}

static void
trim_buffer(Buffer *buf)
{
	while (0 < buf->length && isspace(buf->data[buf->length-1]))
		buf->data[--buf->length] = '\0';
}

static void
client_reset(SmtpCtx *ctx)
{
	TRACE_CTX(ctx, 000);

	LUA_CALL(reset);

	if (*opt_spool_dir.string != '\0') {
		if (ctx->spool_fp != NULL) {
			(void) fclose(ctx->spool_fp);
			ctx->spool_fp = NULL;
		}
		(void) unlink(ctx->path.data);
	}

	*ctx->id_trans = '\0';
	*ctx->path.data = '\0';
	ctx->path.length = 0;
	ctx->state = ctx->state_helo;
	VectorRemoveAll(ctx->recipients);
	free(ctx->sender);
	ctx->sender = NULL;
}

extern SMTP_CMD_DEF(ehlo);
extern SMTP_CMD_DEF(data);
extern SMTP_CMD_DEF(content);

void
client_write(SmtpCtx *ctx, Buffer *buffer)
{
	TRACE_CTX(ctx, 000);

	/* Always detect pipelining. */
	if (socketHasInput(ctx->client, SMTP_PIPELINING_TIMEOUT)) {
		if (verb_info.value)
			syslog(LOG_INFO, LOG_FMT "pipeline detected", LOG_ID(ctx));
		ctx->is_pipelining = 1;
	}

	/* Ignore any subsequent input that follows the QUIT command
	 * as something in the TCP shutdown sequence appears to trigger
	 * this test.
	 *
	 * Also due to a stupid bug in some brain dead versions of
	 * Microsoft Exchange, we have to ignore pipeline input that
	 * might immediately follow the DATA command.
	 *
	 * A related issue to this would be bad SMTP implementations
	 * that simply assume a 354 response will always be sent and
	 * proceed to pipeline the content. Their assumption is broken
	 * since it's perfectly reasonable to perform some additional
	 * tests at DATA and return 4xy or 5xy response instead of 354.
	 */
	if (opt_rfc2920_pipelining_reject.value && ctx->is_pipelining
	&& ctx->state != SMTP_CMD_HOOK(data) && ctx->state != SMTP_CMD_HOOK(content)
	&& (!opt_rfc2920_pipelining.value || ctx->state_helo != SMTP_CMD_HOOK(ehlo))) {
		ctx->reply.length = snprintf(ctx->reply.data, ctx->reply.size, fmt_pipeline, opt_smtp_error_url.string);
		buffer = &ctx->reply;
	}

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "< %ld:%.60s", LOG_ID(ctx), buffer->length, buffer->data);

	if (socketWrite(ctx->client, buffer->data, buffer->length) != buffer->length) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		SIGLONGJMP(ctx->on_error, 1);
	}
}

void
client_send(SmtpCtx *ctx, const char *fmt, ...)
{
	va_list args;

	TRACE_CTX(ctx, 000);

	if (LUA_HOOK_DEFAULT(ctx->smtp_rc) || ctx->reply.length == 0) {
		va_start(args, fmt);
		ctx->reply.length = vsnprintf(ctx->reply.data, ctx->reply.size, fmt, args);
		va_end(args);
	}

	if (ctx->reply.size <= ctx->reply.length) {
		(void) snprintf(ctx->reply.data, ctx->reply.size, fmt_buffer, opt_smtp_error_url.string);
		syslog(LOG_ERR, log_buffer, LOG_INT(ctx));
	}

	client_write(ctx, &ctx->reply);
	ctx->reply.length = 0;

	if (ctx->reply.size <= ctx->reply.length)
		SIGLONGJMP(ctx->on_error, 1);
}

SMTP_CMD_DEF(quit)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->input.size != ctx->pipe.length - ctx->pipe.offset) {
		ctx->input.data[STRLEN("QUIT")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_REJECT, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	LUA_PT_CALL0(quit);

	client_send(ctx, fmt_quit, my_host_name, ctx->id_sess);

	errno = 0;
	SIGLONGJMP(ctx->on_error, 1);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(accept)
{
	PDQ_rr *rr;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	/* Copy the IP address into the host name in case there is no PTR. */
	ctx->host.data[0] = '[';
	ctx->host.length = TextCopy(ctx->host.data+1, ctx->host.size-1, ctx->addr.data)+1;
	ctx->host.data[ctx->host.length++] = ']';
	ctx->host.data[ctx->host.length] = '\0';

	if (lua_hook_newthread(lua_live, ctx) != 0)
		goto error1;

	if (dns_open(loop, event))
		goto error2;

	if (pdqQuery(ctx->pdq, PDQ_CLASS_IN, PDQ_TYPE_PTR, ctx->addr.data, NULL)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error3;
	}

	dns_reset(ctx);
	PT_YIELD_UNTIL(&ctx->pt, dns_wait(ctx, 1) != EAGAIN);

	for (rr = ctx->pdq_answer; rr != NULL; rr = rr->next) {
		if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_PTR) {
			break;
		}
	}

	if (rr == NULL)
		goto error3;

	/* Copy the client's host name into our buffer. This name will
	 * almost certainly have a trailing dot for the root domain, as
	 * will any additional records returned from the DNS lookup.
	 */
	ctx->host.length = TextCopy(ctx->host.data, ctx->host.size, ((PDQ_PTR *) rr)->host.string.value);

	/* Consider dig -x 63.84.135.34, which has a multihomed PTR
	 * record for many different unrelated domains. This affects
	 * our choice to use the grey-listing PTR key or not. If the
	 * multihomed PTR were all for the same domain suffix, then
	 * the grey-listing PTR key will work, otherwise we have to
	 * fall back on just using the IP address.
	 */

	/* Wait to remove the trailing dot for the root domain from
	 * the client's host name until after any multihomed PTR list
	 * is reviewed above.
	 */
	if (0 < ctx->host.length && ctx->host.data[ctx->host.length-1] == '.')
		ctx->host.data[ctx->host.length-1] = '\0';
	TextLower(ctx->host.data, ctx->host.length);
error3:
	dns_close(loop, event);
error2:
	ctx->reply.length = 0;
	LUA_PT_CALL(accept);
error1:
	client_send(ctx, fmt_welcome, my_host_name, ctx->id_sess);
	ctx->pipe.length = 0;

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(unknown)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	LUA_PT_CALL(unknown);
	client_send(ctx, fmt_unknown, opt_smtp_error_url.string, ctx->input.data);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(out_seq)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	client_send(ctx, fmt_out_seq, opt_smtp_error_url.string, ctx->input.data);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(helo)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, ctx->input.data+5);

	LUA_PT_CALL(helo);

	client_send(ctx, fmt_helo, ctx->helo.data, ctx->addr.data, ctx->host.data);

	if (LUA_HOOK_OK(ctx->smtp_rc)) {
		ctx->state = ctx->state_helo = SMTP_CMD_HOOK(helo);
		client_reset(ctx);
	}

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(ehlo)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->input.size != ctx->pipe.length - ctx->pipe.offset
	|| socketHasInput(ctx->client, SMTP_PIPELINING_TIMEOUT)) {
		ctx->input.data[STRLEN("EHLO")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_REJECT, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, ctx->input.data+5);

	LUA_PT_CALL(ehlo);

	client_send(
		ctx, fmt_ehlo, ctx->helo.data, ctx->addr.data, ctx->host.data,
		opt_smtp_xclient.value ? "250-XCLIENT ADDR HELO NAME PROTO\r\n" : "",
		opt_rfc2920_pipelining.value ? "250-PIPELINING\r\n" : "",
		opt_smtp_max_size.value
	);

	if (LUA_HOOK_OK(ctx->smtp_rc)) {
		ctx->state = ctx->state_helo = SMTP_CMD_HOOK(ehlo);
		client_reset(ctx);
	}

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(auth)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_CMD_HOOK(ehlo)) {
		PT_INIT(&ctx->pt);
		return SMTP_CMD_DO(unknown);
	}
	if (0 < ctx->auth.length) {
		client_send(ctx, fmt_auth_already, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	if (TextInsensitiveStartsWith(ctx->input.data+STRLEN("AUTH "), "PLAIN") < 0) {
		client_send(ctx, fmt_auth_mech, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	ctx->auth.length = TextCopy(ctx->auth.data, ctx->auth.size, ctx->input.data);

	LUA_PT_CALL(auth);
	client_send(ctx, fmt_auth_ok);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(mail)
{
	int span;
	const char *error;
	char *sender, *param;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_CMD_HOOK(helo) && ctx->state != SMTP_CMD_HOOK(ehlo)) {
		PT_INIT(&ctx->pt);
		return SMTP_CMD_DO(out_seq);
	}

	/* Find the end of the "MAIL FROM:" string. */
	if ((sender = strchr(ctx->input.data+5, ':')) == NULL) {
		client_send(ctx, fmt_syntax, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}
	sender++;

	span  = strspn(sender, " \t");
	span += strcspn(sender+span, " \t");

	/* Split the MAIL FROM: address from any trailing options, in
	 * particular: "MAIL FROM:<user@example.com> AUTH=<>" which
	 * causes parsePath() to find the right most set of angle
	 * brackets.
	 */
	sender[span] = '\0';

	if ((error = parsePath(sender, parse_path_flags, 1, &ctx->sender)) != NULL) {
		client_send(
			ctx, fmt_mail_parse,
			SMTP_ISS_TEMP(error) ? SMTP_CLOSING : SMTP_BAD_ADDRESS,
			error, opt_smtp_error_url.string
		);
		if (SMTP_ISS_TEMP(error)) {
			syslog(LOG_ERR, log_internal, LOG_INT(ctx));
			SIGLONGJMP(ctx->on_error, 1);
		}
		PT_EXIT(&ctx->pt);
	}

	if ((param = strstr(sender+span+1, "SIZE=")) != NULL) {
		ctx->mail_size = strtol(param+STRLEN("SIZE="), NULL, 10);
		if (0 < opt_smtp_max_size.value && opt_smtp_max_size.value <= ctx->mail_size) {
			client_send(ctx, fmt_mail_size, opt_smtp_error_url.string, opt_smtp_max_size.value);
			PT_EXIT(&ctx->pt);
		}
	}

	next_transaction(ctx);
	LUA_PT_CALL(mail);
	client_send(ctx, fmt_mail_ok, ctx->sender->address.string);

	if (LUA_HOOK_OK(ctx->smtp_rc))
		ctx->state = SMTP_CMD_HOOK(mail);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(rcpt)
{
	int span;
	ParsePath *rcpt;
	char *recipient;
	const char *error;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_CMD_HOOK(mail)) {
		PT_INIT(&ctx->pt);
		return SMTP_CMD_DO(out_seq);
	}

	/* Find the end of the "RCPT TO:" string. */
	if ((recipient = strchr(ctx->input.data+5, ':')) == NULL) {
		client_send(ctx, fmt_syntax, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}
	recipient++;

	span  = strspn(recipient, " \t");
	span += strcspn(recipient+span, " \t");

	/* Split the RCPT TO: address from any trailing options. */
	recipient[span] = '\0';

	/* Parse the address into its component parts. */
	if ((error = parsePath(recipient, parse_path_flags, 0, &rcpt)) != NULL) {
		client_send(
			ctx, fmt_rcpt_parse,
			SMTP_ISS_TEMP(error) ? SMTP_CLOSING : SMTP_BAD_ADDRESS,
			error, opt_smtp_error_url.string
		);
		if (SMTP_ISS_TEMP(error)) {
			syslog(LOG_ERR, log_internal, LOG_INT(ctx));
			SIGLONGJMP(ctx->on_error, 1);
		}
		PT_EXIT(&ctx->pt);
	}
	if (rcpt->address.length == 0) {
		client_send(ctx, fmt_rcpt_null, opt_smtp_error_url.string);
		free(rcpt);
		PT_EXIT(&ctx->pt);
	}

	ctx->rcpt = &rcpt;

	LUA_PT_CALL(rcpt);

	if ((ctx->smtp_rc == 0 || SMTP_IS_OK(ctx->smtp_rc)) && VectorAdd(ctx->recipients, rcpt)) {
		client_send(ctx, fmt_internal, opt_smtp_error_url.string, strerror(errno));
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		free(rcpt);
		SIGLONGJMP(ctx->on_error, 1);
	}

	client_send(ctx, fmt_rcpt_ok, rcpt->address.string);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(data)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_CMD_HOOK(mail)) {
		PT_INIT(&ctx->pt);
		return SMTP_CMD_DO(out_seq);
	}

	if (VectorLength(ctx->recipients) == 0) {
		client_send(ctx, fmt_no_rcpts, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	if (ctx->input.size != ctx->pipe.length - ctx->pipe.offset) {
		ctx->input.data[STRLEN("DATA")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_TRANSACTION_FAILED, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	if (*opt_spool_dir.string != '\0') {
		ctx->path.length = snprintf(ctx->path.data, ctx->path.size, "%s/%s", opt_spool_dir.string, ctx->id_trans);
		if ((ctx->spool_fp = fopen(ctx->path.data, "wb")) == NULL) {
			client_send(ctx, fmt_internal, opt_smtp_error_url.string, strerror(errno));
			syslog(LOG_ERR, log_internal, LOG_INT(ctx));
			SIGLONGJMP(ctx->on_error, 1);
		}
	}

	LUA_PT_CALL0(data);

	if (LUA_HOOK_OK(ctx->smtp_rc))
		ctx->state = SMTP_CMD_HOOK(data);

	client_send(ctx, fmt_data);

	ctx->pipe.length = 0;
	ctx->eoh = 0;

	PT_END(&ctx->pt);
}

extern void client_io_cb(SocketEvents *loop, SocketEvent *event);

SMTP_CMD_DEF(content)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	ctx->state = SMTP_CMD_HOOK(content);

	if (ctx->spool_fp != NULL)
		(void) fwrite(ctx->input.data, ctx->input.length, 1, ctx->spool_fp);

	LUA_PT_CALL(content);

	if (ctx->eoh == 0) {
		/* Process headers line by line. */
		do {
			LUA_PT_CALL(header);
		} while (ctx->eoh == 0);

		/* End of headers */
		LUA_PT_CALL0(eoh);
	}

	/* Process body line by line. */
	do {
		LUA_PT_CALL(body);
	} while (ctx->input.offset < ctx->input.length);

	if (ctx->is_dot) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "> %d:%s", LOG_ID(ctx), ctx->is_dot, ctx->input.data+ctx->input.length);

		if (ctx->spool_fp != NULL) {
			(void) fclose(ctx->spool_fp);
			ctx->spool_fp = NULL;
		}

		LUA_PT_CALL(dot);

		if (LUA_HOOK_OK(ctx->smtp_rc)) {
			LUA_PT_CALL(forward);

			if (LUA_HOOK_DEFAULT(ctx->smtp_rc)
			&& *opt_smtp_smart_host.string != '\0' && *opt_spool_dir.string != '\0') {
				ctx->mx_read.smtp_rc = 0;
				if (mx_open(ctx, opt_smtp_smart_host.string)) {
					ctx->reply.length = 0;
					goto forward_tempfail1;
				}

				PT_SPAWN(&ctx->pt, &ctx->mx_read.pt, mx_read(ctx));
				if (ctx->mx_read.smtp_rc != SMTP_WELCOME) {
					syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
					ctx->reply.length = 0;
					goto forward_tempfail2;
				}
				free(ctx->mx_read.lines);

				if (0 < ctx->auth.length) {
					mx_print(ctx, ctx->auth.data, ctx->auth.length);
					PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
					if (ctx->mx_read.smtp_rc != SMTP_AUTH_OK) {
						syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
						ctx->reply.length = 0;
						goto forward_tempfail2;
					}
					free(ctx->mx_read.lines);
				}

				mx_printf(ctx, "EHLO %s\r\n", my_host_name);
				PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
				free(ctx->mx_read.lines);
				if (ctx->mx_read.smtp_rc != SMTP_OK) {
					mx_printf(ctx, "HELO %s\r\n", my_host_name);
					PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
					if (ctx->mx_read.smtp_rc != SMTP_OK) {
						syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
						ctx->reply.length = 0;
						goto forward_tempfail2;
					}
					free(ctx->mx_read.lines);
				}

				mx_printf(ctx, "MAIL FROM:<%s>\r\n", ctx->sender->address.string);
				PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
				if (ctx->mx_read.smtp_rc != SMTP_OK) {
					syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
					ctx->reply.length = 0;
					goto forward_tempfail2;
				}
				free(ctx->mx_read.lines);

				for (ctx->rcpt = (ParsePath **) VectorBase(ctx->recipients); *ctx->rcpt != NULL; ctx->rcpt++) {
					mx_printf(ctx, "RCPT TO:<%s>\r\n", (*ctx->rcpt)->address.string);
					PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
					if (ctx->mx_read.smtp_rc != SMTP_OK) {
						syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
						ctx->reply.length = 0;
						goto forward_tempfail2;
					}
					free(ctx->mx_read.lines);
				}

				mx_print(ctx, "DATA\r\n", STRLEN("DATA\r\n"));
				PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
				if (ctx->mx_read.smtp_rc != SMTP_WAITING) {
					syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
					ctx->reply.length = 0;
					goto forward_tempfail2;
				}
				free(ctx->mx_read.lines);

				if ((ctx->spool_fp = fopen(ctx->path.data, "rb")) == NULL) {
					syslog(LOG_ERR, LOG_FMT "%s %s: %s (%d)", LOG_ID(ctx), ctx->id_trans, ctx->path.data, strerror(errno), errno);
					ctx->mx_read.smtp_rc = SMTP_ERROR_IO;
					ctx->reply.length = 0;
					goto forward_tempfail2;
				}

				while ((ctx->work.length = fread(ctx->work.data, 1, ctx->work.size, ctx->spool_fp)) != 0) {
					mx_print(ctx, ctx->work.data, ctx->work.length);
				}

				if (ferror(ctx->spool_fp)) {
					syslog(LOG_ERR, LOG_FMT "%s %s: %s (%d)", LOG_ID(ctx), ctx->id_trans, ctx->path.data, strerror(errno), errno);
					ctx->reply.length = 0;
					goto forward_tempfail3;
				}

				mx_print(ctx, ".\r\n", STRLEN(".\r\n"));
				PT_WAIT_THREAD(&ctx->pt, mx_read(ctx));
				if (ctx->mx_read.smtp_rc != SMTP_OK) {
					syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), opt_smtp_smart_host.string, ctx->mx_read.lines[0]);
					ctx->reply.length = 0;
				}
forward_tempfail3:
				(void) fclose(ctx->spool_fp);
				if (SMTP_IS_TEMP(ctx->mx_read.smtp_rc))
					goto forward_tempfail1;
				if (SMTP_IS_PERM(ctx->mx_read.smtp_rc))
					goto forward_reject;
forward_tempfail2:
				mx_print(ctx, "QUIT\r\n", STRLEN("QUIT\r\n"));
				free(ctx->mx_read.lines);
				mx_close(ctx);

				if (ctx->mx_read.smtp_rc != SMTP_OK) {
					ctx->reply.length = 0;
					goto forward_tempfail1;
				}

				/* We've successfully forwarded the message.
				 * Any cached reply has to be positive else
				 * reset the reply and use the default.
				 */
				if (0 < ctx->reply.length && !SMTP_ISS_OK(ctx->reply.data))
					ctx->reply.length = 0;
				ctx->smtp_rc = ctx->mx_read.smtp_rc;
				goto forward_accept;
			}
		}

		switch (opt_smtp_default_at_dot.value) {
		case SMTP_OK:
forward_accept:
			client_send(ctx, fmt_msg_ok, ctx->id_trans);
			break;
		case SMTP_TRY_AGAIN_LATER:
forward_tempfail1:
			client_send(ctx, fmt_try_again, opt_smtp_error_url.string);
			break;
		case SMTP_REJECT:
forward_reject:
			client_send(ctx, fmt_msg_reject, opt_smtp_error_url.string, ctx->id_trans);
			break;
		}

		client_reset(ctx);

		/* When there is input remaining, it is possibly a
		 * pipelined QUIT or the start of another transaction
		 * and we need to process the commands in the pipe
		 * before we return and resume the IO sevent loop.
		 */
		if (0 < ctx->pipe.offset && ctx->pipe.offset < ctx->pipe.length)
			client_io_cb(loop, event);
	}

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(rset)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	LUA_PT_CALL0(rset);
	client_send(ctx, fmt_ok);
	client_reset(ctx);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(noop)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->input.size != ctx->pipe.length - ctx->pipe.offset
	|| socketHasInput(ctx->client, SMTP_PIPELINING_TIMEOUT)) {
		ctx->input.data[STRLEN("NOOP")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_REJECT, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	LUA_PT_CALL0(noop);
	client_send(ctx, fmt_ok);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(help)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	LUA_PT_CALL0(help);
	client_send(ctx, fmt_help);
	PT_END(&ctx->pt);
}

static void
verboseFill(const char *prefix, Buffer *buf)
{
	Option **opt, *o;
	long cols, length;

	if (0 < buf->length)
		buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, "\r\n");
	buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, prefix);

	cols = 0;
	for (opt = verb_table; *opt != NULL; opt++) {
		o = *opt;

		if (LINE_WRAP <= cols % LINE_WRAP + strlen(o->name) + 2) {
			buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, "\r\n");
			buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, prefix);
			cols = 0;
		}

		length = snprintf(
			buf->data+buf->length,
			buf->size-buf->length,
			" %c%s", o->value ? '+' : '-', o->name
		);

		buf->length += length;
		cols += length;
	}

	buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, "\r\n");
}

static void
verboseInit(void)
{
	static char buffer[2048];
	static Buffer usage = { sizeof (buffer), 0, 0, buffer };

	opt_verbose.usage = buffer;
	usage.length = TextCopy(usage.data, usage.size, usage_verbose);
	verboseFill("#", &usage);
	usage.length += TextCopy(usage.data+usage.length, usage.size-usage.length, "#");

//	optionInitOption(&opt_verbose);
//	optionString(opt_verbose.string, verb_table, NULL);
}

SMTP_CMD_DEF(verb)
{
	char *nl;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	ctx->reply.length = 0;
	optionString(ctx->input.data+5, verb_table, NULL);
	verboseFill("214-2.0.0", &ctx->reply);
	nl = strrchr(ctx->reply.data, '\n');
	nl[4] = ' ';

	if (socketWrite(ctx->client, ctx->reply.data, ctx->reply.length) != ctx->reply.length) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		SIGLONGJMP(ctx->on_error, 1);
	}
	ctx->reply.length = 0;

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(xclient)
{
	Vector args;
	const char **list, *value;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (!opt_smtp_xclient.value || (ctx->state_helo != NULL && ctx->state != ctx->state_helo)) {
		PT_INIT(&ctx->pt);
		return SMTP_CMD_DO(out_seq);
	}

	LUA_PT_CALL0(xclient);

	if (!LUA_HOOK_OK(ctx->smtp_rc)) {
		PT_INIT(&ctx->pt);
		return SMTP_CMD_DO(out_seq);
	}

	args = TextSplit(ctx->input.data+STRLEN("XCLIENT "), " ", 0);
	for (list = (const char **) VectorBase(args);  *list != NULL; list++) {
		if (0 <= TextInsensitiveStartsWith(*list, "NAME=")) {
			value = *list + STRLEN("NAME=");
			if (TextInsensitiveCompare(value, "[UNAVAILABLE]") == 0) {
				;
			} else if (TextInsensitiveCompare(value, "[TEMPUNAVAIL]") == 0) {
				;
			} else {
				ctx->host.length = TextCopy(ctx->host.data, ctx->host.size, value);
				if (0 < ctx->host.length && ctx->host.data[ctx->host.length-1] == '.')
					ctx->host.data[--ctx->host.length] = '\0';
				TextLower(ctx->host.data, ctx->host.length);
			}
			continue;
		} else if (0 <= TextInsensitiveStartsWith(*list, "ADDR=")) {
			value = *list + STRLEN("ADDR=");
			if (0 < parseIPv6(value, ctx->ipv6)) {
				ctx->addr.length = TextCopy(ctx->addr.data, ctx->addr.size, value);
				continue;
			}
		} else if (0 <= TextInsensitiveStartsWith(*list, "HELO=")) {
			value = *list + STRLEN("HELO=");
			ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, value);
			continue;
		} else if (0 <= TextInsensitiveStartsWith(*list, "PROTO=")) {
			value = *list + STRLEN("PROTO=");
			if (TextInsensitiveCompare(value, "SMTP") == 0) {
				ctx->state = ctx->state_helo = SMTP_CMD_HOOK(helo);
			} else if (TextInsensitiveCompare(value, "ESMTP") == 0) {
				ctx->state = ctx->state_helo = SMTP_CMD_HOOK(ehlo);
			}
			continue;
		}

		client_send(ctx, fmt_bad_args, opt_smtp_error_url.string, *list);
		PT_EXIT(&ctx->pt);
	}

	PT_INIT(&ctx->pt);
	return SMTP_CMD_DO(accept);

	PT_END(&ctx->pt);
}

static Command smtp_cmd_table[] = {
	{ "HELO", SMTP_CMD_HOOK(helo) },
	{ "EHLO", SMTP_CMD_HOOK(ehlo) },
	{ "AUTH", SMTP_CMD_HOOK(auth) },
	{ "MAIL", SMTP_CMD_HOOK(mail) },
	{ "RCPT", SMTP_CMD_HOOK(rcpt) },
	{ "DATA", SMTP_CMD_HOOK(data) },
	{ "RSET", SMTP_CMD_HOOK(rset) },
	{ "NOOP", SMTP_CMD_HOOK(noop) },
	{ "QUIT", SMTP_CMD_HOOK(quit) },
	{ "HELP", SMTP_CMD_HOOK(help) },
	{ "VERB", SMTP_CMD_HOOK(verb) },
	{ "XCLIENT", SMTP_CMD_HOOK(xclient) },
	{ NULL, NULL }
};

#define ENDS_WITH(s)	(STRLEN(s) <= ctx->pipe.length && strcasecmp(ctx->pipe.data+ctx->pipe.length-STRLEN(s), s) == 0)
#define DOT_QUIT_CRLF	"\r\n.\r\nQUIT\r\n"
#define DOT_QUIT_LF	"\n.\nQUIT\n"
#define DOT_CRLF	"\r\n.\r\n"
#define DOT_LF		"\n.\n"

void
client_io_cb(SocketEvents *loop, SocketEvent *event)
{
	long nbytes;
	Command *entry;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	SETJMP_PUSH(&ctx->on_error);
	if (SIGSETJMP(ctx->on_error, 1) != 0) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "close %s cc=%ld", LOG_ID(ctx), ctx->addr.data, VectorLength(loop->events)-1);
		if (event->on.error != NULL)
			(*event->on.error)(loop, event);
		goto end;
	}

	ctx->loop = loop;
	ctx->event = event;
	ctx->smtp_rc = 0;

	if (ctx->state == SMTP_CMD_HOOK(data)) {
		ctx->pipe.length += socketRead(
			event->socket,
			ctx->pipe.data+ctx->pipe.length, ctx->pipe.size-1-ctx->pipe.length
		);
		ctx->pipe.data[ctx->pipe.length] = '\0';

		ctx->is_dot = 0;
		if (ENDS_WITH(DOT_QUIT_CRLF))
			ctx->is_dot = STRLEN(DOT_QUIT_CRLF)-STRLEN(CRLF);
		else if (ENDS_WITH(DOT_QUIT_LF))
			ctx->is_dot = STRLEN(DOT_QUIT_LF)-STRLEN(LF);
		else if (ENDS_WITH(DOT_CRLF))
			ctx->is_dot = STRLEN(DOT_CRLF)-STRLEN(CRLF);
		else if (ENDS_WITH(DOT_LF))
			ctx->is_dot = STRLEN(DOT_LF)-STRLEN(LF);

		if (ctx->is_dot || ctx->pipe.size <= ctx->pipe.length+SMTP_TEXT_LINE_LENGTH) {
			ctx->input.offset = 0;
			ctx->input.data = ctx->pipe.data;
			ctx->input.length = ctx->pipe.length - ctx->is_dot;
			ctx->input.size = ctx->input.length + 1;
			ctx->input.size += strspn(ctx->input.data+ctx->input.size, CRLF);
			ctx->pipe.offset = ctx->input.size;

			PT_INIT(&ctx->pt);
			(void) SMTP_CMD_DO(content);
		}

		return;
	} else if (ctx->pipe.offset == 0) {
		/* Read the SMTP command line. */
		nbytes = socketRead(
			event->socket,
			ctx->pipe.data+ctx->pipe.length, ctx->pipe.size-1-ctx->pipe.length
		);

		/* EOF? */
		if (nbytes <= 0)
			SIGLONGJMP(ctx->on_error, 1);

		ctx->pipe.length += nbytes;

		/* Wait for a complete line unit. */
		if (ctx->pipe.data[ctx->pipe.length-1] != '\n')
			goto end;

		ctx->pipe.offset = 0;
	}

	/* Process all pipelined commands. */
	for ( ; ctx->pipe.offset < ctx->pipe.length; ctx->pipe.offset += ctx->input.size) {
		/* Start of command. */
		ctx->input.data = ctx->pipe.data + ctx->pipe.offset;

		/* Find newline. */
		ctx->input.length = strcspn(ctx->input.data, CRLF);

		/* Find end of line. */
		ctx->input.size = ctx->input.length + strspn(ctx->input.data+ctx->input.length, CRLF);

		/* Remove newline. */
		ctx->input.data[ctx->input.length] = '\0';

		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "> %ld:%.60s", LOG_ID(ctx), ctx->input.length, ctx->input.data);
{
		lua_State *L1 = lua_hook_getthread(lua_live, ctx);
		if (L1 != NULL) {
			lua_getglobal(L1, "client");
			lua_pushlstring(L1, ctx->input.data, ctx->input.length);
			lua_setfield(L1, -2, "input");

			lua_pushinteger(L1, ctx->is_pipelining);		/* func client flag */
			lua_setfield(L1, -2, "is_pipelining");			/* func client */

			lua_pop(L1, 1);
		}
}
		/* Lookup command. */
		PT_INIT(&ctx->pt);
		for (entry = smtp_cmd_table; entry->cmd != NULL; entry++) {
			if (0 < TextInsensitiveStartsWith(ctx->input.data, entry->cmd)) {
				(*entry->hook)(loop, event);
				break;
			}
		}

		if (entry->cmd == NULL)
			(void) SMTP_CMD_DO(unknown);
	}

	ctx->pipe.length = ctx->pipe.offset = 0;
end:
	SETJMP_POP(&ctx->on_error);
}

void
client_close_cb(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx = event->data;

	if (ctx != NULL) {
		TRACE_CTX(ctx, 000);
		*ctx->id_trans = '\0';

		LUA_CALL0(close);
		lua_hook_endthread(lua_live, ctx);

		VectorDestroy(ctx->recipients);
		dns_close(loop, event);
		free(ctx->sender);
		free(ctx);
	}
}

void
client_error_cb(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx = event->data;
	if (errno != 0)
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
	LUA_CALL(error);
	socketEventRemove(loop, event);
}

void
server_io_cb(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx;
	Socket2 *client;
	SocketEvent *client_event;
	char id_sess[ID_SIZE];

	next_session(id_sess);

	if (verb_trace.value)
		syslog(LOG_DEBUG, LOG_FMT "%s", id_sess, __FUNCTION__);

	if ((client = socketAccept(event->socket)) == NULL) {
		if (verb_warn.value)
			syslog(LOG_WARN, log_error, id_sess, LOG_LINE, strerror(errno), errno);
		return;
	}

//	(void) socketSetNagle(client, 1);
	(void) socketSetLinger(client, 0);
	(void) socketSetKeepAlive(client, 1);
	(void) socketSetNonBlocking(client, 1);
	(void) fileSetCloseOnExec(socketGetFd(client), 1);
	socketSetTimeout(client, opt_smtp_command_timeout.value);

	if ((client_event = socketEventAlloc(client, SOCKET_EVENT_READ)) == NULL) {
		syslog(LOG_ERR, log_oom, id_sess, LOG_LINE);
		socketClose(client);
		return;
	}
	if ((ctx = calloc(1, SMTP_CTX_SIZE)) == NULL) {
		syslog(LOG_ERR, log_oom, id_sess, LOG_LINE);
		socketEventFree(client_event);
		return;
	}

	client_event->data = ctx;
	client_event->on.io = client_io_cb;
	client_event->on.close = client_close_cb;
	client_event->on.error = client_error_cb;

	TextCopy(ctx->id_sess, sizeof (ctx->id_sess), id_sess);

	ctx->loop = loop;
	ctx->event = client_event;
	ctx->client = client;
	ctx->transaction_count = (int) RAND_MSG_COUNT;
	ctx->is_enabled = socketEventGetEnable(client_event);

	ctx->path.size = PATH_MAX;
	ctx->addr.size = SMTP_DOMAIN_LENGTH+1;
	ctx->host.size = SMTP_DOMAIN_LENGTH+1;
	ctx->helo.size = SMTP_DOMAIN_LENGTH+1;
	ctx->auth.size = SMTP_DOMAIN_LENGTH+1;
	ctx->work.size = SMTP_TEXT_LINE_LENGTH+1;
	ctx->reply.size = SMTP_TEXT_LINE_LENGTH+1;
	ctx->pipe.size = SMTP_MINIMUM_MESSAGE_LENGTH;

	ctx->path.data = (unsigned char *) &ctx[1];
	ctx->addr.data = &ctx->path.data[ctx->path.size];
	ctx->host.data = &ctx->addr.data[ctx->addr.size];
	ctx->helo.data = &ctx->host.data[ctx->host.size];
	ctx->auth.data = &ctx->helo.data[ctx->helo.size];
	ctx->work.data = &ctx->auth.data[ctx->auth.size];
	ctx->reply.data = &ctx->work.data[ctx->work.size];
	ctx->pipe.data = &ctx->reply.data[ctx->reply.size];

	if ((ctx->recipients = VectorCreate(10)) == NULL) {
		syslog(LOG_ERR, log_oom, id_sess, LOG_LINE);
		socketEventFree(client_event);
		return;
	}
	VectorSetDestroyEntry(ctx->recipients, free);

	socketAddressGetIPv6(&client->address, 0, ctx->ipv6);
	ctx->addr.length = socketAddressGetString(&client->address, 0, ctx->addr.data, ctx->addr.size);

	if (SIGSETJMP(ctx->on_error, 1) != 0) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "close %s cc=%ld", LOG_ID(ctx), ctx->addr.data, VectorLength(loop->events)-1);
		if (client_event->on.error != NULL)
			(*client_event->on.error)(loop, client_event);
		return;
	}

	if (socketEventAdd(loop, client_event)) {
		syslog(LOG_ERR, log_oom, id_sess, LOG_LINE);
		socketEventFree(client_event);
		return;
	}

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "accept %s cc=%ld", LOG_ID(ctx), ctx->addr.data, VectorLength(loop->events)-1);

	PT_INIT(&ctx->pt);
	ctx->state = SMTP_CMD_HOOK(accept);
	(void) SMTP_CMD_HOOK(accept)(loop, client_event);
}

void
serverOptions(int argc, char **argv)
{
	int argi;

	TRACE_FN(000);

	/* Parse command line options looking for a file= option. */
	optionInit(opt_table, NULL);
	argi = optionArrayL(argc, argv, opt_table, NULL);

	/* Parse the option file followed by the command line options again. */
	if (opt_file.string != NULL && *opt_file.string != '\0') {
		/* Do NOT reset this option. */
		opt_file.initial = opt_file.string;
		opt_file.string = NULL;

		optionInit(opt_table, NULL);
		(void) optionFile(opt_file.string, opt_table, NULL);
		(void) optionArrayL(argc, argv, opt_table, NULL);
	}

	opt_smtp_accept_timeout.value *= UNIT_MILLI;
	opt_smtp_command_timeout.value *= UNIT_MILLI;
	opt_smtp_data_timeout.value *= UNIT_MILLI;
	opt_smtp_dot_timeout.value *= UNIT_MILLI;

	parse_path_flags = 0;
	if (opt_rfc2821_angle_brackets.value)
		parse_path_flags |= STRICT_SYNTAX;
	if (opt_rfc2821_local_length.value)
		parse_path_flags |= STRICT_LOCAL_LENGTH;
	if (opt_rfc2821_domain_length.value)
		parse_path_flags |= STRICT_DOMAIN_LENGTH;
	if (opt_rfc2821_literal_plus.value)
		parse_path_flags |= STRICT_LITERAL_PLUS;

	optionString(opt_verbose.string, verb_table, NULL);
}

void
sig_term(int signum)
{
	syslog(LOG_INFO, "signal %d received", signum);
	socketEventsStop(&main_loop);
}

#ifdef NOT_YET
void
sig_hup(int signum)
{
	syslog(LOG_INFO, "signal %d received", signum);
	lua_dying = lua_live;
	lua_live = NULL;
}
#endif

int
serverMain(void)
{
	int rc;
	Socket2 *socket;
	SocketEvent event;
	SocketAddress *saddr;

	TRACE_FN(000);
	rc = EXIT_FAILURE;
	syslog(LOG_INFO, _NAME " " _VERSION " " _COPYRIGHT);

	(void) umask(0002);
	(void) signal(SIGPIPE, SIG_IGN);
	(void) signal(SIGINT, sig_term);
	(void) signal(SIGHUP, sig_term);
	(void) signal(SIGQUIT, sig_term);
	(void) signal(SIGTERM, sig_term);
	rand_seed = time(NULL) ^ getpid();
	srand(rand_seed);

	if (socketInit()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_OSERR;
		goto error0;
	}

	PDQ_OPTIONS_SETTING(verb_dns.value);
	if (pdqInit()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_OSERR;
		goto error0;
	}

	if ((saddr = socketAddressNew("0.0.0.0", opt_smtp_server_port.value)) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error1;
	}
	if ((socket = socketOpen(saddr, 1)) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error2;
	}
	networkGetMyName(my_host_name);
	if ((lua_live = lua_hook_init()) == NULL) {
		rc = EX_SOFTWARE;
		goto error3;
	}

	(void) fileSetCloseOnExec(socketGetFd(socket), 1);
	(void) socketSetNonBlocking(socket, 1);
	(void) socketSetLinger(socket, 0);
	(void) socketSetReuse(socket, 1);

	if (socketServer(socket, opt_smtp_server_queue.value)) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error4;
	}

	socketEventsInit(&main_loop);
	socketEventInit(&event, socket, SOCKET_EVENT_READ);
	if (socketEventAdd(&main_loop, &event)) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error4;
	}

	event.on.io = server_io_cb;
	(void) socketSetTimeout(socket, -1);

	socketEventsRun(&main_loop);
	socketEventsFree(&main_loop);
	syslog(LOG_INFO, "terminated");
	rc = EXIT_SUCCESS;
error4:
	lua_close(lua_live);
error3:
	if (rc != EXIT_SUCCESS)
		socketClose(socket);
error2:
	free(saddr);
error1:
	pdqFini();
error0:
	return rc;
}

int
main(int argc, char **argv)
{
	verboseInit();
	serverOptions(argc, argv);

	if (opt_version.string != NULL) {
		printVersion();
		exit(EX_USAGE);
	}
	if (opt_info.string != NULL) {
		printInfo();
		exit(EX_USAGE);
	}
	if (opt_help.string != NULL) {
		optionUsageL(opt_table, NULL);
		exit(EX_USAGE);
	}

	if (opt_daemon.value) {
		openlog(_NAME, LOG_PID|LOG_NDELAY, LOG_MAIL);
		if (daemon(0, 0)) {
			syslog(LOG_ERR, "daemon mode failed");
			exit(EX_SOFTWARE);
		}
	} else {
		LogSetProgramName(_NAME);
		LogOpen("(standard error)");
	}

	return serverMain();
}

