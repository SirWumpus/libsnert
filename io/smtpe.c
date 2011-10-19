/*
 * smtpe.c
 *
 * SMTP Engine using IO events; an Events test.
 *
 * Copyright 2011 by Anthony Howe & Fort Systems Ltd. All rights reserved.
 */

#define _NAME				"smtpe"
#define _VERSION			"0.1"
#define _COPYRIGHT			LIBSNERT_COPYRIGHT
#define API_VERSION			"0.1"

#define HOOK_INIT
#define STRIP_ROOT_DOT

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

#if !defined(SAFE_PATH)
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

#if !defined(HASH_TABLE_SIZE)
#define HASH_TABLE_SIZE			(16 * 1024)
#endif

#if !defined(MAX_LINEAR_PROBE)
#define MAX_LINEAR_PROBE		24
#endif

#if !defined(SMTP_PIPELINING_TIMEOUT)
#define SMTP_PIPELINING_TIMEOUT		300
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
#include <com/snert/lib/io/events.h>
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/net/pdq.h>
#include <com/snert/lib/net/http.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/type/list.h>
#include <com/snert/lib/util/convertDate.h>
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/util/time62.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/md5.h>
#include <com/snert/lib/util/uri.h>

/***********************************************************************
 ***
 ***********************************************************************/

#ifndef PTHREAD_MUTEX_LOCK
#define PTHREAD_MUTEX_LOCK(m)
#define PTHREAD_MUTEX_UNLOCK(m)
#endif

#ifdef HAVE_RAND_R
# define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand_r(&rand_seed) / (RAND_MAX+1.0)))
#else
# define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand() / (RAND_MAX+1.0)))
#endif

#ifndef RAND_MSG_COUNT
#define RAND_MSG_COUNT		RANDOM_NUMBER(62.0*62.0)
#endif

#define LUA_CMD_INIT(fn)	hook_init ## fn
#define LUA_CMD_DEF(fn)		static int LUA_CMD_INIT(fn)(lua_State *L1, SmtpCtx *ctx)

typedef struct stmp_ctx SmtpCtx;

#define EVENT_NAME(fn)		fn ## _cb
#define EVENT_DEF(fn)		void EVENT_NAME(fn) EVENT_ARGS
#define EVENT_DO(fn)		(*EVENT_NAME(fn))(loop, _ev, revents)
#define EVENT_ARGS		(Events *loop, void *_ev, int revents)

#define SMTP_NAME(fn)		cmd_ ## fn
#define SMTP_DEF(fn)		PT_THREAD( SMTP_NAME(fn) SMTP_ARGS )
#define SMTP_DO(fn)		(*SMTP_NAME(fn))(loop, event)
#define SMTP_ARGS		(Events *loop, Event *event)

typedef pt_word_t (*SmtpCmdHook) SMTP_ARGS;

typedef struct {
	const char *cmd;
	SmtpCmdHook hook;
} Command;

struct map_integer {
	const char *name;
	lua_Integer value;
};

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

typedef int (*LuaHookInit)(lua_State *, SmtpCtx *);
typedef int (*LuaYieldHook)(lua_State *, SmtpCtx *);

typedef struct {
	pt_t pt;
	int thread;
	SmtpCmdHook smtp_state;
	LuaYieldHook yield_until;
	LuaYieldHook yield_after;
} Lua;

typedef struct {
	md5_state_t source;
	md5_state_t decode;
	char *content_type;
	char *content_encoding;
} MD5Mime;

typedef struct {
	pt_t pt;
	size_t size;
	long length;
	int line_no;
	int line_max;
	char **lines;
	SMTP_Reply_Code smtp_rc;
} MxRead;

typedef struct {
	pt_t pt;
	MxRead read;
	SOCKET socket;
	Event event;
	const char *host;
	const char *mail;
	Vector rcpts;
	unsigned rcpts_ok;
	const char *spool;
	size_t length;
} MxSend;

typedef struct {
	PDQ *pdq;
	int wait_all;
	PDQ_rr *answer;
	Event event;
	long timeout_sum;	/* overall tally of timeouts */
	long timeout_next;	/* timeout doubles each iteration. */
} Dns;

typedef struct service Service;
typedef int (*ServiceFn)(Service *, SmtpCtx *);
typedef pt_word_t (*ServicePt)(Service *, SmtpCtx *);

typedef struct {
	List list;
	Service *resume;	/* Service to resume, see service_io_cb. */
	int wait_for_all;
	int client_is_enabled;
} Services;

typedef enum {
	DROP_NO,
	DROP_LUA,
	DROP_RATE,
	DROP_WRITE,
	DROP_ERROR
} DropCode;

typedef struct {
	Event event;
	Events *loop;
	SOCKET socket;
	SocketAddress addr;
	int is_pipelining;
	int dropped;
	int enabled;
} Client;

#define ID_SIZE		20

struct stmp_ctx {
	char id_sess[ID_SIZE];
	char id_trans[ID_SIZE];
	int transaction_count;
	JMP_BUF on_error;
	ParsePath *sender;
	Vector rcpts;		/* Vector of char * */
	char **rcpt;		/* For interating over rcpts list. */
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
	lua_State *script;
	SMTP_Reply_Code smtp_rc;

	/* SMTP state */
	int is_dot;		/* 0 no trailing dot, else length of "dot" tail */
	unsigned eoh;		/* Offset to end-of-header seperator. */
	unsigned long length;	/* Message length. */
	SmtpCmdHook state;
	SmtpCmdHook state_helo;

	Lua lua;
	Dns pdq;
	MxSend mx;
	Mime *mime;
	MD5Mime md5;
	Vector headers;
	Services services;

	Client client;
	FILE *spool_fp;
};

#define SETJMP_PUSH(this_jb) \
	{ JMP_BUF _jb; memcpy(&_jb, this_jb, sizeof (_jb))

#define SETJMP_POP(this_jb) \
	memcpy(this_jb, &_jb, sizeof (_jb)); }

typedef enum {
	JMP_SET,
	JMP_DROP,
	JMP_ERROR,
	JMP_INTERNAL
} JmpCode;

/***********************************************************************
 *** Strings
 ***********************************************************************/

#define STRLEN(const_str)	(sizeof (const_str)-1)
#define CRLF	"\r\n"
#define LF	"\n"

#define LOG_FMT			"%s "
#define LOG_LINE		__FILE__, __LINE__
#define LOG_ID(ctx)		(ctx)->id_sess
#define LOG_TRAN(ctx)		((*(ctx)->id_trans == '\0') ? LOG_ID(ctx) : (ctx)->id_trans)
#define LOG_INT(ctx)		LOG_ID(ctx), LOG_LINE
#define LOG_FN_FMT		"%s %s "
#define LOG_FN(ctx)		LOG_ID(ctx), __FUNCTION__

#define CLIENT_FMT		"%s [%s] "
#define CLIENT_INFO(c)		(c)->host.data, (c)->addr.data

#define TRACE_FN(n)		if (verb_trace.value) syslog(LOG_DEBUG, "%s", __FUNCTION__)
#define TRACE_CTX(s, n)		if (verb_trace.value) syslog(LOG_DEBUG, LOG_FMT "%s", (s)->id_sess, __FUNCTION__)

static const char empty[] = "";

static const char log_init[] = "initialisation error %s.%d: %s (%d)";
static const char log_oom[] = LOG_FMT "out of memory %s.%d";
static const char log_internal[] = LOG_FMT "internal error %s.%d";
static const char log_buffer[] = LOG_FMT "buffer overflow %s.%d";
static const char log_error[] = LOG_FMT "error %s.%d: %s (%d)";

#define FMT(n)			" %sE" #n " "

static const char fmt_ok[] = "250 2.0.0 OK" CRLF;
static const char fmt_welcome[] = "220 %s ESMTP %s" CRLF;
static const char fmt_rate_client[] = "421 4.4.5" FMT(000) CLIENT_FMT "connections %ld exceed %ld/60s" CRLF;
static const char fmt_quit[] = "221 2.0.0 %s closing connection %s" CRLF;
static const char fmt_pipeline[] = "550 5.3.3" FMT(000) "pipelining not allowed" CRLF;
static const char fmt_no_rcpts[] = "554 5.5.0" FMT(000) "no recipients" CRLF;
static const char fmt_no_piping[] = "%d 5.5.0" FMT(000) "pipeline data after %s command" CRLF;
static const char fmt_missing_arg[] = "501 5.5.2" FMT(000) "missing argument" CRLF;
static const char fmt_unknown[] = "502 5.5.1" FMT(000) "%s command unknown" CRLF;
static const char fmt_out_seq[] = "503 5.5.1" FMT(000) "%s out of sequence" CRLF;
static const char fmt_data[] = "354 enter mail, end with \".\" on a line by itself" CRLF;
static const char fmt_auth_already[] = "503 5.5.1" FMT(000) "already authenticated" CRLF;
static const char fmt_auth_mech[] = "504 5.5.4" FMT(000) "unknown AUTH mechanism" CRLF;
static const char fmt_auth_ok[] = "235 2.0.0" FMT(000) "authenticated" CRLF;
static const char fmt_syntax[] = "501 5.5.2" FMT(000) "syntax error" CRLF;
static const char fmt_bad_args[] = "501 5.5.4" FMT(000) "invalid argument %s" CRLF;
static const char fmt_internal[] = "421 4.3.0" FMT(000) "internal error" CRLF;
static const char fmt_internal2[] = "421 4.3.0 internal error" CRLF;
static const char fmt_buffer[] = "421 4.3.0" FMT(000) "buffer overflow" CRLF;
static const char fmt_mail_parse[] = "%d %s" FMT(000) "" CRLF;
static const char fmt_mail_size[] = "552 5.3.4" FMT(000) "message size exceeds %ld" CRLF;
static const char fmt_mail_ok[] = "250 2.1.0 sender <%s> OK" CRLF;
static const char fmt_rcpt_parse[] = "%d %s" FMT(000) "" CRLF;
static const char fmt_rcpt_null[] = "550 5.7.1" FMT(000) "null recipient invalid" CRLF;
static const char fmt_rcpt_ok[] = "250 2.1.0 recipient <%s> OK" CRLF;
static const char fmt_msg_ok[] = "250 2.0.0" FMT(000) "message %s accepted" CRLF;
static const char fmt_msg_try_again[] = "451 4.4.5" FMT(000) "try again later %s" CRLF;
static const char fmt_msg_reject[] = "550 5.7.0" FMT(000) "message %s rejected" CRLF;
static const char fmt_msg_empty[] = "550 5.6.0" FMT(000) "message %s is empty" CRLF;

static const char fmt_helo[] = "250 Hello %s (%s, %s)" CRLF;

static const char fmt_ehlo[] =
"250-Hello %s (%s, %s)" CRLF
"250-ENHANCEDSTATUSCODES" CRLF	/* RFC 2034 */
"%s"				/* XCLIENT */
"%s"				/* RFC 2920 pipelining */
"250-AUTH PLAIN" CRLF		/* RFC 2554 */
"250 SIZE %ld" CRLF		/* RFC 1870 */
;

static const char fmt_help[] =
"214-2.0.0 ESMTP supported commands:" CRLF
"214-2.0.0     AUTH    DATA    EHLO    HELO    HELP" CRLF
"214-2.0.0     NOOP    MAIL    RCPT    RSET    QUIT" CRLF
"214-2.0.0" CRLF
"214-2.0.0 ESMTP commands not implemented:" CRLF
"214-2.0.0     ETRN    EXPN    TURN    VRFY" CRLF
"214-2.0.0" CRLF
"214-2.0.0 Administration commands:" CRLF
"214-2.0.0     VERB    XCLIENT" CRLF
"214-2.0.0" CRLF
#ifdef ADMIN_CMDS
"214-2.0.0 Administration commands:" CRLF
"214-2.0.0     CONN    CACHE   INFO    KILL    LKEY    OPTN" CRLF
"214-2.0.0     STAT    VERB    XCLIENT" CRLF
"214-2.0.0" CRLF
#endif
"214 2.0.0 End" CRLF
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
Option opt_test			= { "test",			"-",		"Interactive interpreter test mode." };

static const char usage_events_wait[] =
  "Runtime selection of eventsWait() method: kqueue, epoll, poll.\n"
"# Leave blank for the system default.\n"
"#"
;
Option opt_events_wait_fn	= { "events-wait",		"",		usage_events_wait };

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
static const char usage_smtp_reply_timeout[] =
  "Timeout in seconds to wait after a SMTP reply is returned to the\n"
"# client.\n"
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
Option opt_smtp_reply_timeout	= { "smtp-reply-timeout",	QUOTE(SMTP_COMMAND_TO),		usage_smtp_reply_timeout };
Option opt_smtp_error_url	= { "smtp-error-url",		"",				usage_smtp_error_url };
Option opt_smtp_max_size	= { "smtp-max-size",		"0",				usage_smtp_max_size };
Option opt_smtp_server_port	= { "smtp-server-port",		QUOTE(SMTP_PORT), 		usage_smtp_server_port };
Option opt_smtp_server_queue	= { "smtp-server-queue",	"20",				usage_smtp_server_queue };
Option opt_smtp_smart_host	= { "smtp-smart-host", 		"",		 		usage_smtp_smart_host };
Option opt_smtp_xclient		= { "smtp-xclient", 		"+", 				usage_smtp_xclient };
Option opt_spool_dir		= { "spool-dir",		"/tmp",				usage_spool_dir };

static const char usage_smtp_default_at_dot[] =
  "The default reply to send at dot, when no alternative reply given.\n"
"# The option can be set to 250 (accept), 451 (try again later), or\n"
"# 550 (reject). This option is intended for testing and will typically\n"
"# be overridden by the Lua hook.dot() function.\n"
"#"
;
Option opt_smtp_default_at_dot	= { "smtp-default-at-dot",	QUOTE(451),	usage_smtp_default_at_dot };

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

Option opt_rfc2920_pipelining		= { "rfc2920-pipelining-enable","-",	usage_rfc2920_pipelining };
Option opt_rfc2920_pipelining_reject	= { "rfc2920-pipelining-reject","-",	usage_rfc2920_pipelining_reject };
Option opt_rfc2821_angle_brackets	= { "rfc2821-angle-brackets",	"-", 	usage_rfc2821_angle_brackets };
Option opt_rfc2821_local_length		= { "rfc2821-local-length", 	"-", 	usage_rfc2821_local_length };
Option opt_rfc2821_domain_length	= { "rfc2821-domain-length", 	"-", 	usage_rfc2821_domain_length };
Option opt_rfc2821_literal_plus		= { "rfc2821-literal-plus", 	"-", 	usage_rfc2821_literal_plus };

static const char usage_rate_global[] =
  "Overall client connections per second allowed before imposing a\n"
"# one second delay. Specify zero (0) to disable.\n"
"#"
;
static const char usage_rate_client[] =
  "The number of connections per minuute a unique client is permitted.\n"
"# Specify zero (0) to disable.\n"
"#"
;
Option opt_rate_global			= { "rate-global", 	"100", 		usage_rate_global };
Option opt_rate_client			= { "rate-client", 	"0", 		usage_rate_client };


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

Option verb_http	= { "http",		"-", empty };
Option verb_clamd	= { "clamd",		"-", empty };
Option verb_mime	= { "mime",		"-", empty };
Option verb_spamd	= { "spamd",		"-", empty };
Option verb_service	= { "service",		"-", empty };
Option verb_uri		= { "uri",		"-", empty };

/***********************************************************************
 *** Globals
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
	&opt_test,
	&opt_events_wait_fn,
	&opt_version,

	PDQ_OPTIONS_TABLE,

	&opt_rate_global,
	&opt_rate_client,

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
	&opt_smtp_reply_timeout,
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

	&verb_clamd,
	&verb_dns,
	&verb_http,
	&verb_mime,
	&verb_service,
	&verb_smtp,
#ifdef NOT_USED
	&verb_smtp_data,
	&verb_smtp_dot,
#endif
	&verb_spamd,
	&verb_uri,

	&verb_connect,
	&verb_helo,
	&verb_auth,
	&verb_mail,
	&verb_rcpt,
	&verb_data,
	&verb_noop,
	&verb_rset,

	NULL
};

static Vector smart_hosts;
static unsigned rand_seed;
static int parse_path_flags;
static Events *main_loop;
static const char *smtp_default_at_dot;
static char my_host_name[SMTP_DOMAIN_LENGTH+1];

void client_write(SmtpCtx *ctx, Buffer *);
void client_send(SmtpCtx *ctx, const char *fmt, ...);

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

/***********************************************************************
 *** Lua support functions
 ***********************************************************************/

static void
lua_table_getglobal(lua_State *L, const char *name)
{
	lua_getglobal(L, name);				/* __svc */
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);				/* -- */
		lua_newtable(L);			/* __svc */
	}
}

static void
lua_table_getfield(lua_State *L, int table_index, const char *name)
{
	lua_getfield(L, table_index, name);				/* __svc */
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);				/* -- */
		lua_newtable(L);			/* __svc */
	}
}

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

static Vector
lua_array_to_vector(lua_State *L, int table_index)
{
	Vector v;
	size_t i, length;
	char *string, *el;

	if (!lua_istable(L, table_index))
		return NULL;

	length = lua_objlen(L, table_index);

	if ((v = VectorCreate(length)) == NULL)
		return NULL;
	VectorSetDestroyEntry(v, free);

	for (i = 1; i <= length; i++) {
		lua_pushinteger(L, i);				/* i */
		lua_gettable(L, table_index-(table_index < 0));	/* el */
		el = (char *)lua_tostring(L, -1);		/* el */
		lua_pop(L, 1);					/* -- */

		/* Element has to string or number. Or there might be a
		 * gap at this index, which means its not a proper array.
		 */
		if (el == NULL)
			continue;

		string = strdup(el);
		if (string == NULL || VectorAdd(v, string)) {
			VectorDestroy(v);
			return NULL;
		}
	}

	return v;
}

static void
lua_vector_to_array(lua_State *L, Vector v)
{
	char **string;

	lua_newtable(L);					/* t */
	for (string = (char **)VectorBase(v); *string != NULL; string++) {
		lua_array_push_string(L, -1, *string);
	}
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
lua_smtp_ctx(lua_State *L)
{
	SmtpCtx *ctx;

	lua_getglobal(L, "__ctx");
	ctx = lua_touserdata(L, -1);
	lua_pop(L, 1);

	return ctx;
}

static lua_State *
lua_getthread(lua_State *L, SmtpCtx *ctx)
{
	lua_State *thread;

	lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->lua.thread);
	thread = lua_tothread(L, -1);
	lua_pop(L, 1);

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua.thread);

	return thread;
}

LUA_CMD_DEF(error);
static PT_THREAD(hook_do(lua_State *L, SmtpCtx *ctx, const char *hook, LuaHookInit initfn));

void
sigsetjmp_action(SmtpCtx *ctx, JmpCode jc)
{
	switch (jc) {
	case JMP_INTERNAL:
		client_send(ctx, fmt_internal, opt_smtp_error_url.string, strerror(errno));
		/*@fallthrough@*/
	case JMP_ERROR:
#ifdef ORIGINALLY
		LUA_CALL_SETJMP(error);
#else
		SETJMP_PUSH(&ctx->on_error);
		if (SIGSETJMP(ctx->on_error, 1) == JMP_SET) {
			PT_INIT(&ctx->lua.pt);
			while (PT_SCHEDULE(hook_do(ctx->script, ctx, "error", hook_initerror)))
				;
		}
		SETJMP_POP(&ctx->on_error);
#endif
		ctx->client.dropped = DROP_ERROR;
		/*@fallthrough@*/
	case JMP_DROP:
		eventRemove(ctx->client.loop, &ctx->client.event);
#ifndef USE_LIBEV
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "close %s cc=%lu", LOG_ID(ctx), ctx->addr.data, (unsigned long)ctx->client.loop->events.length);
#endif
		/*@fallthrough@*/
	case JMP_SET:
		break;
	}
}

/***********************************************************************
 *** Service Event Support
 ***********************************************************************/

struct service {
	pt_t pt;
	void *data;
	FreeFn free;		/* How to free data. */
	char *host;
	SmtpCtx *ctx;
	ListItem link;
	SOCKET socket;
	Event event;
	CLOCK started;
	ServicePt service;
	ServiceFn results;
};

void
service_event_free(void *_event)
{
	SmtpCtx *ctx;
	Service *svc;

	if (_event != NULL) {
		svc = ((Event *)_event)->data;
		ctx = svc->ctx;

		TRACE_CTX(ctx, 000);

		listDelete(&ctx->services.list, &svc->link);
		if (ctx->services.list.length == 0)
			eventSetEnabled(&ctx->client.event, ctx->services.client_is_enabled);

		if (svc->free != NULL)
			(*svc->free)(svc->data);
		free(svc->host);
		free(svc);
	}
}

EVENT_DEF(service_close)
{
	/* eventRemove() handles the clean up of a service
	 * via VectorRemove() -> service_event_free().
	 */
	eventRemove(loop, eventGetBase(_ev));
}

EVENT_DEF(service_io)
{
	Event *event = eventGetBase(_ev);
	JmpCode jc;
	Service *svc = event->data;
	SmtpCtx *ctx = svc->ctx;

	TRACE_CTX(ctx, 000);

	SETJMP_PUSH(&ctx->on_error);
	if ((jc = SIGSETJMP(ctx->on_error, 1)) != JMP_SET) {
		eventDoTimeout(EVENT_NAME(service_close), loop, event, revents);
	} else {
		eventResetTimeout(event);

		/* Remember which service to resume. */
		ctx->services.resume = svc;

		/* Resume the service indirectly through the
		 * Lua co-routine managed via hook_do().
		 */
		(*ctx->state)(loop, &ctx->client.event);
	}
	SETJMP_POP(&ctx->on_error);
	sigsetjmp_action(ctx, jc);
}

static Service *
service_new(SmtpCtx *ctx)
{
	Service *svc;

	if ((svc = calloc(1, sizeof (*svc))) != NULL) {
		svc->ctx = ctx;
		PT_INIT(&svc->pt);
		svc->link.data = svc;

		/* NOTE svc->link.free should always remain NULL.
		 * eventRemove() handles the clean up of a service
		 * via VectorRemove() -> service_event_free().
		 */
	}

	return svc;
}

static int
service_add(SmtpCtx *ctx, Service *svc, long timeout)
{
	TRACE_CTX(ctx, 000);

	if (ctx == NULL || svc == NULL)
		return -1;

	eventInit(&svc->event, svc->socket, EVENT_READ|EVENT_WRITE);
	eventSetCbTimer(&svc->event, EVENT_NAME(service_close));
	eventSetTimeout(&svc->event, timeout);
	eventSetCbIo(&svc->event, EVENT_NAME(service_io));
	svc->event.free = service_event_free;
	svc->event.data = svc;

	if (eventAdd(ctx->client.loop, &svc->event)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		return -1;
	}

	/* Disable the client event until the service completes. */
	if (ctx->services.list.length == 0)
		ctx->services.client_is_enabled = eventGetEnabled(&ctx->client.event);
	eventSetEnabled(&ctx->client.event, 0);

	listInsertBefore(&ctx->services.list, NULL, &svc->link);
	TIMER_START(svc->started);

	return 0;
}

static Service *
service_open(SmtpCtx *ctx, Vector hosts, int port, long timeout)
{
	char **host;
	Service *svc;

	TRACE_CTX(ctx, 000);

	if ((svc = service_new(ctx)) == NULL) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		goto error0;
	}
	for (host = (char **)VectorBase(hosts); *host != NULL; host++) {
		if (verb_service.value)
			syslog(LOG_DEBUG, LOG_FMT ">> trying %s", LOG_TRAN(ctx), *host);
		if (0 <= (svc->socket = socket3_connect(*host, port, timeout)))
			break;
	}
	if (*host == NULL)
		goto error1;

	(void) fileSetCloseOnExec(svc->socket, 1);
	(void) socket3_set_nonblocking(svc->socket, 1);
	(void) socket3_set_linger(svc->socket, 0);
	svc->host = *host;
	*host = NULL;

	if (service_add(ctx, svc, timeout)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		goto error2;
	}

	if (verb_service.value)
		syslog(LOG_DEBUG, LOG_FMT ">> connected %s", LOG_TRAN(ctx), *host);

	return svc;
error2:
	socket3_close(svc->socket);
error1:
	free(svc);
error0:
	return NULL;
}

static void
service_time(Service *svc, lua_State *L, int table_index)
{
	CLOCK elapsed;

	CLOCK_GET(&elapsed);
	CLOCK_SUB(&elapsed, &svc->started);
	lua_pushnumber(L, CLOCK_TO_DOUBLE(&elapsed));
	lua_setfield(L, table_index - (table_index < 0), "elapsed_time");

	lua_pushstring(L, svc->host);
	lua_setfield(L, table_index - (table_index < 0), "service_host");
}

static int
service_until(lua_State *L, SmtpCtx *ctx)
{
	int nargs;
	Service *svc = ctx->services.resume;

	TRACE_CTX(ctx, 000);

	if (!PT_SCHEDULE((*svc->service)(svc, ctx))) {
		/* Service has finished. Collect the results. */
		if (svc->results != NULL) {
			nargs = (*svc->results)(svc, ctx);	/* ... */
			lua_pop(L, nargs);
		}
		eventDoIo(EVENT_NAME(service_close), ctx->client.loop, &svc->event, 0);

		return ctx->services.list.length == 0 || !ctx->services.wait_for_all;
	}

	return 0;
}

static int
service_result(lua_State *L, SmtpCtx *ctx)
{
	TRACE_CTX(ctx, 000);

	lua_getglobal(L, "__service");	/* __svc */
	lua_pushnil(L);			/* __svc nil */
	lua_setglobal(L, "__service");	/* __svc */

	return 1;
}

static int
service_wait(lua_State *L)
{
	SmtpCtx *ctx = lua_smtp_ctx(L);

	TRACE_CTX(ctx, 000);

	ctx->services.wait_for_all = luaL_optint(L, 1, 1);
	ctx->lua.yield_until = service_until;
	ctx->lua.yield_after = service_result;

	return lua_yield(L, 0);
}

static int
service_reset(lua_State *L)
{
	Service *svc;
	SmtpCtx *ctx = lua_smtp_ctx(L);

	TRACE_CTX(ctx, 000);

	while (0 < ctx->services.list.length) {
		svc = ctx->services.list.head->data;
		eventDoIo(EVENT_NAME(service_close), ctx->client.loop, &svc->event, 0);
	}

	return 0;
}

/***********************************************************************
 *** Client.Send Service
 ***********************************************************************/

typedef struct {
	Buffer buffer;
} ClientData;

static
PT_THREAD(client_yielduntil(Service *svc, SmtpCtx *ctx))
{
	ClientData *cd;

	PT_BEGIN(&svc->pt);

	cd = svc->data;

	if (0 < verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "< %lu:%s", LOG_TRAN(ctx), cd->buffer.length, cd->buffer.data);

	if (socket3_write(svc->socket, (unsigned char *) cd->buffer.data, cd->buffer.length, NULL) != cd->buffer.length) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		PT_EXIT(&svc->pt);
	}

	PT_YIELD(&svc->pt);

	PT_END(&svc->pt);
}

static void
client_free(void *data)
{
	ClientData *cd = data;

	if (cd != NULL) {
		free(cd->buffer.data);
		free(cd);
	}
}

/**
 * boolean = service.client.write(string)
 */
static int
service_client_write(lua_State *L)
{
	Service *svc;
	ClientData *cd;
	const char *string;
	SmtpCtx *ctx = lua_smtp_ctx(L);

	if ((cd = calloc(1, sizeof (*cd))) == NULL)
		goto error0;

	string = luaL_optlstring(L, 1, NULL, (size_t *)&cd->buffer.length);
	if (string == NULL)
		goto error1;

	cd->buffer.size = cd->buffer.length+1;
	cd->buffer.data = strdup(string);

	if ((svc = service_new(ctx)) == NULL)
		goto error1;

	svc->data = cd;
	svc->free = client_free;
	svc->service = client_yielduntil;
	svc->socket = ctx->client.socket;
	svc->host = strdup(ctx->host.data);

	if (service_add(ctx, svc, luaL_optint(L, 2, opt_smtp_reply_timeout.value)))
		goto error2;

	lua_pushboolean(L, 1);

	return 1;
error2:
	free(svc);
error1:
	client_free(svc);
error0:
	lua_pushboolean(L, 0);

	return 1;
}

static int
lua_client_write(lua_State *L)
{
	int is_ready;

	(void) service_client_write(L);
	is_ready = lua_toboolean(L, -1);
	lua_pop(L, lua_gettop(L));

	if (is_ready)
		return service_wait(L);

	return -1;
}

static const luaL_Reg lua_client_pkg[] = {
	{ "write", 			lua_client_write },
	{ NULL, NULL },
};

static void
lua_define_client(lua_State *L)
{
	luaL_register(L, "client", lua_client_pkg);		/* pkg */
	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Clamd Support
 ***********************************************************************/

#ifndef CLAMD_PORT
#define CLAMD_PORT			3310
#endif

#ifndef CLAMD_TIMEOUT
#define CLAMD_TIMEOUT			(120 * UNIT_MILLI)
#endif

typedef struct {
	FILE *fp;
	char *filepath;
	Buffer buffer;
} Clamd;

static
PT_THREAD(clamd_yielduntil(Service *svc, SmtpCtx *ctx))
{
	Clamd *cd;
	long offset;
	uint32_t size;
	lua_State *L1;

	PT_BEGIN(&svc->pt);

	cd = svc->data;

	if (*svc->host != '/' && !isReservedIP(svc->host, IS_IP_LOCAL)) {
		/* Stream the file to clamd. */
		if ((cd->fp = fopen(cd->filepath, "rb")) == NULL) {
			syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
			PT_EXIT(&svc->pt);
		}

		for (;;) {
			cd = svc->data;

			if ((cd->buffer.length = fread(cd->buffer.data, 1, cd->buffer.size, cd->fp)) <= 0)
				break;

			size = htonl(cd->buffer.length);

			if (socket3_write(svc->socket, (unsigned char *) &size, sizeof (size), NULL) != sizeof (size)) {
				syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
				PT_EXIT(&svc->pt);
			}

			if (1 < verb_clamd.value)
				syslog(LOG_DEBUG, LOG_FMT "clamd >> %ld:%s", LOG_TRAN(ctx), cd->buffer.length, cd->buffer.data);

			if (socket3_write(svc->socket, (unsigned char *) cd->buffer.data, cd->buffer.length, NULL) != cd->buffer.length) {
				syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
				PT_EXIT(&svc->pt);
			}

			PT_YIELD(&svc->pt);
		}

		/* Send end of file. */
		size = 0;
		if (socket3_write(svc->socket, (unsigned char *) &size, sizeof (size), NULL) != sizeof (size)) {
			syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
			PT_EXIT(&svc->pt);
		}

		if (verb_clamd.value == 1)
			syslog(LOG_DEBUG, LOG_FMT "clamd >> (wrote %ld bytes)", LOG_TRAN(ctx), ftell(cd->fp));

		(void) fclose(cd->fp);
		cd->fp = NULL;
	}

	/* Get the clamd response line. */
	cd->buffer.length = 0;
	eventSetType(&svc->event, EVENT_READ);

	do {
		PT_YIELD(&svc->pt);
		cd = svc->data;
		cd->buffer.offset = socket3_read(
			svc->socket,
			(unsigned char *) cd->buffer.data+cd->buffer.length,
			cd->buffer.size-cd->buffer.length,
			NULL
		);

		cd->buffer.data[cd->buffer.length+cd->buffer.offset] = '\0';
		if (0 < verb_clamd.value)
			syslog(LOG_DEBUG, LOG_FMT "clamd << %ld:%s", LOG_TRAN(ctx), cd->buffer.offset, cd->buffer.data+cd->buffer.length);
		cd->buffer.length += cd->buffer.offset;
	} while (0 < cd->buffer.offset && cd->buffer.length < cd->buffer.size-1 && cd->buffer.data[cd->buffer.length-1] != '\n');

	socket3_close(svc->socket);

	if ((L1 = lua_getthread(ctx->script, ctx)) != NULL) {
		lua_table_getglobal(L1, "__service");	/* __svc */
		lua_newtable(L1);			/* __svc clamd */

		lua_pushliteral(L1, "clamd");
		lua_setfield(L1, -2, "service_name");
		service_time(svc, L1, -1);

		lua_pushstring(L1, cd->filepath);
		lua_setfield(L1, -2, "file");
		lua_pushlstring(L1, cd->buffer.data, cd->buffer.length);
		lua_setfield(L1, -2, "reply");

		offset = TextFind(cd->buffer.data, "*FOUND*", cd->buffer.length, 1);
		lua_pushboolean(L1, 0 <= offset);
		lua_setfield(L1, -2, "is_infected");

		lua_setfield(L1, -2, "clamd");				/* __svc */
		lua_setglobal(L1, "__service");				/* -- */
	}

	PT_END(&svc->pt);
}

static void
clamd_free(void *data)
{
	Clamd *cd = data;

	if (cd != NULL) {
		if (cd->fp != NULL)
			(void) fclose(cd->fp);
		free(cd->filepath);
		free(cd);
	}
}

/**
 * boolean = service.clamd(filepath[, host_list[, timeout]])
 */
static int
service_clamd(lua_State *L)
{
	Clamd *cd;
	int is_scan;
	Service *svc;
	Vector host_list;
	SmtpCtx *ctx = lua_smtp_ctx(L);
	int timeout = luaL_optint(L, 3, CLAMD_TIMEOUT);

	if ((cd = calloc(1, sizeof (*cd) + SMTP_TEXT_LINE_LENGTH)) == NULL)
		goto error0;

	cd->buffer.data = (char *) &cd[1];
	cd->buffer.size = SMTP_TEXT_LINE_LENGTH;
	cd->filepath = strdup(luaL_optstring(L, 1, NULL));

	switch (lua_type(L, 2)) {
	case LUA_TTABLE:
		host_list = lua_array_to_vector(L, 2);
		break;

	case LUA_TSTRING:
		if ((host_list = TextSplit(luaL_optstring(L, 2, "127.0.0.1"), ";, ", 0)) == NULL) {
			syslog(LOG_ERR, log_oom, LOG_INT(ctx));
			goto error1;
		}
		break;
	}

	if ((svc = service_open(ctx, host_list, CLAMD_PORT, timeout)) == NULL)
		goto error2;
	VectorDestroy(host_list);

	if (verb_clamd.value)
		syslog(LOG_DEBUG, LOG_FMT "clamd >> connected %s", LOG_TRAN(ctx), svc->host);

	svc->data = cd;
	svc->free = clamd_free;
	svc->service = clamd_yielduntil;

	if ((is_scan = (*svc->host == '/' || isReservedIP(svc->host, IS_IP_LOCAL)))) {
		cd->buffer.length = snprintf(cd->buffer.data, cd->buffer.size, "nSCAN %s\n", cd->filepath);
		eventSetType(&svc->event, EVENT_READ);
	} else {
		cd->buffer.length = TextCopy(cd->buffer.data, cd->buffer.size, "nINSTREAM\n");
		eventSetType(&svc->event, EVENT_WRITE);
	}

	if (0 < verb_clamd.value)
		syslog(LOG_DEBUG, LOG_FMT "clamd >> %ld:%s", LOG_TRAN(ctx), cd->buffer.length, cd->buffer.data);

	/* Send the clamd command. */
	if (socket3_write(svc->socket, (unsigned char *) cd->buffer.data, cd->buffer.length, NULL) != cd->buffer.length) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error3;
	}

	lua_pushboolean(L, 1);

	return 1;
error3:
	eventDoIo(EVENT_NAME(service_close), ctx->client.loop, &svc->event, 0);
error2:
	VectorDestroy(host_list);
error1:
	clamd_free(svc);
error0:
	lua_pushboolean(L, 0);

	return 1;
}

/***********************************************************************
 *** Spamd Support
 ***********************************************************************/

#ifndef SPAMD_PORT
#define SPAMD_PORT			783
#endif

#ifndef SPAMD_TIMEOUT
#define SPAMD_TIMEOUT			(120 * UNIT_MILLI)
#endif

#ifndef SPAMD_BUFFER
#define SPAMD_BUFFER			(4 * 1024)
#endif

typedef struct {
	FILE *fp;
	char **hdr;
	int replace_msg;
	char *filepath;
	Buffer buffer;
} Spamd;

static
PT_THREAD(spamd_yielduntil(Service *svc, SmtpCtx *ctx))
{
	Spamd *sd;
	long offset;
	lua_State *L1;
	size_t length;

	PT_BEGIN(&svc->pt);

	sd = svc->data;

	/* Stream the file. */
	if ((sd->fp = fopen(sd->filepath, "rb")) == NULL || fseek(sd->fp, ctx->eoh, SEEK_SET)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		PT_EXIT(&svc->pt);
	}

	for (sd->hdr = (char **) VectorBase(ctx->headers); *sd->hdr != NULL; sd->hdr++) {
		length = strlen(*sd->hdr);

		if (1 < verb_spamd.value)
			syslog(LOG_DEBUG, LOG_FMT "spamd >> %lu:%s", LOG_TRAN(ctx), (unsigned long)length+STRLEN(CRLF), *sd->hdr);

		if (socket3_write(svc->socket, (unsigned char *)*sd->hdr, length, NULL) != length) {
			syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
			PT_EXIT(&svc->pt);
		}
		if (socket3_write(svc->socket, (unsigned char *)CRLF, STRLEN(CRLF), NULL) != STRLEN(CRLF)) {
			syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
			PT_EXIT(&svc->pt);
		}

		PT_YIELD(&svc->pt);

		/* Make sure to restore the spamd structure pointer
		 * each iteration as a PT_YIELD() means the state
		 * of local variables will be undefined.
		 */
		sd = svc->data;
	}

	if (socket3_write(svc->socket, (unsigned char *)CRLF, STRLEN(CRLF), NULL) != STRLEN(CRLF)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		PT_EXIT(&svc->pt);
	}

	for (;;) {
		sd = svc->data;

		if ((sd->buffer.length = fread(sd->buffer.data, 1, sd->buffer.size, sd->fp)) <= 0)
			break;

		if (1 < verb_spamd.value)
			syslog(LOG_DEBUG, LOG_FMT "spamd >> %ld:%s", LOG_TRAN(ctx), sd->buffer.length, sd->buffer.data);

		if (socket3_write(svc->socket, (unsigned char *) sd->buffer.data, sd->buffer.length, NULL) != sd->buffer.length) {
			syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
			PT_EXIT(&svc->pt);
		}

		PT_YIELD(&svc->pt);
	}

	if (verb_spamd.value == 1)
		syslog(LOG_DEBUG, LOG_FMT "spamd >> (wrote %ld bytes)", LOG_TRAN(ctx), ftell(sd->fp));

	(void) fclose(sd->fp);
	sd->fp = NULL;

	if (sd->replace_msg && (sd->fp = fopen(sd->filepath, "wb")) == NULL) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		PT_EXIT(&svc->pt);
	}

	/* Get the response. */
	sd->buffer.length = 0;
	eventSetType(&svc->event, EVENT_READ);
	socket3_shutdown(svc->socket, SHUT_WR);

	do {
		PT_YIELD(&svc->pt);
		sd = svc->data;
		sd->buffer.offset = socket3_read(
			svc->socket,
			(unsigned char *) sd->buffer.data+sd->buffer.length,
			sd->buffer.size-sd->buffer.length,
			NULL
		);

		sd->buffer.data[sd->buffer.length+sd->buffer.offset] = '\0';
		if (0 < verb_spamd.value)
			syslog(LOG_DEBUG, LOG_FMT "spamd << %ld:%s", LOG_TRAN(ctx), sd->buffer.offset, sd->buffer.data+sd->buffer.length);
		if (sd->replace_msg) {
			if (socket3_write(svc->socket, (unsigned char *) sd->buffer.data+sd->buffer.length, sd->buffer.offset, NULL) != sd->buffer.offset) {
				syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
				PT_EXIT(&svc->pt);
			}
		}
		sd->buffer.length += sd->buffer.offset;
	} while (0 < sd->buffer.offset && sd->buffer.length < sd->buffer.size-1);

	socket3_close(svc->socket);
	if (sd->replace_msg) {
		(void) fclose(sd->fp);
		sd->fp = NULL;
	}

	if ((L1 = lua_getthread(ctx->script, ctx)) != NULL) {
		lua_table_getglobal(L1, "__service");	/* __svc */
		lua_newtable(L1);					/* __svc spamd */

		lua_pushliteral(L1, "spamd");
		lua_setfield(L1, -2, "service_name");
		service_time(svc, L1, -1);

		lua_pushstring(L1, sd->filepath);
		lua_setfield(L1, -2, "file");

		lua_pushlstring(L1, sd->buffer.data, sd->buffer.length);
		lua_setfield(L1, -2, "reply");

		offset = TextFind(sd->buffer.data, "*spam: true*", sd->buffer.length, 1);
		lua_pushboolean(L1, 0 <= offset);
		lua_setfield(L1, -2, "is_spam");

		lua_setfield(L1, -2, "spamd");				/* __svc */
		lua_setglobal(L1, "__service");				/* -- */
	}

	PT_END(&svc->pt);
}

static void
spamd_free(void *data)
{
	Spamd *sd = data;

	if (sd != NULL) {
		if (sd->fp != NULL)
			(void) fclose(sd->fp);
		free(sd->filepath);
		free(sd);
	}
}

/**
 * boolean = service.clamd(filepath[, host_list[, method[, user[, timeout]]]])
 */
static int
service_spamd(lua_State *L)
{
	Spamd *sd;
	int length;
	Service *svc;
	struct stat sb;
	Vector host_list;
	const char *user, *method;
	SmtpCtx *ctx = lua_smtp_ctx(L);
	char data[SMTP_TEXT_LINE_LENGTH];
	int timeout = luaL_optint(L, 5, CLAMD_TIMEOUT);

	if ((sd = calloc(1, sizeof (*sd) + SPAMD_BUFFER)) == NULL)
		goto error0;

	sd->buffer.data = (char *) &sd[1];
	sd->buffer.size = SMTP_TEXT_LINE_LENGTH;
	sd->filepath = strdup(luaL_optstring(L, 1, NULL));

	switch (lua_type(L, 2)) {
	case LUA_TTABLE:
		host_list = lua_array_to_vector(L, 2);
		break;

	case LUA_TSTRING:
		if ((host_list = TextSplit(luaL_optstring(L, 2, "127.0.0.1"), ";, ", 0)) == NULL) {
			syslog(LOG_ERR, log_oom, LOG_INT(ctx));
			goto error1;
		}
		break;
	}

	if ((svc = service_open(ctx, host_list, SPAMD_PORT, timeout)) == NULL)
		goto error2;
	VectorDestroy(host_list);

	if (0 < verb_spamd.value)
		syslog(LOG_DEBUG, LOG_FMT "spamd >> connected %s", LOG_TRAN(ctx), svc->host);

	svc->data = sd;
	svc->free = spamd_free;
	svc->service = spamd_yielduntil;

	if (stat(sd->filepath, &sb)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error3;
	}

 	/* Add length of end-of-header marker and simulated Return-Path: header. */
 	(void) TextCopy(data, sizeof (data), CRLF);
 	length = ctx->sender != NULL
 		? snprintf(data, sizeof (data), CRLF "Return-Path: <%s>" CRLF, ctx->sender->address.string)
 		: 0
 	;

	method = luaL_optstring(L, 3, "CHECK");
	sd->buffer.length = snprintf(
		sd->buffer.data, sd->buffer.size,
		"%s SPAMC/1.2" CRLF "Content-Length: %ld" CRLF,
		method, (long) sb.st_size+length
	);
	sd->replace_msg = (TextInsensitiveCompare(method, "PROCESS") == 0);

	if ((user = luaL_optstring(L, 4, NULL)) != NULL) {
		sd->buffer.length += snprintf(
			sd->buffer.data+sd->buffer.length, sd->buffer.size-sd->buffer.length,
			"User: %s" CRLF, user
		);
	}

	sd->buffer.length += TextCopy(
		sd->buffer.data+sd->buffer.length, sd->buffer.size-sd->buffer.length,
		data
	);

	if (0 < verb_spamd.value)
		syslog(LOG_DEBUG, LOG_FMT "spamd >> %ld:%s", LOG_TRAN(ctx), sd->buffer.length, sd->buffer.data);

	eventSetType(&svc->event, EVENT_WRITE);

	/* Send the command with simulated Return-Path header. */
	if (socket3_write(svc->socket, (unsigned char *) sd->buffer.data, sd->buffer.length, NULL) != sd->buffer.length) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error3;
	}

	lua_pushboolean(L, 1);

	return 1;
error3:
	eventDoIo(EVENT_NAME(service_close), ctx->client.loop, &svc->event, 0);
error2:
	VectorDestroy(host_list);
error1:
	spamd_free(svc);
error0:
	lua_pushboolean(L, 0);

	return 1;
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

	if (SMTP_IS_ERROR(ctx->mx.read.smtp_rc))
		PT_EXIT(&ctx->mx.read.pt);

	PT_BEGIN(&ctx->mx.read.pt);

	ctx->mx.read.size = 0;
	ctx->mx.read.length = 0;
	ctx->mx.read.line_no = 0;
	ctx->mx.read.line_max = 0;
	ctx->mx.read.smtp_rc = 0;
	free(ctx->mx.read.lines);
	ctx->mx.read.lines = NULL;

	do {
		do {
			/* Enlarge table/buffer? */
			if (ctx->mx.read.line_max <= ctx->mx.read.line_no
			|| ctx->mx.read.size <= ctx->mx.read.length + SMTP_REPLY_LINE_LENGTH) {
				if ((table = realloc(ctx->mx.read.lines, sizeof (char *) * (ctx->mx.read.line_max + 11) + ctx->mx.read.size + SMTP_REPLY_LINE_LENGTH)) == NULL)
					goto error1;

				table[0] = 0;
				ctx->mx.read.lines = table;
				memmove(&table[ctx->mx.read.line_max + 11], &table[ctx->mx.read.line_max + 1], ctx->mx.read.length);

				ctx->mx.read.line_max += 10;
				ctx->mx.read.size += SMTP_REPLY_LINE_LENGTH;
			}

			/* Wait for input ready. */
			PT_YIELD(&ctx->mx.read.pt);

			/* Read input. */
			buffer = (char *) &ctx->mx.read.lines[ctx->mx.read.line_max + 1];
			length = socket3_read(
				ctx->mx.socket,
				(unsigned char *)buffer+ctx->mx.read.length,
				ctx->mx.read.size-ctx->mx.read.length,
				NULL
			);

			switch (length) {
			case SOCKET_EOF:
				ctx->mx.read.smtp_rc = SMTP_ERROR_EOF;
				goto error1;
			case SOCKET_ERROR:
				ctx->mx.read.smtp_rc = SMTP_ERROR;
				goto error1;
			}

			ctx->mx.read.length += length;

			/* Wait for a complete line. */
		} while (buffer[ctx->mx.read.length-1] != '\n');

		offset = (size_t) ctx->mx.read.lines[ctx->mx.read.line_no];

		/* Identify start of each line in the chunk read. */
		do {
			length = strcspn(buffer+offset, CRLF);
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
			ctx->mx.read.lines[ctx->mx.read.line_no++] = (char *) offset;
			offset += length;
		} while (offset < ctx->mx.read.length);
	} while (ch == '-');

	/* Add in the base of the buffer to each line's offset. */
	for (ch = 0; ch < ctx->mx.read.line_no; ch++) {
		ctx->mx.read.lines[ch] = buffer + (int) ctx->mx.read.lines[ch];
	}
	ctx->mx.read.lines[ch] = NULL;

	if (0 < ctx->mx.read.line_no) {
		ctx->mx.read.smtp_rc = strtol(ctx->mx.read.lines[0], NULL, 10);
		PT_EXIT(&ctx->mx.read.pt);
	}
error1:
	free(ctx->mx.read.lines);
	ctx->mx.read.lines = NULL;

	PT_END(&ctx->mx.read.pt);
}

static void
mx_close(SmtpCtx *ctx)
{
	TRACE_CTX(ctx, 000);

	eventSetEnabled(&ctx->client.event, ctx->client.enabled);
	eventRemove(ctx->client.loop, &ctx->mx.event);
	socket3_close(ctx->mx.socket);
	free(ctx->mx.read.lines);
}

EVENT_DEF(mx_io)
{
	Event *event = eventGetBase(_ev);
	JmpCode jc;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	SETJMP_PUSH(&ctx->on_error);
	if ((jc = SIGSETJMP(ctx->on_error, 1)) != JMP_SET) {
		mx_close(ctx);
	} else {
		eventResetTimeout(event);
		(*ctx->state)(loop, &ctx->client.event);
	}
	SETJMP_POP(&ctx->on_error);
	sigsetjmp_action(ctx, jc);
}

static int
mx_open(SmtpCtx *ctx, const char *host)
{
	if ((ctx->mx.socket = socket3_connect(host, SMTP_PORT, opt_smtp_accept_timeout.value * UNIT_MILLI)) < 0) {
		syslog(LOG_ERR, LOG_FMT "%s: %s (%d)", LOG_TRAN(ctx), host, strerror(errno), errno);
		return -1;
	}

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT ">> connected %s", LOG_TRAN(ctx), host);

	(void) fileSetCloseOnExec(ctx->mx.socket, 1);
	(void) socket3_set_nonblocking(ctx->mx.socket, 1);
	(void) socket3_set_linger(ctx->mx.socket, 0);

	/* Create an event for the forward host. */
	eventInit(&ctx->mx.event, ctx->mx.socket, EVENT_READ);

	ctx->mx.event.data = ctx;
	eventSetCbIo(&ctx->mx.event, EVENT_NAME(mx_io));
	eventSetTimeout(&ctx->mx.event, opt_smtp_command_timeout.value);

	if (eventAdd(ctx->client.loop, &ctx->mx.event)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		socket3_close(ctx->mx.socket);
		return -1;
	}

	/* Disable the client event until the forwarding completes. */
	ctx->client.enabled = eventGetEnabled(&ctx->client.event);
	eventSetEnabled(&ctx->client.event, 0);
	ctx->mx.read.lines = NULL;

	return 0;
}

static long
mx_print(SmtpCtx *ctx, const char *line, size_t length)
{
	long sent;

	/* Have we already had an error? */
	if (SMTP_IS_ERROR(ctx->mx.read.smtp_rc))
		return ctx->mx.read.smtp_rc;

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT ">> %lu:%s", LOG_TRAN(ctx), (unsigned long) length, line);

	if ((sent = socket3_write(ctx->mx.socket, (unsigned char *) line, length, NULL)) < 0) {
		syslog(LOG_ERR, LOG_FMT "%s: %s (%d)", LOG_TRAN(ctx), ctx->mx.host, strerror(errno), errno);
		ctx->mx.read.smtp_rc = SMTP_ERROR;
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

static
PT_THREAD(mx_send(SmtpCtx *ctx, Vector hosts, const char *mail, Vector rcpts, const char *spool_msg, size_t length))
{
	int err;
	FILE *fp;
	char **host;

	PT_BEGIN(&ctx->mx.pt);

	ctx->reply.length = 0;
	ctx->mx.read.smtp_rc = SMTP_TRY_AGAIN_LATER;

	if (hosts == NULL || VectorLength(hosts) <= 0)
		goto mx_tempfail1;

	for (host = (char **) VectorBase(hosts); *host != NULL; host++) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT ">> trying %s", LOG_TRAN(ctx), *host);

		if (mx_open(ctx, *host) == 0)
			break;
	}
	if (*host == NULL) {
		syslog(LOG_ERR, LOG_FMT "%s", LOG_TRAN(ctx), "all mail host(s) failed");
		goto mx_tempfail1;
	}

	ctx->mx.host = *host;
	ctx->mx.mail = mail;
	ctx->mx.rcpts = rcpts;
	ctx->mx.rcpts_ok = 0;
	ctx->mx.spool = spool_msg;
	ctx->mx.length = length;

	PT_SPAWN(&ctx->mx.pt, &ctx->mx.read.pt, mx_read(ctx));
	if (ctx->mx.read.smtp_rc != SMTP_WELCOME) {
		syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), ctx->mx.host, ctx->mx.read.lines[0]);
		VectorDestroy(ctx->mx.rcpts);
		ctx->mx.rcpts = NULL;
		goto mx_tempfail2;
	}
//	free(ctx->mx.read.lines);

	if (0 < ctx->auth.length) {
		mx_print(ctx, ctx->auth.data, ctx->auth.length);
		PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
		if (ctx->mx.read.smtp_rc != SMTP_AUTH_OK) {
			syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), ctx->mx.host, ctx->mx.read.lines[0]);
			VectorDestroy(ctx->mx.rcpts);
			ctx->mx.rcpts = NULL;
			goto mx_tempfail2;
		}
//		free(ctx->mx.read.lines);
	}

	mx_printf(ctx, "EHLO %s" CRLF, my_host_name);
	PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
//	free(ctx->mx.read.lines);
	if (ctx->mx.read.smtp_rc != SMTP_OK) {
		mx_printf(ctx, "HELO %s" CRLF, my_host_name);
		PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
		if (ctx->mx.read.smtp_rc != SMTP_OK) {
			syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), ctx->mx.host, ctx->mx.read.lines[0]);
			VectorDestroy(ctx->mx.rcpts);
			ctx->mx.rcpts = NULL;
			goto mx_tempfail2;
		}
//		free(ctx->mx.read.lines);
	}

	mx_printf(ctx, "MAIL FROM:<%s>" CRLF, mail);
	PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
	if (ctx->mx.read.smtp_rc != SMTP_OK) {
		syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), ctx->mx.host, ctx->mx.read.lines[0]);
		VectorDestroy(ctx->mx.rcpts);
		ctx->mx.rcpts = NULL;
		goto mx_tempfail2;
	}
//	free(ctx->mx.read.lines);

	for (ctx->rcpt = (char **) VectorBase(rcpts); *ctx->rcpt != NULL; ctx->rcpt++) {
		mx_printf(ctx, "RCPT TO:<%s>" CRLF, *ctx->rcpt);
		PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
		if (ctx->mx.read.smtp_rc != SMTP_OK)
			syslog(LOG_ERR, LOG_FMT "%s: %s: %s", LOG_TRAN(ctx), ctx->mx.host, *ctx->rcpt, ctx->mx.read.lines[0]);

		free(*ctx->rcpt);
		*ctx->rcpt = strdup(ctx->mx.read.lines[0]);

//		free(ctx->mx.read.lines);
		if (ctx->mx.read.smtp_rc == SMTP_OK)
			ctx->mx.rcpts_ok++;
	}

	if (spool_msg == NULL)
		goto mx_tempfail2;

	mx_print(ctx, "DATA" CRLF, STRLEN("DATA" CRLF));
	PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
	if (ctx->mx.read.smtp_rc != SMTP_WAITING) {
		syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), ctx->mx.host, ctx->mx.read.lines[0]);
		goto mx_tempfail2;
	}
//	free(ctx->mx.read.lines);

	if (0 < length) {
		/* Send message string. */
		mx_print(ctx, spool_msg, length);
	} else {
		/* Send message / spool file. */
		if ((fp = fopen(spool_msg, "rb")) == NULL) {
			syslog(LOG_ERR, LOG_FMT "%s %s: %s (%d)", LOG_ID(ctx), ctx->id_trans, spool_msg, strerror(errno), errno);
			ctx->mx.read.smtp_rc = SMTP_ERROR_IO;
			goto mx_tempfail2;
		}

		while ((ctx->work.length = fread(ctx->work.data, 1, ctx->work.size, fp)) != 0) {
			mx_print(ctx, ctx->work.data, ctx->work.length);
		}

		err = ferror(fp);
		(void) fclose(fp);

		if (err) {
			syslog(LOG_ERR, LOG_FMT "%s %s: %s (%d)", LOG_ID(ctx), ctx->id_trans, spool_msg, strerror(errno), errno);
			goto mx_abort;
		}
	}

	eventSetTimeout(&ctx->mx.event, opt_smtp_dot_timeout.value);
	mx_print(ctx, "." CRLF, STRLEN("." CRLF));
	PT_WAIT_THREAD(&ctx->mx.pt, mx_read(ctx));
	if (ctx->mx.read.smtp_rc != SMTP_OK) {
		syslog(LOG_ERR, LOG_FMT "%s: %s", LOG_TRAN(ctx), ctx->mx.host, ctx->mx.read.lines[0]);
	}
	eventSetTimeout(&ctx->mx.event, opt_smtp_command_timeout.value);
mx_tempfail2:
	mx_print(ctx, "QUIT" CRLF, STRLEN("QUIT" CRLF));
mx_abort:
//	free(ctx->mx.read.lines);
	mx_close(ctx);
mx_tempfail1:

	PT_END(&ctx->mx.pt);
}

static int
lua_mx_senduntil(lua_State *L, SmtpCtx *ctx)
{
	return !PT_SCHEDULE(mx_send(ctx, NULL, ctx->mx.mail, ctx->mx.rcpts, ctx->mx.spool, ctx->mx.length));
}

static int
lua_mx_sendresult(lua_State *L, SmtpCtx *ctx)
{
	lua_pushinteger(L, SMTP_IS_OK(ctx->mx.read.smtp_rc));
	lua_pushinteger(L, ctx->mx.rcpts_ok);
	lua_vector_to_array(L, ctx->mx.rcpts);
	VectorDestroy(ctx->mx.rcpts);

	return 3;
}

static int
lua_mx_send(lua_State *L, size_t string_length)
{
	int is_scheduled;
	Vector hosts, rcpts;
	SmtpCtx *ctx = lua_smtp_ctx(L);

	if (ctx == NULL)
		return luaL_error(L, LOG_FN_FMT "client context not found ", LOG_FN(ctx));

	luaL_checktype(L, 1, LUA_TTABLE);
	if ((hosts = lua_array_to_vector(L, 1)) == NULL)
		return luaL_error(L, LOG_FN_FMT "hosts table error: %s (%d)", LOG_FN(ctx), strerror(errno), errno);
	if (VectorLength(hosts) <= 0)
		return luaL_error(L, LOG_FN_FMT "hosts table cannot be empty", LOG_FN(ctx));

	luaL_checktype(L, 3, LUA_TTABLE);
	if ((rcpts = lua_array_to_vector(L, 3)) == NULL)
		return luaL_error(L, LOG_FN_FMT "rcpts table error: %s (%d)", LOG_FN(ctx), strerror(errno), errno);
	if (VectorLength(rcpts) <= 0)
		return luaL_error(L, LOG_FN_FMT "rcpts table cannot be empty", LOG_FN(ctx));

	ctx->lua.yield_until = lua_mx_senduntil;
	ctx->lua.yield_after = lua_mx_sendresult;

	is_scheduled = PT_SCHEDULE(
		mx_send(ctx, hosts, luaL_checkstring(L, 2), rcpts, luaL_optstring(L, 4, NULL), string_length)
	);

	VectorDestroy(hosts);

	if (!is_scheduled) {
		VectorDestroy(rcpts);
		return luaL_error(L, LOG_FN_FMT "%s error", LOG_FN(ctx));
	}

	return lua_yield(L, 0);
}

/**
 * smtp.code, n_rcpt_ok = smtp.sendfile(host, mail, rcpts, filepath)
 */
static int
lua_mx_sendfile(lua_State *L)
{
	struct stat sb;
	SmtpCtx *ctx = lua_smtp_ctx(L);
	const char *file = luaL_optstring(L, 4, NULL);

	if (file != NULL && stat(file, &sb) != 0)
		return luaL_error(L, LOG_FN_FMT "%s error: %s (%d)", LOG_FN(ctx), file, strerror(errno), errno);

	return lua_mx_send(L, 0);
}

/**
 * smtp.code, n_rcpt_ok = smtp.sendstring(host, mail, rcpts, message)
 */
static int
lua_mx_sendstring(lua_State *L)
{
	return lua_mx_send(L, lua_objlen(L, -1));
}

static struct map_integer smtp_code_constants[] = {
	/*
	 * RFC 821, 2821, 5321 Reply Codes.
	 */
	{ "STATUS",		/* 211 */	SMTP_STATUS },
	{ "HELP",		/* 214 */	SMTP_HELP },
	{ "WELCOME",		/* 220 */	SMTP_WELCOME },
	{ "GOODBYE",		/* 221 */	SMTP_GOODBYE },
	{ "AUTH_OK",		/* 235 */	SMTP_AUTH_OK },		/* RFC 4954 section 6 */
	{ "OK",			/* 250 */	SMTP_OK },
	{ "USER_NOT_LOCAL",	/* 251 */	SMTP_USER_NOT_LOCAL },

	{ "WAITING",		/* 354 */	SMTP_WAITING },

	{ "CLOSING",		/* 421 */	SMTP_CLOSING },
	{ "AUTH_MECHANISM",	/* 432 */	SMTP_AUTH_MECHANISM },	/* RFC 4954 section 6 */
	{ "BUSY",		/* 450 */	SMTP_BUSY },
	{ "TRY_AGAIN_LATER",	/* 451 */	SMTP_TRY_AGAIN_LATER },
	{ "NO_STORAGE",		/* 452 */	SMTP_NO_STORAGE },
	{ "AUTH_TEMP",		/* 454 */	SMTP_AUTH_TEMP },	/* RFC 4954 section 6 */

	{ "BAD_SYNTAX",		/* 500 */	SMTP_BAD_SYNTAX },
	{ "BAD_ARGUMENTS",	/* 501 */	SMTP_BAD_ARGUMENTS },
	{ "UNKNOWN_COMMAND",	/* 502 */	SMTP_UNKNOWN_COMMAND },
	{ "BAD_SEQUENCE",	/* 503 */	SMTP_BAD_SEQUENCE },
	{ "UNKNOWN_PARAM",	/* 504 */	SMTP_UNKNOWN_PARAM },
	{ "AUTH_REQUIRED",	/* 530 */	SMTP_AUTH_REQUIRED },	/* RFC 4954 section 6 */
	{ "AUTH_WEAK",		/* 534 */	SMTP_AUTH_WEAK },	/* RFC 4954 section 6 */
	{ "AUTH_FAIL",		/* 535 */	SMTP_AUTH_FAIL },	/* RFC 4954 section 6 */
	{ "AUTH_ENCRYPT",	/* 538 */	SMTP_AUTH_ENCRYPT },	/* RFC 4954 section 6 */
	{ "REJECT",		/* 550 */	SMTP_REJECT },
	{ "UNKNOWN_USER",	/* 551 */	SMTP_UNKNOWN_USER },
	{ "OVER_QUOTA",		/* 552 */	SMTP_OVER_QUOTA },
	{ "BAD_ADDRESS",	/* 553 */	SMTP_BAD_ADDRESS },
	{ "TRANSACTION_FAILED",	/* 554 */	SMTP_TRANSACTION_FAILED },

	/*
	 * Error conditions indicated like SMTP Reply Codes.
	 */
	{ "ERROR",		/* 100 */	SMTP_ERROR },
	{ "ERROR_CONNECT",	/* 110 */	SMTP_ERROR_CONNECT },
	{ "ERROR_TIMEOUT",	/* 120 */	SMTP_ERROR_TIMEOUT },
	{ "ERROR_EOF",		/* 130 */	SMTP_ERROR_EOF },
	{ "ERROR_IO",		/* 140 */	SMTP_ERROR_IO },

	{ "NULL", 0 }
};

static const luaL_Reg lua_mx_package[] = {
	{ "sendfile", 	lua_mx_sendfile },
	{ "sendstring", lua_mx_sendstring },
	{ NULL, NULL },
};

static void
lua_define_smtp(lua_State *L)
{
	struct map_integer *map;

	luaL_register(L, "smtp", lua_mx_package);	/* smtp */

	lua_newtable(L);				/* smtp code */
	for (map = smtp_code_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);

		lua_pushinteger(L, map->value);
		lua_pushstring(L, map->name);
		lua_settable(L, -3);
	}
	lua_setfield(L, -2, "code");			/* smtp */

	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua Syslog API
 ***********************************************************************/

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

	if ((ctx = lua_smtp_ctx(L)) == NULL) {
		/* Master state has no __ctx. */
		syslog(level, "%s", luaL_checkstring(L, 2));
	} else {
		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d L=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua.thread, (long) L);
		syslog(level, LOG_FMT "%s", LOG_TRAN(ctx), luaL_checkstring(L, 2));
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
 *** Lua Header API
 ***********************************************************************/

static long
header_find_name(Vector headers, const char *name, long instance)
{
	long i, length;
	const char *hdr;

	if (headers == NULL || name == NULL || instance < 0)
		return -1;

	for (i = 0; i < headers->_length; i++) {
		hdr = VectorGet(headers, i);
		if (0 < (length = TextInsensitiveStartsWith(hdr, name)) && hdr[length] == ':') {
			if (headers->_length <= i + instance)
				break;

			hdr = VectorGet(headers, i + instance);
			if ((length = TextInsensitiveStartsWith(hdr, name)) < 0 || hdr[length] != ':')
				break;

			return i;
		}
	}

	return -1;
}

/*
 * header.add(header_line)
 */
static int
header_add(lua_State *L)
{
	SmtpCtx *ctx = lua_smtp_ctx(L);
	char *hdr = (char *) luaL_optstring(L, 1, NULL);

	if (hdr != NULL && (hdr = strdup(hdr)) != NULL && VectorAdd(ctx->headers, hdr))
		free(hdr);

	return 0;
}

/*
 * header.insert(header_line, index)
 */
static int
header_insert(lua_State *L)
{
	SmtpCtx *ctx = lua_smtp_ctx(L);
	long index = luaL_optlong(L, 2, 1)-1;
	char *hdr = (char *) luaL_optstring(L, 1, NULL);

	if (hdr != NULL && (hdr = strdup(hdr)) != NULL && VectorInsert(ctx->headers, index, hdr))
		free(hdr);

	return 0;
}

/*
 * header.delete(header_name, instance)
 */
static int
header_delete(lua_State *L)
{
	long index;
	SmtpCtx *ctx = lua_smtp_ctx(L);
	long instance = luaL_optlong(L, 2, 1)-1;
	const char *hdr = luaL_optstring(L, 1, NULL);

	if (0 <= (index = header_find_name(ctx->headers, hdr, instance)))
		VectorRemove(ctx->headers, index);

	return 0;
}

/*
 * header.modify(header_name, instance, new_value)
 */
static int
header_modify(lua_State *L)
{
	long index, stop;
	char *header, *hdr;
	size_t length, size;
	SmtpCtx *ctx = lua_smtp_ctx(L);
	long instance = luaL_optlong(L, 2, 1);
	const char *name = luaL_optstring(L, 1, NULL);
	const char *value = luaL_optstring(L, 3, NULL);

	if (name == NULL || value == NULL || instance < 0)
		return 0;

 	size = (length = strlen(name)) + strlen(value) + 3;
 	if ((header = malloc(size)) == NULL)
 		return 0;
 	(void) snprintf(header, size, "%s: %s", name, value);

	if (0 <= (index = header_find_name(ctx->headers, name, 0))) {
		/* Insert at start of header group? */
		if (instance == 0) {
			if (VectorInsert(ctx->headers, index, header))
				free(header);
			return 0;
		}

		/* Find instance within group. */
		instance--;
		for (stop = index + instance; index < stop; index++) {
			/* End of group reached? */
			if (TextInsensitiveStartsWith(hdr, name) < 0 || hdr[length] != ':')
				break;
		}

		/* Replace existing header in group. */
		if (index == stop)
			VectorSet(ctx->headers, index, header);

		/* Append to group. */
		else if (VectorInsert(ctx->headers, index, header))
			free(header);

		return 0;
	}

	/* No such header. Start new group, append to end of headers. */
	if (VectorAdd(ctx->headers, header))
		free(header);

	return 0;
}

/*
 * index, value = header.find(header_name, instance)
 */
static int
header_find(lua_State *L)
{
	long index;
	SmtpCtx *ctx = lua_smtp_ctx(L);
	long instance = luaL_optlong(L, 2, 1)-1;
	const char *hdr = luaL_optstring(L, 1, NULL), *value;

	if (0 <= (index = header_find_name(ctx->headers, hdr, instance))) {
		value  = VectorGet(ctx->headers, index);
		value += strlen(hdr) + 1;
		value += strspn(value, " \t");

		lua_pushinteger(L, index);
		lua_pushstring(L, value);
		return 2;
	}

	return 0;
}

static const luaL_Reg header_pkg[] = {
	{ "add", 	header_add },
	{ "insert", 	header_insert },
	{ "delete", 	header_delete },
	{ "modify", 	header_modify },
	{ "find",	header_find },
	{ NULL, NULL },
};

static void
lua_define_header(lua_State *L)
{
	luaL_register(L, "header", header_pkg);	/* pkg */
	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua DNS API
 ***********************************************************************/

void
dns_close(Events *loop, Event *event)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	if (ctx->pdq.pdq != NULL) {
		eventSetEnabled(event, ctx->client.enabled);
		pdqListFree(ctx->pdq.answer);
		ctx->pdq.answer = NULL;

		eventRemove(loop, &ctx->pdq.event);
		pdqClose(ctx->pdq.pdq);
		ctx->pdq.pdq = NULL;
	}
}

EVENT_DEF(dns_io)
{
	Event *event = eventGetBase(_ev);
	JmpCode jc;
	Event *client_event = event->data;
	SmtpCtx *ctx = client_event->data;

	TRACE_CTX(ctx, 000);

	SETJMP_PUSH(&ctx->on_error);
	if ((jc = SIGSETJMP(ctx->on_error, 1)) != JMP_SET) {
		dns_close(loop, client_event);
	} else {
		if (errno == ETIMEDOUT) {
			/* Double the timeout for next iteration. */
			ctx->pdq.timeout_sum += ctx->pdq.timeout_next;
			ctx->pdq.timeout_next += ctx->pdq.timeout_next;

			if (verb_dns.value)
				syslog(LOG_DEBUG, LOG_FMT "dns timeout sum=%ld next=%ld",  LOG_ID(ctx), ctx->pdq.timeout_sum, ctx->pdq.timeout_next);

			eventSetTimeout(&ctx->pdq.event, ctx->pdq.timeout_next);
		}
		(*ctx->state)(loop, client_event);
	}
	SETJMP_POP(&ctx->on_error);
	sigsetjmp_action(ctx, jc);
}

#ifdef DNS_MX_5A_HELP
static int
dns_check_answer(PDQ *pdq, PDQ_rr *head, int wait_all)
{
	PDQ_type type;
	PDQ_class class;
	PDQ_rr *rr, *mx0;
	int short_query;

	short_query = pdqGetBasicQuery(pdq);

	for (rr = head; rr != NULL; rr = rr->next) {
		if (rr->section == PDQ_SECTION_QUERY) {
			class = rr->class;
			type = rr->type;
		}

		/* When there is no MX found, apply the implicit MX 0 rule. */
		if (rr->section == PDQ_SECTION_QUERY && type == PDQ_TYPE_MX
		&& pdqListFind(rr->next, PDQ_CLASS_ANY, PDQ_TYPE_MX, NULL) == NULL
		&& (mx0 = pdqCreate(PDQ_TYPE_MX)) != NULL) {
			pdqSetName(&((PDQ_MX *) mx0)->rr.name, rr->name.string.value);
			pdqSetName(&((PDQ_MX *) mx0)->host, rr->name.string.value);

			mx0->section = PDQ_SECTION_ANSWER;
			mx0->type = PDQ_TYPE_MX;
			mx0->class = class;

			/* Insert MX 0 directly after the query. */
			mx0->next = rr->next;
			rr->next = mx0;

			/* Continue for A/AAAA checks below. */
			rr = mx0;
		}

		if (short_query || rr->section == PDQ_SECTION_QUERY
		|| !(type == PDQ_TYPE_MX || type == PDQ_TYPE_NS || type == PDQ_TYPE_SOA))
			continue;

		if (rr->type == type) {
			/* "domain IN MX ." is a short hand to indicate
			 * that a domain has no MX records. No point in
			 * looking up A/AAAA records. Like wise for NS
			 * and SOA records.
			 */
			if (strcmp(".", ((PDQ_MX *) rr)->host.string.value) == 0)
				continue;

			if (pdqListFindName(head, class, PDQ_TYPE_A, ((PDQ_MX *) rr)->host.string.value) == NULL)
				(void) pdqQuery(pdq, class, PDQ_TYPE_A, ((PDQ_MX *) rr)->host.string.value, NULL);
			if (pdqListFindName(head, class, PDQ_TYPE_AAAA, ((PDQ_MX *) rr)->host.string.value) == NULL)
				(void) pdqQuery(pdq, class, PDQ_TYPE_AAAA, ((PDQ_MX *) rr)->host.string.value, NULL);

			wait_all = 1;
		}
	}

	return wait_all;
}
#endif

int
dns_wait(SmtpCtx *ctx, int wait_all)
{
	PDQ_rr *head;

	errno = 0;

	TRACE_CTX(ctx, 000);

	if (pdqQueryIsPending(ctx->pdq.pdq)) {
		if ((head = pdqPoll(ctx->pdq.pdq, 10)) != NULL) {
			if (head->section == PDQ_SECTION_QUERY && ((PDQ_QUERY *) head)->rcode == PDQ_RCODE_TIMEDOUT) {
				if (verb_debug.value)
					pdqListLog(head);
				pdqListFree(head);
			} else {
#ifdef DNS_MX_5A_HELP
				ctx->pdq.wait_all = dns_check_answer(ctx->pdq.pdq, head, wait_all);
#endif
				ctx->pdq.answer = pdqListAppend(ctx->pdq.answer, head);
			}
		}
		if (pdqQueryIsPending(ctx->pdq.pdq)
		&& ctx->pdq.timeout_sum < pdqGetTimeout(ctx->pdq.pdq)
		&& (wait_all || ctx->pdq.answer == NULL))
			return errno = EAGAIN;
	}

	eventSetEnabled(&ctx->pdq.event, 0);

	return errno;
}

void
dns_reset(SmtpCtx *ctx)
{
	ctx->pdq.timeout_sum = 0;
	ctx->pdq.timeout_next = PDQ_TIMEOUT_START;
	eventSetTimeout(&ctx->pdq.event, ctx->pdq.timeout_next);
	pdqListFree(ctx->pdq.answer);
	ctx->pdq.answer = NULL;
}

int
dns_open(Events *loop, Event *event)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	if (ctx->pdq.pdq != NULL)
		return 0;

	if ((ctx->pdq.pdq = pdqOpen()) == NULL) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error0;
	}

	/* Create an event for the DNS lookup. */
	eventInit(&ctx->pdq.event, pdqGetFd(ctx->pdq.pdq), EVENT_READ);

	/* Disable the event until dns_wait() is explicity called
	 * otherwise we get timeouts errors.
	 */
	eventSetEnabled(&ctx->pdq.event, 0);

	ctx->pdq.event.data = event;
	eventSetCbIo(&ctx->pdq.event, EVENT_NAME(dns_io));

	if (eventAdd(loop, &ctx->pdq.event)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		goto error1;
	}

	/* Disable the client event until the DNS lookup completes. */
	ctx->client.enabled = eventGetEnabled(event);
	eventSetEnabled(event, opt_test.value);
	ctx->pdq.answer = NULL;
	dns_reset(ctx);

	return 0;
error1:
	pdqClose(ctx->pdq.pdq);
	ctx->pdq.pdq = NULL;
error0:
	return -1;
}

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
 * dns.ispending()
 */
static int
lua_dns_ispending(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_smtp_ctx(L)) != NULL) {
		lua_pushboolean(L, pdqQueryIsPending(ctx->pdq.pdq));
		return 1;
	}

	return 0;
}

static long
lua_string_to_buffer(lua_State *L, int index, const char *field, char *buffer, size_t size)
{
	size_t length;
	const char *value;

	lua_getfield(L, index, field);
	value = lua_tolstring(L, -1, &length);
	lua_pop(L, 1);

	if (value == NULL || size <= length)
		return -1;

	(void) TextCopy(buffer, size, value);

	return (long) length;
}

static PDQ_rr *
lua_rr_to_pdq(lua_State *L, int table_index)
{
	PDQ_rr *rr;
	PDQ_type type;

	lua_getfield(L, table_index, "type");
	type = lua_tointeger(L, -1);
	lua_pop(L, 1);

	if ((rr = pdqCreate(type)) == NULL)
		goto error0;

	lua_getfield(L, table_index, "section");	/* s */
	rr->section = lua_tointeger(L, -1);
	lua_getfield(L, table_index, "class");		/* s c */
	rr->class = lua_tointeger(L, -1);
	lua_getfield(L, table_index, "ttl");		/* s c t */
	rr->ttl = lua_tointeger(L, -1);
	lua_pop(L, 3);					/* -- */

	rr->name.string.length = (unsigned short) lua_string_to_buffer(L, -1, "name", rr->name.string.value, sizeof (rr->name.string.value));
	if (rr->name.string.length == 0 || sizeof (rr->name.string.value) <= rr->name.string.length)
		goto error1;

	switch (type) {
	case PDQ_TYPE_A:
		((PDQ_A *) rr)->address.ip.offset = IPV6_OFFSET_IPV4;
		/*@fallthrough@*/

	case PDQ_TYPE_AAAA:
		((PDQ_A *)rr)->address.string.length = lua_string_to_buffer(L, -1, "value", ((PDQ_A *)rr)->address.string.value, sizeof (((PDQ_A *)rr)->address.string.value));
		if (((PDQ_A *)rr)->address.string.length == 0 || sizeof (((PDQ_A *)rr)->address.string.value) <= ((PDQ_A *)rr)->address.string.length)
			goto error1;

		if (parseIPv6(((PDQ_A *)rr)->address.string.value, ((PDQ_A *) rr)->address.ip.value) <= 0)
			goto error1;
		break;

	case PDQ_TYPE_MX:
		lua_getfield(L, table_index, "preference");	/* p */
		((PDQ_MX *)rr)->preference = lua_tointeger(L, -1);
		lua_pop(L, 1);
		/*@fallthrough@*/

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		((PDQ_NS *)rr)->host.string.length = lua_string_to_buffer(L, -1, "value", ((PDQ_NS *)rr)->host.string.value, sizeof (((PDQ_NS *)rr)->host.string.value));
		if (((PDQ_NS *)rr)->host.string.length == 0 || sizeof (((PDQ_NS *)rr)->host.string.value) <= ((PDQ_NS *)rr)->host.string.length)
			goto error1;
		break;

	case PDQ_TYPE_TXT:
	case PDQ_TYPE_NULL:
		((PDQ_TXT *)rr)->text.length = lua_string_to_buffer(L, -1, "value", (char *)((PDQ_TXT *)rr)->text.value, sizeof (((PDQ_TXT *)rr)->text.value));
		if (((PDQ_TXT *)rr)->text.length == 0 || sizeof (((PDQ_TXT *)rr)->text.value) <= ((PDQ_TXT *)rr)->text.length)
			goto error1;
		break;

	case PDQ_TYPE_SOA:
		((PDQ_SOA *)rr)->mname.string.length = lua_string_to_buffer(L, -1, "value", ((PDQ_SOA *)rr)->mname.string.value, sizeof (((PDQ_SOA *)rr)->mname.string.value));
		if (((PDQ_SOA *)rr)->mname.string.length == 0 || sizeof (((PDQ_SOA *)rr)->mname.string.value) <= ((PDQ_SOA *)rr)->mname.string.length)
			goto error1;

		((PDQ_SOA *)rr)->rname.string.length = lua_string_to_buffer(L, -1, "value", ((PDQ_SOA *)rr)->rname.string.value, sizeof (((PDQ_SOA *)rr)->rname.string.value));
		if (((PDQ_SOA *)rr)->rname.string.length == 0 || sizeof (((PDQ_SOA *)rr)->rname.string.value) <= ((PDQ_SOA *)rr)->rname.string.length)
			goto error1;

		lua_getfield(L, table_index, "serial");
		((PDQ_SOA *)rr)->serial = lua_tointeger(L, -1);
		lua_getfield(L, table_index, "refresh");
		((PDQ_SOA *)rr)->refresh = lua_tointeger(L, -1);
		lua_getfield(L, table_index, "retry");
		((PDQ_SOA *)rr)->retry = lua_tointeger(L, -1);
		lua_getfield(L, table_index, "expire");
		((PDQ_SOA *)rr)->expire = lua_tointeger(L, -1);
		lua_getfield(L, table_index, "minimum");
		((PDQ_SOA *)rr)->minimum = lua_tointeger(L, -1);
		lua_pop(L, 5);
		break;
	default:
		goto error1;
	}

	return rr;
error1:
	free(rr);
error0:
	return NULL;
}

static int
lua_pdq_to_rr(lua_State *L, PDQ_rr *rr)
{
	lua_newtable(L);			/* rr */

	/* Common RR fields. */
	lua_pushlstring(L, rr->name.string.value, rr->name.string.length);
	lua_setfield(L, -2, "name");
	lua_pushinteger(L, rr->class);
	lua_setfield(L, -2, "class");
	lua_pushinteger(L, rr->type);
	lua_setfield(L, -2, "type");

	if (rr->section == PDQ_SECTION_QUERY) {
		lua_pushinteger(L, ((PDQ_QUERY *)rr)->rcode);
		lua_setfield(L, -2, "rcode");

		/* Index queries. */
		lua_pushvalue(L, -1);		/* qy qy */
		lua_array_push(L, -3);		/* qy */

		/* Also save queries by key in same table. */
		(void) lua_pushfstring(
			L, "%s,%s,%s",
			pdqClassName(rr->class),
			pdqTypeName(rr->type),
			rr->name.string.value
		);				/* qy key */
		lua_pushvalue(L, -2);		/* qy key qy */
		lua_settable(L, -4);		/* qy */

		if (rr->next != NULL && rr->next->section != PDQ_SECTION_QUERY) {
			lua_newtable(L);	/* qy extra */
			lua_newtable(L);	/* qy extra authority */
			lua_newtable(L);	/* qy extra authority answer */
			return 4;
		}

		lua_pop(L, 1);			/* */
		return 0;
	}

	lua_pushinteger(L, rr->ttl);
	lua_setfield(L, -2, "ttl");

	switch (rr->type) {			/* qy extra authority answer rr */
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
	case PDQ_TYPE_NULL:
		lua_pushlstring(L, (char *)((PDQ_TXT *) rr)->text.value, ((PDQ_TXT *) rr)->text.length);
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

	return 1;
}

/**
 * boolean = dns.isequal(rr1, rr2)
 */
static int
lua_dns_isequal(lua_State *L)
{
	SmtpCtx *ctx;
	PDQ_rr *r1, *r2;

	if ((ctx = lua_smtp_ctx(L)) == NULL)
		return 0;

	if (lua_gettop(L) != 2) {
		lua_pushboolean(L, 0);
		return 1;
	}

	if (lua_equal(L, 1, 2)) {
		lua_pushboolean(L, 1);
		return 1;
	}

	r1 = lua_rr_to_pdq(L, 1);
	r2 = lua_rr_to_pdq(L, 2);

	lua_pushboolean(L, r1 != NULL && r2 != NULL && pdqEqual(r1, r2));

	free(r2);
	free(r1);

	return 1;
}

/**
 * dns.open()
 */
static int
lua_dns_open(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_smtp_ctx(L)) != NULL)
		(void) dns_open(ctx->client.loop, &ctx->client.event);

	return 0;
}

/**
 * dns.close()
 */
static int
lua_dns_close(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_smtp_ctx(L)) != NULL)
		(void) dns_close(ctx->client.loop, &ctx->client.event);

	return 0;
}

/**
 * dns.reset()
 */
static int
lua_dns_reset(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_smtp_ctx(L)) != NULL) {
		pdqQueryRemoveAll(ctx->pdq.pdq);
		dns_reset(ctx);
	}

	return 0;
}

/**
 * dns.query(class, type, name)
 */
static int
lua_dns_query(lua_State *L)
{
	SmtpCtx *ctx;
	const char *name;

	if ((ctx = lua_smtp_ctx(L)) == NULL)
		return 0;

	if ((name = luaL_optstring(L, 3, NULL)) == NULL || *name == '\0')
		return 0;

	if (pdqQuery(ctx->pdq.pdq, luaL_optint(L, 1, PDQ_CLASS_IN), luaL_optint(L, 2, PDQ_TYPE_ANY), name, NULL)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
	}

	return 0;
}

static int
lua_dns_waituntil(lua_State *L, SmtpCtx *ctx)
{
	return dns_wait(ctx, ctx->pdq.wait_all) != EAGAIN;
}

static int
lua_dns_getresult(lua_State *L, PDQ_rr *rr)
{
	lua_newtable(L);				/* answers */
	if (rr == NULL)
		return 1;

	for ( ; rr != NULL; rr = rr->next) {
		if (lua_pdq_to_rr(L, rr) != 1)
			continue;
								/* answers qy extra authority answer rr */
		/* See PDQ_SECTION_ indices for why this works. */
		lua_array_push(L, -1 -rr->section); 		/* answers qy extra authority answer */

		if (rr->next == NULL || rr->next->section == PDQ_SECTION_QUERY) {
			lua_setfield(L, -4, "answer");		/* answers qr extra authority */
			lua_setfield(L, -3, "authority");	/* answers qr extra */
			lua_setfield(L, -2, "extra");		/* answers qr */
			lua_pop(L, 1);				/* answers */
		}
	}

	return 1;
}

static int
lua_dns_yieldafter(lua_State *L, SmtpCtx *ctx)
{
	return lua_dns_getresult(L, ctx->pdq.answer);
}

/**
 * table = dns.wait(all_flag)
 */
static int
lua_dns_wait(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_smtp_ctx(L)) != NULL) {
		eventSetEnabled(&ctx->pdq.event, 1);
		ctx->pdq.wait_all = luaL_optint(L, 1, 1);
		ctx->lua.yield_until = lua_dns_waituntil;
		ctx->lua.yield_after = lua_dns_yieldafter;
	}

	return lua_yield(L, 0);
}

/**
 * table = dns.poll(all_flag)
 */
static int
lua_dns_poll(lua_State *L)
{
	SmtpCtx *ctx;

	if ((ctx = lua_smtp_ctx(L)) != NULL) {
		ctx->pdq.wait_all = luaL_optint(L, 1, 1);
		while (dns_wait(ctx, ctx->pdq.wait_all) == EAGAIN)
			;
		return lua_dns_getresult(L, ctx->pdq.answer);
	}

	return 0;
}

static const luaL_Reg lua_dns_package[] = {
	{ "open", 	lua_dns_open },
	{ "reset", 	lua_dns_reset },
	{ "query", 	lua_dns_query },
	{ "wait", 	lua_dns_wait },
	{ "poll",	lua_dns_poll },
	{ "close", 	lua_dns_close },
	{ "classname",	lua_dns_classname },
	{ "typename",	lua_dns_typename },
	{ "rcodename",	lua_dns_rcodename },
	{ "ispending",	lua_dns_ispending },
	{ "isequal",	lua_dns_isequal },
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

/***********************************************************************
 *** Lua Network API
 ***********************************************************************/

static struct map_integer is_ip_constants[] = {
	{ "BENCHMARK",		IS_IP_BENCHMARK },	/* 198.18.0.0/15   RFC 2544       */
	{ "LINK_LOCAL",		IS_IP_LINK_LOCAL },	/* 169.254.0.0/16  link local (private use) */
	{ "LOCALHOST",		IS_IP_LOCALHOST },	/* 127.0.0.1/32    localhost      */
	{ "LOOPBACK",		IS_IP_LOOPBACK },	/* 127.0.0.0/8     loopback, excluding 127.0.0.1 */
	{ "MULTICAST",		IS_IP_MULTICAST },	/* 224.0.0.0/4     RFC 3171       */
	{ "PRIVATE_A",		IS_IP_PRIVATE_A },	/* 10.0.0.0/8      private use    */
	{ "PRIVATE_B",		IS_IP_PRIVATE_B },	/* 172.16.0.0/12   private use    */
	{ "PRIVATE_C",		IS_IP_PRIVATE_C },	/* 192.168.0.0/16  private use    */
	{ "RESERVED",		IS_IP_RESERVED },	/* IPv6 reserved prefix (not global unicast) */
	{ "SITE_LOCAL",		IS_IP_SITE_LOCAL }, 	/* IPv6 site local autoconfiguration */
	{ "TEST_NET",		IS_IP_TEST_NET },	/* 192.0.2.0/24    test network   */
	{ "THIS_HOST",		IS_IP_THIS_HOST },	/* 0.0.0.0/32      "this" host */
	{ "THIS_NET",		IS_IP_THIS_NET },	/* 0.0.0.0/8       "this" network */
	{ "V4_COMPATIBLE",	IS_IP_V4_COMPATIBLE },  /* is this an IPv4-compatible IPv6 address */
	{ "V4_MAPPED",		IS_IP_V4_MAPPED },  	/* is this an IPv4-mapped IPv6 address */
	{ "V6",			IS_IP_V6 },
	{ "V4",			IS_IP_V4 },
	{ "ANY",		IS_IP_ANY },
	{ "TEST",		IS_IP_TEST },
	{ "LOCAL",		IS_IP_LOCAL },
	{ "LAN",		IS_IP_LAN },
	{ "RESTICTED",		IS_IP_RESTRICTED },
	{ NULL,			0 }
};

/**
 * boolean = net.is_ip_reserved(address, is_ip_flags)
 */
static int
lua_net_is_ip_reserved(lua_State *L)
{
	lua_pushboolean(L, isReservedIP( luaL_optstring(L, 1, NULL), luaL_optint(L, 2, 0)));

	return 1;
}

/**
 * string = net.reverse_ip(address, suffix_flag)
 */
static int
lua_net_reverse_ip(lua_State *L)
{
	long length;
	char buffer[DOMAIN_STRING_LENGTH];

	length = reverseIp(luaL_checkstring(L, 1), buffer, sizeof (buffer), luaL_checkint(L, 2));
	lua_pushlstring(L, buffer, length);

	return 1;
}

/**
 * boolean = net.contains_ip(net_cidr_string, ip_string)
 */
static int
lua_net_contains_ip(lua_State *L)
{
	lua_pushboolean(
		L, networkContainsIP(
			luaL_optstring(L, 1, "::0/0"),
			luaL_optstring(L, 2, "::0")
		)
	);

	return 1;
}

/**
 * boolean = net.has_valid_tld(string)
 */
static int
lua_net_has_valid_tld(lua_State *L)
{
	lua_pushboolean(L, hasValidTLD(luaL_optstring(L, 1, NULL)));

	return 1;
}

/**
 * boolean = net.has_valid_nth_tld(string, level)
 */
static int
lua_net_has_valid_nth_tld(lua_State *L)
{
	lua_pushboolean(L, hasValidNthTLD(luaL_optstring(L, 1, NULL), luaL_optint(L, 2, 1)));

	return 1;
}

/**
 * offset = net.index_valid_tld(string)
 */
static int
lua_net_index_valid_tld(lua_State *L)
{
	lua_pushinteger(L, indexValidTLD(luaL_optstring(L, 1, NULL))+1);

	return 1;
}

/**
 * offset = net.index_valid_nth_tld(string, level)
 */
static int
lua_net_index_valid_nth_tld(lua_State *L)
{
	lua_pushinteger(L, indexValidNthTLD(luaL_optstring(L, 1, NULL), luaL_optint(L, 2, 1))+1);

	return 1;
}

/**
 * integer = net.is_ipv4_in_name(ipv4, string)
 */
static int
lua_net_is_ipv4_in_name(lua_State *L)
{
	const char *name, *address;
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	address = luaL_optstring(L, 1, "");
	name = luaL_optstring(L, 2, "");

	if (parseIPv6(address, ipv6) <= 0)
		lua_pushnil(L);
	else
		lua_pushinteger(L, isIPv4InName(name, ipv6+IPV6_OFFSET_IPV4, NULL, NULL));

	return 1;
}

/**
 * offset, span = net.find_ip(string)
 */
static int
lua_net_find_ip(lua_State *L)
{
	int offset, span;

	if (findIP(luaL_optstring(L, 1, ""), &offset, &span) != NULL) {
		lua_pushinteger(L, offset);
		lua_pushinteger(L, span);
		return 2;
	}

	return 0;
}

/**
 * string = net.format_ip(string, compact)
 */
static int
lua_net_format_ip(lua_State *L)
{
	const char *string;
	int compact, length;
	char address[IPV6_STRING_LENGTH];
	unsigned char ipv6[IPV6_BYTE_LENGTH];

 	if ((string = luaL_optstring(L, 1, NULL)) == NULL || parseIPv6(string, ipv6) <= 0) {
 		lua_pushstring(L, "");
		return 1;
	}

	compact = luaL_optint(L, 2, 0);
	length = (isReservedIPv6(ipv6, IS_IP_V4) ? IPV4_BYTE_LENGTH : IPV6_BYTE_LENGTH);
	length = formatIP(ipv6, length, compact, address, sizeof (address));
	lua_pushlstring(L, address, length);

	return 1;
}

static const luaL_Reg lua_net_package[] = {
	{ "reverse_ip",			lua_net_reverse_ip },
	{ "contains_ip", 		lua_net_contains_ip },
	{ "find_ip",			lua_net_find_ip },
	{ "has_valid_tld",		lua_net_has_valid_tld },
	{ "has_valid_nth_tld",		lua_net_has_valid_nth_tld },
	{ "index_valid_tld",		lua_net_index_valid_tld },
	{ "index_valid_nth_tld",	lua_net_index_valid_nth_tld },
	{ "is_ipv4_in_name",		lua_net_is_ipv4_in_name },
	{ "is_ip_reserved",		lua_net_is_ip_reserved },
	{ "format_ip",			lua_net_format_ip },
	{ NULL, NULL },
};

static void
lua_define_net(lua_State *L)
{
	struct map_integer *map;

	luaL_register(L, "net", lua_net_package);	/* pkg */

	lua_newtable(L);				/* pkg is_ip */
	for (map = is_ip_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);
	}
	lua_setfield(L, -2, "is_ip");			/* pkg */

	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua MD5 API
 ***********************************************************************/

static const char lua_md5_type[] = "libsnert.md5";

static int
lua_md5_new(lua_State *L)
{
	md5_state_t *md5;

	md5 = lua_newuserdata(L, sizeof (*md5));
	luaL_getmetatable(L, lua_md5_type);
	lua_setmetatable(L, -2);
	md5_init(md5);

	return 1;
}

static int
lua_md5_append(lua_State *L)
{
	size_t length;
	const char *string;
	md5_state_t *md5;

	md5 = (md5_state_t *)luaL_checkudata(L, 1, lua_md5_type);
	string = luaL_optlstring(L, 2, "", &length);
	md5_append(md5, (unsigned char *)string, length);

	return 0;
}

static int
lua_md5_end(lua_State *L)
{
	md5_state_t *md5;
	md5_byte_t digest[16];
	char digest_string[33];

	md5 = (md5_state_t *)luaL_checkudata(L, 1, lua_md5_type);
	md5_finish(md5, digest);
	md5_digest_to_string(digest, digest_string);
	lua_pushlstring(L, digest_string, 32);

	return 1;
}

#include <com/snert/lib/util/ixhash.h>

static int
lua_md5_use_ixhash(lua_State *L, int (*ixhash_test)(const unsigned char *, size_t))
{
	size_t length;
	const char *string;

	string = luaL_optlstring(L, 2, "", &length);
	lua_pushboolean(L, (*ixhash_test)((unsigned char *)string, length));

	return 1;
}

static int
lua_md5_use_ixhash1(lua_State *L)
{
	return lua_md5_use_ixhash(L, ixhash_condition1);
}

static int
lua_md5_use_ixhash2(lua_State *L)
{
	return lua_md5_use_ixhash(L, ixhash_condition2);
}

static int
lua_md5_use_ixhash3(lua_State *L)
{
	return lua_md5_use_ixhash(L, ixhash_condition3);
}

static int
lua_md5_ixhash(lua_State *L, void (*ixhash_fn)(md5_state_t *, const unsigned char *, size_t))
{
	size_t length;
	const char *string;
	md5_state_t *md5;

	md5 = (md5_state_t *)luaL_checkudata(L, 1, lua_md5_type);
	string = luaL_optlstring(L, 2, "", &length);
	(*ixhash_fn)(md5, (unsigned char *)string, length);

	return 0;
}

static int
lua_md5_ixhash1(lua_State *L)
{
	return lua_md5_ixhash(L, ixhash_hash1);
}

static int
lua_md5_ixhash2(lua_State *L)
{
	return lua_md5_ixhash(L, ixhash_hash2);
}

static int
lua_md5_ixhash3(lua_State *L)
{
	return lua_md5_ixhash(L, ixhash_hash3);
}

static const luaL_Reg lua_md5_package_f[] = {
	{ "new", 	lua_md5_new },
	{ NULL, NULL },
};

static const luaL_Reg lua_md5_package_m[] = {
	{ "append", 		lua_md5_append },
	{ "done", 		lua_md5_end },
	{ "ixhash1",		lua_md5_ixhash1 },
	{ "ixhash2",		lua_md5_ixhash2 },
	{ "ixhash3",		lua_md5_ixhash3 },
	{ "use_ixhash1",	lua_md5_use_ixhash1 },
	{ "use_ixhash2",	lua_md5_use_ixhash2 },
	{ "use_ixhash3",	lua_md5_use_ixhash3 },
	{ NULL, NULL },
};

static void
lua_define_md5(lua_State *L)
{
	luaL_newmetatable(L, lua_md5_type);		/* meta */
	lua_pushvalue(L, -1);				/* meta meta */
	lua_setfield(L, -2, "__index");			/* meta */
	luaL_register(L, NULL, lua_md5_package_m);	/* meta */
	luaL_register(L, "md5", lua_md5_package_f);	/* meta md5 */
	lua_pop(L, 2);					/* -- */
}

/***********************************************************************
 *** Lua Uri API
 ***********************************************************************/

static void
lua_pushuri(lua_State *L, URI *uri)
{
	lua_newtable(L);			/* uri */

	lua_table_set_string(L, -1, "uri_raw", 		uri->uri);
	lua_table_set_string(L, -1, "uri_decoded", 	uri->uriDecoded);
	lua_table_set_string(L, -1, "scheme", 		uri->scheme);
	lua_table_set_string(L, -1, "scheme_info", 	uri->schemeInfo);
	lua_table_set_string(L, -1, "user_info", 	uri->userInfo);
	lua_table_set_string(L, -1, "host", 		uri->host);
	lua_table_set_integer(L, -1, "port", 		uriGetSchemePort(uri));
	lua_table_set_string(L, -1, "path", 		uri->path);
	lua_table_set_string(L, -1, "query", 		uri->query);
	lua_table_set_string(L, -1, "fragment",		uri->fragment);

	if (verb_uri.value) {
		SmtpCtx *ctx = lua_smtp_ctx(L);
		syslog(
			LOG_DEBUG,
			LOG_FMT "uri_raw=%s uri_decoded=%s scheme=%s scheme_info=%s user_info=%s host=%s port=%d path=%s query=%s fragment=%s",
			LOG_ID(ctx),
			uri->uri,
			uri->uriDecoded,
			uri->scheme,
			uri->schemeInfo,
			uri->userInfo,
			uri->host,
			uriGetSchemePort(uri),
			uri->path,
			uri->query,
			uri->fragment
		);
	}
}

/**
 * table = uri.parse(string)
 */
static int
uri_parse(lua_State *L)
{
	URI *uri;
	size_t length;
	const char *string;

	string = luaL_optlstring(L, 1, NULL, &length);

	if (verb_uri.value) {
		SmtpCtx *ctx = lua_smtp_ctx(L);
		syslog(LOG_DEBUG, LOG_FMT "uri.parse(%s)", LOG_ID(ctx), string);
	}

	if ((uri = uriParse(string, (int) length)) == NULL) {
		lua_pushnil(L);
	} else {
		lua_pushuri(L, uri);
		free(uri);
	}

	return 1;
}

/**
 * string = uri.encode(string)
 */
static int
uri_encode(lua_State *L)
{
	char *string;
	size_t length;

	string = (char*)luaL_optlstring(L, 1, NULL, &length);

	if ((string = uriEncode(string)) == NULL) {
		lua_pushnil(L);
	} else {
		lua_pushstring(L, string);
		free(string);
	}

	return 1;
}

/**
 * string = uri.decode(string)
 */
static int
uri_decode(lua_State *L)
{
	char *string;
	size_t length;

	string = (char*)luaL_optlstring(L, 1, NULL, &length);

	if ((string = uriDecode(string)) == NULL) {
		lua_pushnil(L);
	} else {
		lua_pushstring(L, string);
		free(string);
	}

	return 1;
}

static const luaL_Reg lua_uri_package[] = {
	{ "parse", 			uri_parse },
	{ "decode", 			uri_decode },
	{ "encode", 			uri_encode },
	{ NULL, NULL },
};

static void
lua_define_uri(lua_State *L)
{
	luaL_register(L, "uri", lua_uri_package);	/* pkg */
	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua Utility API
 ***********************************************************************/

/**
 * integer = util.cpucount()
 */
static int
util_cpucount(lua_State *L)
{
	lua_pushinteger(L, sysGetCpuCount());
	return 1;
}

/**
 * boolean = util.mkpath(string)
 */
static int
util_mkpath(lua_State *L)
{
	int bool = mkpath(luaL_optstring(L, 1, "."));
	lua_pushboolean(L, !bool);
	return 1;
}

/**
 * length[, integer] = util.date_to_time(string)
 */
static int
util_date_to_time(lua_State *L)
{
	time_t value;
	const char *start, *stop;

	start = luaL_optstring(L, 1, NULL);
	if (convertDate(start, &value, &stop) == 0) {
		lua_pushinteger(L, stop - start);
		lua_pushinteger(L, (lua_Integer) value);
		return 2;
	}

	lua_pushinteger(L, 0);

	return 1;
}

#ifdef HAVE_GETLOADAVG
static int
util_getloadavg(lua_State *L)
{
	double avg[3];

	if (getloadavg(avg, 3) != -1) {
		lua_newtable(L);

		lua_pushnumber(L, avg[0]);
		lua_rawseti(L, -2, 1);
		lua_pushnumber(L, avg[1]);
		lua_rawseti(L, -2, 2);
		lua_pushnumber(L, avg[2]);
		lua_rawseti(L, -2, 3);

		lua_pushnumber(L, avg[0]);
		lua_setfield(L, -2, "1m_avg");
		lua_pushnumber(L, avg[1]);
		lua_setfield(L, -2, "5m_avg");
		lua_pushnumber(L, avg[2]);
		lua_setfield(L, -2, "15m_avg");

		return 1;
	}

	return 0;
}
#endif /* HAVE_GETLOADAVG */

static const luaL_Reg util_pkg[] = {
	{ "mkpath",		util_mkpath },
	{ "date_to_time",	util_date_to_time },
#ifdef HAVE_GETLOADAVG
	{ "getloadavg",		util_getloadavg },
#endif /* HAVE_GETLOADAVG */
	{ "cpucount",		util_cpucount },
	{ NULL, NULL },
};

static void
lua_define_util(lua_State *L)
{
	luaL_register(L, "util", util_pkg);		/* pkg */
	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua Text API
 ***********************************************************************/

/**
 * array = text.split(string, delims)
 */
static int
lua_text_split(lua_State *L)
{
	Vector v;
	const char *string;

	if ((string = luaL_optstring(L, 1, NULL)) == NULL) {
		lua_pushnil(L);
	} else {
		v = TextSplit(string, luaL_optstring(L, 2, " \t"), 0);
		lua_vector_to_array(L, v);
		VectorDestroy(v);
	}

	return 1;
}

/**
 * offset = text.find(haystack, needle, caseless)
 */
static int
lua_text_find(lua_State *L)
{
	long offset;

	offset = TextFind(
		luaL_optstring(L, 1, NULL),
		luaL_optstring(L, 2, NULL),
		-1, luaL_optint(L, 3, 0)
	);

	lua_pushinteger(L, offset+1);

	return 1;
}

/**
 * diff = text.natcmp(a, b, caseless)
 */
static int
lua_text_natcmp(lua_State *L)
{
	int diff;
	const char *a, *b;

	a = luaL_optstring(L, 1, NULL);
	b = luaL_optstring(L, 2, NULL);

	if (a == NULL && b != NULL)
		diff = 1;
	else if (a != NULL && b == NULL)
		diff = -1;
	else if (a == NULL && b == NULL)
		diff = 0;
	else
		diff = strnatcmp0((const unsigned char *)a, (const unsigned char *)b, luaL_optint(L, 3, 0));

	lua_pushinteger(L, diff);

	return 1;
}


static const luaL_Reg lua_text_pkg[] = {
	{ "find",		lua_text_find },
	{ "split",		lua_text_split },
	{ "natcmp",		lua_text_natcmp },
	{ NULL, NULL },
};

static void
lua_define_text(lua_State *L)
{
	luaL_register(L, "text", lua_text_pkg);		/* pkg */
	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua HTTP API
 ***********************************************************************/

static struct map_integer http_code_constants[] = {
	{ "CONTINUE",			/* 0 */		HTTP_CONTINUE },
	{ "DROP",			/* 10 */	HTTP_DROP },

	{ "OK",				/* 200 */	HTTP_OK },
	{ "CREATED",			/* 201 */	HTTP_CREATED },
	{ "ACCEPTED",			/* 202 */	HTTP_ACCEPTED },
	{ "NON_AUTH_INFO",		/* 203 */	HTTP_NON_AUTH_INFO },
	{ "NO_CONTENT",			/* 204 */	HTTP_NO_CONTENT },
	{ "RESET_CONTENT",		/* 205 */	HTTP_RESET_CONTENT },
	{ "PARTIAL_CONTENT",		/* 206 */	HTTP_PARTIAL_CONTENT },

	{ "MULTIPLE_CHOICES",		/* 300 */	HTTP_MULTIPLE_CHOICES },
	{ "MOVED_PERMANENTLY",		/* 301 */	HTTP_MOVED_PERMANENTLY },
	{ "FOUND",			/* 302 */	HTTP_FOUND },
	{ "SEE_OTHER",			/* 303 */	HTTP_SEE_OTHER },
	{ "NOT_MODIFIED",		/* 304 */	HTTP_NOT_MODIFIED },
	{ "USE_PROXY",			/* 305 */	HTTP_USE_PROXY },
	{ "TEMPORARY_REDIRECT",		/* 307 */	HTTP_TEMPORARY_REDIRECT },

	{ "BAD_REQUEST",		/* 400 */	HTTP_BAD_REQUEST },
	{ "UNAUTHORIZED",		/* 401 */	HTTP_UNAUTHORIZED },
	{ "PAYMENT_REQUIRED",		/* 402 */	HTTP_PAYMENT_REQUIRED },
	{ "FORBIDDEN",			/* 403 */	HTTP_FORBIDDEN },
	{ "NOT_FOUND",			/* 404 */	HTTP_NOT_FOUND },
	{ "METHOD_NOT_ALLOWED",		/* 405 */	HTTP_METHOD_NOT_ALLOWED },
	{ "NOT_ACCEPTABLE",		/* 406 */	HTTP_NOT_ACCEPTABLE },
	{ "PROXY_AUTH_REQUIRED",	/* 407 */	HTTP_PROXY_AUTH_REQUIRED },
	{ "REQUEST_TIMEOUT",		/* 408 */	HTTP_REQUEST_TIMEOUT },
	{ "CONFLICT",			/* 409 */	HTTP_CONFLICT },
	{ "GONE",			/* 410 */	HTTP_GONE },
	{ "LENGTH_REQUIRED",		/* 411 */	HTTP_LENGTH_REQUIRED },
	{ "PRECOND_FAILED",		/* 412 */	HTTP_PRECOND_FAILED },
	{ "REQUEST_TOO_LARGE",		/* 413 */	HTTP_REQUEST_TOO_LARGE },
	{ "URI_TOO_LONG",		/* 414 */	HTTP_URI_TOO_LONG },
	{ "UNSUPPORTED_MEDIA",		/* 415 */	HTTP_UNSUPPORTED_MEDIA },
	{ "RANGE_NOT_POSSIBLE",		/* 416 */	HTTP_RANGE_NOT_POSSIBLE },
	{ "EXPECTATION_FAILED",		/* 417 */	HTTP_EXPECTATION_FAILED },

	{ "INTERNAL",			/* 500 */	HTTP_INTERNAL },
	{ "NOT_IMPLEMENTED",		/* 501 */	HTTP_NOT_IMPLEMENTED },
	{ "BAD_GATEWAY",		/* 502 */	HTTP_BAD_GATEWAY },
	{ "SERVICE_UNAVAILABLE",	/* 503 */	HTTP_SERVICE_UNAVAILABLE },
	{ "GATEWAY_TIMEOUT",		/* 504 */	HTTP_GATEWAY_TIMEOUT },
	{ "VERSION_NOT_SUPPORTED",	/* 505 */	HTTP_VERSION_NOT_SUPPORTED },
	{ NULL, 0 }
};

static
PT_THREAD(http_yielduntil(Service *svc, SmtpCtx *ctx))
{
	HttpContent *content = svc->data;
	return httpReadPt(&content->response);
}

static int
http_yieldafter(Service *svc, SmtpCtx *ctx)
{
	lua_State *L1;
	HttpContent *content = svc->data;
	HttpResponse *response = &content->response;

	if ((L1 = lua_getthread(ctx->script, ctx)) != NULL) {
		lua_table_getglobal(L1, "__service");	/* __svc */
		lua_table_getfield(L1, -1, "http");	/* __svc http */

		lua_pushliteral(L1, "http");
		lua_setfield(L1, -2, "service_name");

		lua_newtable(L1);			/* __svc http t */

		lua_pushstring(L1, response->url);
		lua_setfield(L1, -2, "url");
		lua_pushstring(L1, response->id_log);
		lua_setfield(L1, -2, "id");
		lua_pushinteger(L1, response->result);
		lua_setfield(L1, -2, "rcode");
		lua_pushlstring(L1, (char *)response->content->bytes, response->eoh);
		lua_setfield(L1, -2, "headers");
		lua_pushlstring(L1, (char *)response->content->bytes+response->eoh, response->content->length-response->eoh);
		lua_setfield(L1, -2, "content");

		lua_pushinteger(L1, content->date);
		lua_setfield(L1, -2, "date");
		lua_pushinteger(L1, content->expires);
		lua_setfield(L1, -2, "expires");
		lua_pushinteger(L1, content->last_modified);
		lua_setfield(L1, -2, "last_modified");
		lua_pushstring(L1, content->content_type);
		lua_setfield(L1, -2, "content_type");
		lua_pushstring(L1, content->content_encoding);
		lua_setfield(L1, -2, "content_encoding");

		lua_pushliteral(L1, "http");
		lua_setfield(L1, -2, "service_name");
		service_time(svc, L1, -1);

		lua_array_push(L1, -2);			/* __svc http */
		lua_setfield(L1, -2, "http");		/* __svc */
		lua_setglobal(L1, "__service");
	}

	return 0;
}

static void
http_free(void *data)
{
	httpContentFree(data);
	free(data);
}

/**
 * boolean = service.http.request(url, [method, [modified_since, [post]]])
 */
static int
service_http_request(lua_State *L)
{
	Service *svc;
	HttpRequest request;
	HttpContent *content;
	SmtpCtx *ctx = lua_smtp_ctx(L);

	if (ctx == NULL)
//		return luaL_error(L, LOG_FN_FMT "client context not found ", LOG_FN(ctx));
		goto error0;

	if ((content = malloc(sizeof (*content))) == NULL)
		goto error0;

	httpSetDebug(verb_http.value);
	httpContentInit(content);
	content->response.timeout = HTTP_TIMEOUT_MS;

	memset(&request, 0, sizeof (request));

	if ((request.url = uriParse(luaL_optstring(L, 1, NULL), -1)) == NULL)
		goto error1;

	if (lua_isstring(L, 3)) {
		if (convertDate(luaL_optstring(L, 3, NULL), &request.if_modified_since, NULL))
			request.if_modified_since = 0;
	} else {
		request.if_modified_since = luaL_optlong(L, 3, 0);
	}

	request.debug = content->response.debug;
	request.id_log = content->response.id_log;
	request.timeout = HTTP_TIMEOUT_MS;
	request.method = luaL_optstring(L, 2, "HEAD");
	request.post_buffer = (unsigned char *)luaL_optlstring(L, 4, NULL, &request.post_size);

	content->response.url = strdup(request.url->uri);
	if ((content->response.socket = httpSend(&request)) < 0)
		goto error2;
	if ((svc = service_new(ctx)) == NULL)
		goto error3;

	svc->data = content;
	svc->free = http_free;
	svc->socket = content->response.socket;
	svc->host = strdup(request.url->host);
	svc->service = http_yielduntil;
	svc->results = http_yieldafter;

	free(request.url);

	if (service_add(ctx, svc, HTTP_TIMEOUT_MS/UNIT_MILLI))
		goto error4;

	lua_pushboolean(L, 1);

	return 1;
error4:
	free(svc);
error3:
	socket3_close(content->response.socket);
error2:
	free(request.url);
error1:
	httpContentFree(content);
	free(content);
error0:
	lua_pushboolean(L, 0);

	return 1;
}

#ifdef REMOVE
/**
 * table = http.do(url, method, modified_since, post)
 */
static int
lua_http_do(lua_State *L)
{
	int is_ready;

	(void) service_http_request(L);
	is_ready = lua_toboolean(L, -1);
	lua_pop(L, lua_gettop(L));

	if (is_ready)
		return service_wait(L);

	return -1;
}

/**
 * table = http.get(url, modified_since)
 */
static int
lua_http_doget(lua_State *L)
{
	lua_pushliteral(L, "GET");	/* url [since] GET */
	lua_insert(L, 2);		/* url GET [since] */
	return lua_http_do(L);
}

/**
 * table = http.head(url, modified_since)
 */
static int
lua_http_dohead(lua_State *L)
{
	lua_pushliteral(L, "HEAD");
	lua_insert(L, 2);
	return lua_http_do(L);
}

/**
 * table = http.post(url, modified_since, post)
 */
static int
lua_http_dopost(lua_State *L)
{
	lua_pushliteral(L, "POST");
	lua_insert(L, 2);
	return lua_http_do(L);
}

static const luaL_Reg lua_http_pkg[] = {
	{ "request", 			lua_http_do },
	{ "get", 			lua_http_doget },
	{ "head", 			lua_http_dohead },
	{ "post", 			lua_http_dopost },
	{ NULL, NULL },
};

static void
lua_define_http(lua_State *L)
{
	struct map_integer *map;

	luaL_register(L, "http", lua_http_pkg);		/* pkg */

	lua_newtable(L);				/* pkg code */
	for (map = http_code_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);
	}
	lua_setfield(L, -2, "code");			/* pkg */

	lua_pop(L, 1);					/* -- */
}
#endif

/***********************************************************************
 *** Lua Service API
 ***********************************************************************/

static const luaL_Reg lua_service_pkg[] = {
	{ "wait", 		service_wait },
	{ "reset",		service_reset },
	{ "clamd", 		service_clamd },
	{ "spamd", 		service_spamd },
	{ NULL, NULL },
};

static const luaL_Reg lua_service_http[] = {
	{ "request", 		service_http_request },
	{ NULL, NULL },
};

static const luaL_Reg lua_service_client[] = {
	{ "write", 		service_client_write },
	{ NULL, NULL },
};

static void
lua_define_service(lua_State *L)
{
	const luaL_Reg *reg;
	struct map_integer *map;

	luaL_register(L, "service", lua_service_pkg);	/* pkg */

	lua_newtable(L);				/* pkg t */

	/* service.http.* functions */
	for (reg = lua_service_http; reg->name != NULL; reg++) {
		lua_pushcfunction(L, reg->func);	/* pkg http fn */
		lua_setfield(L, -2, reg->name);		/* pkg http */
	}

	/* service.http.code.* constants */
	lua_newtable(L);				/* pkg http code */
	for (map = http_code_constants; map->name != NULL; map++) {
		lua_table_set_integer(L, -1, map->name, map->value);

		lua_pushinteger(L, map->value);
		lua_pushstring(L, map->name);
		lua_settable(L, -3);
	}
	lua_setfield(L, -2, "code");			/* pkg http */
	lua_setfield(L, -2, "http");			/* pkg */

	/* service.client.* functions */
	lua_newtable(L);				/* pkg client */
	for (reg = lua_service_client; reg->name != NULL; reg++) {
		lua_pushcfunction(L, reg->func);	/* pkg client fn */
		lua_setfield(L, -2, reg->name);		/* pkg client */
	}
	lua_setfield(L, -2, "client");			/* pkg */

	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** SMTPE
 ***********************************************************************/

/**
 * string = smtpe.getoption(option_name)
 */
static int
smtpe_getoption(lua_State *L)
{
	Option *option;

	if ((option = optionFind(opt_table, luaL_optstring(L, 1, NULL))) != NULL) {
		lua_pushstring(L, option->string);
		return 1;
	}

	return 0;
}

/**
 * smtpe.setoption(option_name, value)
 */
static int
smtpe_setoption(lua_State *L)
{
	Option *option;

	if ((option = optionFind(opt_table, luaL_optstring(L, 1, NULL))) != NULL) {
		(void) optionSet(option, strdup(luaL_optstring(L, 2, NULL)));
	}

	return 0;
}

static const luaL_Reg smtpe_pkg[] = {
	{ "getoption", 		smtpe_getoption },
	{ "setoption",		smtpe_setoption },
	{ NULL, NULL },
};

static void
lua_define_smtpe(lua_State *L)
{
	luaL_register(L, _NAME , smtpe_pkg);		/* pkg */

	lua_pushliteral(L, _VERSION);
	lua_setfield(L, -2, "bin_version");
	lua_pushliteral(L, API_VERSION);
	lua_setfield(L, -2, "api_version");
	lua_pushliteral(L, _COPYRIGHT);
	lua_setfield(L, -2, "copyright");
	lua_pushstring(L, my_host_name);
	lua_setfield(L, -2, "host");

	lua_pop(L, 1);					/* -- */
}

/***********************************************************************
 *** Lua Interface
 ***
 *** Setup a master state (interpreter) within which we create all our
 *** globals and "spawn" Lua threads per client connection.
 ***********************************************************************/

#define LUA_PT_CALL_INIT(fn, init) \
	PT_SPAWN(&ctx->pt, &ctx->lua.pt, hook_do(ctx->script, ctx, # fn, init)); \
	/* Assert that the DNS is closed after Lua. */	\
	if (!opt_test.value) dns_close(loop, event)

#define LUA_CALL_INIT(fn, init) \
	PT_INIT(&ctx->lua.pt); \
	while (PT_SCHEDULE(hook_do(ctx->script, ctx, #fn, init))); \
	dns_close(ctx->client.loop, &ctx->client.event)

#define LUA_PT_CALL0(fn)	LUA_PT_CALL_INIT(fn, hook_noargs)
#define LUA_PT_CALL(fn)		LUA_PT_CALL_INIT(fn, LUA_CMD_INIT(fn))

#define LUA_CALL0(fn)		LUA_CALL_INIT(fn, hook_noargs)
#define LUA_CALL(fn)		LUA_CALL_INIT(fn, LUA_CMD_INIT(fn))

#define LUA_CALL0_SETJMP(fn)	\
	SETJMP_PUSH(&ctx->on_error); \
	if (SIGSETJMP(ctx->on_error, 1) == JMP_SET) { LUA_CALL0(fn); } \
	SETJMP_POP(&ctx->on_error);

#define LUA_CALL_SETJMP(fn)	\
	SETJMP_PUSH(&ctx->on_error); \
	if (SIGSETJMP(ctx->on_error, 1) == JMP_SET) { LUA_CALL(fn); } \
	SETJMP_POP(&ctx->on_error);

#define LUA_PROTECT_STATE0(fn) \
	ctx->lua.smtp_state = ctx->state; \
	ctx->state = SMTP_NAME(fn); \
	LUA_PT_CALL0(fn); \
	ctx->state = ctx->lua.smtp_state

#define LUA_PROTECT_STATE(fn) \
	ctx->lua.smtp_state = ctx->state; \
	ctx->state = SMTP_NAME(fn); \
	LUA_PT_CALL(fn); \
	ctx->state = ctx->lua.smtp_state

#define LUA_HOOK_DEFAULT(x)	((x) < 200)
#define LUA_HOOK_OK(x)		(LUA_HOOK_DEFAULT(x) || SMTP_IS_OK(x))

static int
hook_noargs(lua_State *L, SmtpCtx *ctx)
{
	return 0;
}

LUA_CMD_DEF(error);
LUA_CMD_DEF(interpret);

static LuaCode
hook_endthread(lua_State *L, SmtpCtx *ctx)
{
	/* Unanchor client Lua thread so it can be gc'ed. */
	luaL_unref(L, LUA_REGISTRYINDEX, ctx->lua.thread);

	return Lua_OK;
}

static lua_State *
hook_newthread(lua_State *L, SmtpCtx *ctx)
{
	lua_State *L1;

	if (L == NULL)
		return NULL;

	/* Create new Lua thread and anchor it. */
	L1 = lua_newthread(L);				/* L1 */
	ctx->lua.thread = luaL_ref(L, LUA_REGISTRYINDEX); /* -- */

	if (L1 == NULL)
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));

	return L1;
}

static
PT_THREAD(hook_do(lua_State *L, SmtpCtx *ctx, const char *hook, LuaHookInit initfn))
{
	int nargs;
	LuaCode rc;
	lua_State *L1;
	size_t reply_len;
	const char *reply, *crlf;

	if (L == NULL)
		PT_EXIT(&ctx->lua.pt);

	/* Get the coroutine thread in case of a resume after an
	 * IO event. Ignore if L1 is NULL on initial hook call as
	 * hook_newthread() will create the new coroutine thread.
	 */
	L1 = lua_getthread(L, ctx);

	PT_BEGIN(&ctx->lua.pt);

	/* Create a new coroutine thread for this hook. */
	if ((L1 = hook_newthread(L, ctx)) == NULL)
		PT_EXIT(&ctx->lua.pt);

	errno = 0;
	lua_getglobal(L1, "hook");		/* hook */
	lua_getfield(L1, -1, hook);		/* hook fn */
	lua_remove(L1, -2);			/* fn */

	nargs = (*initfn)(L1, ctx);		/* fn ... */

	if (nargs < 0 || !lua_isfunction(L1, -1 - nargs)) {
		lua_pop(L1, lua_gettop(L1));
		PT_EXIT(&ctx->lua.pt);
	}

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-before=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua.thread, lua_gettop(L1), (long) L1);

	while ((rc = lua_resume(L1, nargs)) == Lua_YIELD) {
		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-yield=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua.thread, lua_gettop(L1), (long) L1);

		/* Not expecting anything from the yield(). */
		lua_pop(L1, lua_gettop(L1));

		if (ctx->lua.yield_until == NULL || ctx->lua.yield_after == NULL)
			break;

		PT_YIELD_UNTIL(&ctx->lua.pt, (*ctx->lua.yield_until)(L1, ctx));
		nargs = (*ctx->lua.yield_after)(L1, ctx);

		if (verb_debug.value)
			syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-resume=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua.thread, lua_gettop(L1), (long) L1);
	}

	if (verb_debug.value)
		syslog(LOG_DEBUG, LOG_FMT "%s ctx=%lx thread=%d top-after=%d L1=%lx", LOG_ID(ctx), __FUNCTION__, (long) ctx, ctx->lua.thread, lua_gettop(L1), (long) L1);

	if (rc != Lua_OK) {
		errno = EINVAL;
		ctx->smtp_rc = SMTP_ERROR;
		syslog(LOG_ERR, LOG_FMT "hook.%s: %s", LOG_ID(ctx), hook, lua_tostring(L1, -1));

		/* Discard coroutine thread in non-resumable error state. */
		hook_endthread(L1, ctx);

		SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
	} else if (0 < lua_gettop(L1) && (reply = lua_tolstring(L1, -1, &reply_len)) != NULL) {
		crlf = "";
		/* Is the reply missing a CRLF? */
		if (!(1 < reply_len && reply[reply_len-1] == '\n' && reply[reply_len-2] == '\r'))
			crlf = CRLF;
		ctx->smtp_rc = strtol(reply, NULL, 10);
		if (SMTP_IS_VALID(ctx->smtp_rc))
			ctx->reply.length = snprintf(ctx->reply.data, ctx->reply.size, "%s%s", reply, crlf);

		if (1 < lua_gettop(L1))
			ctx->client.dropped = lua_toboolean(L1, -2);
	}

	if (initfn == LUA_CMD_INIT(interpret) && 0 < lua_gettop(L1)) {
	      lua_getglobal(L1, "print");
	      lua_insert(L1, 1);
	      (void) lua_pcall(L1, lua_gettop(L1)-1, 0, 0);
	}

	lua_pop(L1, lua_gettop(L1));

	/* Coroutine thread successfully finished. */
	hook_endthread(L1, ctx);

	PT_END(&ctx->lua.pt);
}

LUA_CMD_DEF(accept)
{
	socklen_t socklen;
	SocketAddress address;

	/* Create client connection table. */
	lua_table_getglobal(L1, "client");			/* fn client */
	lua_pushvalue(L1, -1);					/* fn client client */
	lua_setglobal(L1, "client");				/* fn client */

	*ctx->work.data = '\0';
	socklen = sizeof (address);
	if (getsockname(ctx->client.socket, &address.sa, &socklen) == 0) {
		ctx->work.length = socketAddressFormatIp(&address.sa, SOCKET_ADDRESS_AS_IPV4, ctx->work.data, ctx->work.size);
		lua_pushlstring(L1, ctx->work.data, ctx->work.length);	/* fn client our_ip */
		lua_setfield(L1, -2, "local_address");		/* fn client */
	}

	lua_pushstring(L1, ctx->id_sess);			/* fn client id */
	lua_setfield(L1, -2, "id_sess");			/* fn client */

	lua_pushboolean(L1, ctx->client.is_pipelining);		/* fn client flag */
	lua_setfield(L1, -2, "is_pipelining");			/* fn client */

	lua_table_set_integer(L1, -1, "thread", ctx->lua.thread);
	lua_table_set_integer(L1, -1, "port", opt_test.value ? 0 : socketAddressGetPort(&ctx->client.addr));

	lua_pushlstring(L1, ctx->addr.data, ctx->addr.length);	/* fn client ip */
	lua_pushvalue(L1, -1);					/* fn client ip ip */
	lua_setfield(L1, -3, "address");			/* fn client ip */

	lua_pushlstring(L1, ctx->host.data, ctx->host.length);	/* fn client ip host */
	lua_pushvalue(L1, -1);					/* fn client ip host host */
	lua_setfield(L1, -4, "host");				/* fn client ip host */
	lua_remove(L1, -3);					/* fn ip host */

	return 2;
}

LUA_CMD_DEF(close)
{
	lua_pushinteger(L1, ctx->client.dropped);			/* fn arg */
	return 1;
}

LUA_CMD_DEF(helo)
{
	lua_pushlstring(L1, ctx->helo.data, ctx->helo.length);		/* fn arg */
	return 1;
}

LUA_CMD_DEF(ehlo)
{
	lua_pushlstring(L1, ctx->helo.data, ctx->helo.length);		/* fn arg */
	return 1;
}

LUA_CMD_DEF(auth)
{
	lua_pushlstring(L1, ctx->auth.data, ctx->auth.length);		/* fn arg */
	return 1;
}

LUA_CMD_DEF(mail)
{
	lua_getglobal(L1, "client");		/* fn client */
	lua_table_set_string(L1, -1, "id_trans", ctx->id_trans);
	lua_pop(L1, 1);

	lua_pushlstring(L1, ctx->sender->address.string, ctx->sender->address.length);	/* fn arg */
	lua_pushlstring(L1, ctx->sender->domain.string, ctx->sender->domain.length);	/* fn arg */

	return 2;
}

LUA_CMD_DEF(rcpt)
{
	char *at_sign;

	lua_pushstring(L1, *ctx->rcpt);		/* fn arg */

	if ((at_sign = strchr(*ctx->rcpt, '@')) != NULL) {
		TextLower(at_sign+1, -1);
		lua_pushstring(L1, at_sign+1);		/* fn arg */
	} else {
		lua_pushstring(L1, "");
	}

	return 2;
}

LUA_CMD_DEF(data)
{
	lua_getglobal(L1, "client");		/* fn client */
	lua_pushlstring(L1, ctx->path.data, ctx->path.length);
	lua_setfield(L1, -2, "msg_file");
	lua_pop(L1, 1);

	return 0;
}

LUA_CMD_DEF(out_seq)
{
	lua_pushlstring(L1, ctx->input.data, ctx->input.length);
	return 1;
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

#define DOT_CRLF		"." CRLF
#define DOT_LF			"." LF

LUA_CMD_DEF(header)
{
	char *hdr;
	int span, is_crlf;

	/* This line is EOH? */
	if (STARTS_WITH(CRLF)) {
		ctx->input.offset += STRLEN(CRLF);
		ctx->length += STRLEN(CRLF);
		ctx->eoh = ctx->length;

		/* Tell hook_do() to abort. */
		return -1;
	} else if (STARTS_WITH(LF)) {
		ctx->input.offset += STRLEN(LF);
		ctx->length += STRLEN(LF);
		ctx->eoh = ctx->length;

		/* Tell hook_do() to abort. */
		return -1;
	}

	/* Find end of header line. */
	for (span = 0; ; span++) {
		span += strcspn(ctx->input.data+ctx->input.offset+span, LF);
		if (ctx->input.length <= ctx->input.offset+span)
			break;
		if (!isblank(ctx->input.data[ctx->input.offset+span+1]))
			break;
	}

	/* Backup one byte if end of line is CRLF. */
	span -= (is_crlf = (0 < span && ctx->input.data[ctx->input.offset+span-1] == '\r'));

	/* Push the line. */
	lua_pushlstring(L1, ctx->input.data+ctx->input.offset, span);

	/* Save a copy of the original header that can be modified. */
	if ((hdr = malloc(span+1)) != NULL) {
		(void) TextCopy(hdr, span+1, ctx->input.data+ctx->input.offset);
		if (VectorAdd(ctx->headers, hdr))
			free(hdr);
	}

	/* Skip over the newline. */
	ctx->input.offset += span + is_crlf + 1;
	ctx->length += span + is_crlf + 1;

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
	ctx->length += span + is_crlf + 1;

	return 1;
}

LUA_CMD_DEF(dot)
{
	lua_pushlstring(L1, ctx->path.data, ctx->path.length);

	lua_getglobal(L1, "client");				/* client */
	lua_pushinteger(L1, ctx->length);			/* client length */
	lua_setfield(L1, -2, "message_length");			/* client */
	lua_pop(L1, 1);

	return 1;
}

LUA_CMD_DEF(forward)
{
	lua_pushlstring(L1, ctx->path.data, ctx->path.length);
	lua_pushlstring(L1, ctx->sender->address.string,  ctx->sender->address.length);
	lua_vector_to_array(L1, ctx->rcpts);
	return 3;
}

LUA_CMD_DEF(reply)
{
	lua_pushlstring(L1, ctx->reply.data, ctx->reply.length);
	return 1;
}

LUA_CMD_DEF(error)
{
	if (errno == 0)
		/* Tell hook_do() to abort. */
		return -1;

	lua_pushinteger(L1, errno);
	lua_pushstring(L1, strerror(errno));
	return 2;
}

LUA_CMD_DEF(interpret)
{
	lua_pop(L1, lua_gettop(L1));

	if (luaL_loadbuffer(L1, ctx->input.data, ctx->input.length, "=stdin")) {
		if (!lua_isnil(L1, -1)) {
			const char *msg = lua_tostring(L1, -1);
			printf("error: %s\n", msg);
		}
		lua_pop(L1, lua_gettop(L1));
		SIGLONGJMP(ctx->on_error, JMP_ERROR);
	}

	return 0;
}

static lua_State *
hook_init(SmtpCtx *ctx)
{
	lua_State *L;

	if ((L = luaL_newstate()) == NULL)
		goto error0;

	/* Save client's context for use by C API. */
	lua_pushlightuserdata(L, ctx);			/* ctx */
	lua_setglobal(L, "__ctx");			/* -- */

	lua_newtable(L);
	lua_setglobal(L, "hook");

	lua_define_client(L);
	lua_define_smtpe(L);
	lua_define_dns(L);
	lua_define_header(L);
	lua_define_md5(L);
	lua_define_net(L);
	lua_define_smtp(L);
	lua_define_service(L);
	lua_define_syslog(L);
	lua_define_text(L);
	lua_define_uri(L);
	lua_define_util(L);

	/* Stop collector during initialization. */
	lua_gc(L, LUA_GCSTOP, 0);
	luaL_openlibs(L);
	lua_gc(L, LUA_GCRESTART, 0);

	lua_getglobal(L, "syslog");			/* syslog */
	lua_getfield(L, -1, "error");			/* syslog errfn */
	lua_remove(L, -2);				/* errfn */

	switch (luaL_loadfile(L, opt_script.string)) {	/* errfn file */
	case LUA_ERRFILE:
	case LUA_ERRMEM:
	case LUA_ERRSYNTAX:
		syslog(LOG_ERR, "%s", lua_tostring(L, -1));
		goto error1;
	}

	if (lua_pcall(L, 0, LUA_MULTRET, -2)) {		/* errfn file */
		syslog(LOG_ERR, "%s init: %s", opt_script.string, TextNull(lua_tostring(L, -1))); /* errfn errmsg */
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
 *** Rate Throttling
 ***********************************************************************/

#define RATE_TICK		6		/* seconds per tick */
#define	RATE_INTERVALS		10		/* ticks per minute */
#define RATE_WINDOW_SIZE	60		/* one minute window */

typedef struct {
	unsigned long ticks;
	unsigned long count;
} RateInterval;

typedef struct {
	RateInterval intervals[RATE_INTERVALS];
	unsigned char ipv6[IPV6_BYTE_LENGTH];
	time_t touched;
} RateHash;

volatile unsigned long connections_per_second;
RateInterval cpm_intervals[RATE_INTERVALS];

static RateHash rate_hashes[HASH_TABLE_SIZE];
static pthread_mutex_t rate_mutex;
static time_t last_connection;

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

static unsigned long
rate_update(RateInterval *intervals, unsigned long ticks, int step)
{
	int i;
	RateInterval *interval;
	unsigned long connections = 0;

	/* Update the current interval. */
	interval = &intervals[ticks % RATE_INTERVALS];
	if (interval->ticks != ticks) {
		interval->ticks = ticks;
		interval->count = 0;
	}
	interval->count += step;

	/* Sum the number of connections within this window. */
	interval = intervals;
	for (i = 0; i < RATE_INTERVALS; i++) {
		if (ticks - RATE_INTERVALS <= interval->ticks && interval->ticks <= ticks)
			connections += interval->count;
		interval++;
	}

	return connections;
}

static void
rate_global(void)
{
	unsigned long cpm;
	time_t now = time(NULL);

	TRACE_FN(000);

	PTHREAD_MUTEX_LOCK(&rate_mutex);

	cpm = rate_update(cpm_intervals, now / RATE_TICK, 1);
	if (verb_debug.value)
		syslog(LOG_DEBUG, "connection-per-minute=%lu", cpm);

	/* Throttle overall connections to stave off DDoS attacks that
	 * attempt to drive up the CPU load and saturate the system.
	 * This will allow legit and bogus connections to trickle
	 * through. This combined with per client rate limits should
	 * minimise the effects of a DDoS.
	 */
	if (last_connection != now) {
		connections_per_second = 1;
		last_connection = now;
	}

	/* Keep the connections_per_second updated, even if rate-global
	 * is off.
	 */
	else if (opt_rate_global.value <= ++connections_per_second && 0 < opt_rate_global.value) {
		syslog(LOG_ERR, "%lu connections exceeds %ld per second", connections_per_second, opt_rate_global.value);
		nap(1, 0);
	}

	PTHREAD_MUTEX_UNLOCK(&rate_mutex);
}

static SMTP_Reply_Code
rate_client(SmtpCtx *ctx)
{
	int i;
	time_t now;
	SMTP_Reply_Code rc;
	RateHash *entry, *oldest;
	unsigned long hash, client_rate;

	TRACE_FN(000);

	rc = SMTP_OK;
	if (opt_rate_client.value <= 0)
		return SMTP_OK;

	/* Find a hash table entry for this client. */
	hash = djb_hash_index(ctx->ipv6, sizeof (ctx->ipv6), HASH_TABLE_SIZE);
	oldest = &rate_hashes[hash];

	PTHREAD_MUTEX_LOCK(&rate_mutex);

	for (i = 0; i < MAX_LINEAR_PROBE; i++) {
		entry = &rate_hashes[(hash + i) & (HASH_TABLE_SIZE-1)];

		if (entry->touched < oldest->touched)
			oldest = entry;

		if (memcmp(ctx->ipv6, entry->ipv6, sizeof (entry->ipv6)) == 0)
			break;
	}

	/* If we didn't find the client within the linear probe
	 * distance, then overwrite the oldest hash entry. Note
	 * that we take the risk of two or more IPs repeatedly
	 * cancelling out each other's entry. Shit happens on
	 * a full moon.
	 */
	if (MAX_LINEAR_PROBE <= i) {
		entry = oldest;
		memset(entry->intervals, 0, sizeof (entry->intervals));
		memcpy(entry->ipv6, ctx->ipv6, sizeof (entry->ipv6));
	}

	/* Parse the client's N connections per minute. We've
	 * opted for to fix the rate limit window size at 60
	 * seconds in 6 second intervals. Not being able to
	 * specify the window size globally or per client was
	 * an intentional design decision.
	 */
	(void) time(&now);
	client_rate = rate_update(entry->intervals, now / RATE_TICK, 1);
	entry->touched = now;

	if (opt_rate_client.value < client_rate) {
		(void) rate_update(entry->intervals, now / RATE_TICK, -1);
		ctx->reply.length = snprintf(ctx->reply.data, ctx->reply.size, fmt_rate_client, opt_smtp_error_url.string, CLIENT_INFO(ctx), client_rate, opt_rate_client.value);
		if (verb_debug.value)
			syslog(LOG_DEBUG,  "%s", ctx->reply.data);
		ctx->client.dropped = DROP_RATE;
		rc = SMTP_TRY_AGAIN_LATER;
	}

	PTHREAD_MUTEX_UNLOCK(&rate_mutex);

	return rc;
}

/***********************************************************************
 *** MIME and URI Parsing
 ***********************************************************************/

void
md5_mime_free(Mime *m, void *data)
{
	MimeHooks *hook = data;
	SmtpCtx *ctx = hook->data;

	if (ctx != NULL) {
		free(ctx->md5.content_encoding);
		ctx->md5.content_encoding = NULL;
		free(ctx->md5.content_type);
		ctx->md5.content_type = NULL;
		free(hook);
	}
}

void
md5_header(Mime *m, void *data)
{
	SmtpCtx *ctx = data;

	if (0 <= TextFind((char *) m->source.buffer, "Content-Transfer-Encoding:*", m->source.length, 1)) {
		free(ctx->md5.content_encoding);
		ctx->md5.content_encoding = strdup((char *)m->source.buffer);
	} else if (0 <= TextFind((char *) m->source.buffer, "Content-Type:*", m->source.length, 1)) {
		free(ctx->md5.content_type);
		ctx->md5.content_type = strdup((char *)m->source.buffer);
	}
}

void
md5_body_start(Mime *m, void *data)
{
	SmtpCtx *ctx = data;

	if (ctx != NULL) {
		md5_init(&ctx->md5.source);
		md5_init(&ctx->md5.decode);
	}
}

void
md5_body_finish(Mime *m, void *data)
{
	lua_State *L;
	SmtpCtx *ctx = data;
	md5_byte_t digest[16];
	char digest_string[33];

	if (ctx == NULL)
		return;

	L = ctx->script;
	lua_getglobal(L, "mime");		/* mime */
	lua_getfield(L, -1, "parts");		/* mime parts */
	lua_newtable(L);			/* mime parts part */

	md5_finish(&ctx->md5.source, digest);
	md5_digest_to_string(digest, digest_string);
	lua_table_set_string(L, -1, "md5_encoded", digest_string);

	if (verb_mime.value)
		syslog(LOG_DEBUG, LOG_FMT "md5_encoded=%s", LOG_ID(ctx), digest_string);

	md5_finish(&ctx->md5.decode, digest);
	md5_digest_to_string(digest, digest_string);
	lua_table_set_string(L, -1, "md5_decoded", digest_string);

	if (verb_mime.value)
		syslog(LOG_DEBUG, LOG_FMT "md5_decoded=%s", LOG_ID(ctx), digest_string);

	if (verb_mime.value) {
		syslog(
			LOG_DEBUG,
			LOG_FMT "part_length=%ld body_length=%ld content_type=%s content_transfer_encoding=%s",
			LOG_ID(ctx),
			m->mime_part_length,
			m->mime_body_length,
			ctx->md5.content_type,
			ctx->md5.content_encoding
		);
	}

	lua_table_set_integer(L, -1, "part_length", m->mime_part_length);
	lua_table_set_integer(L, -1, "body_length", m->mime_body_length);
	lua_table_set_string(L, -1, "content_type", ctx->md5.content_type);
	free(ctx->md5.content_type); ctx->md5.content_type = NULL;
	lua_table_set_string(L, -1, "content_transfer_encoding", ctx->md5.content_encoding);
	free(ctx->md5.content_encoding); ctx->md5.content_encoding = NULL;

	lua_array_push(L, -2);			/* mime parts */
	lua_pop(L, 2);				/* -- */
}

void
md5_source_flush(Mime *m, void *data)
{
	SmtpCtx *ctx = data;

	md5_append(&ctx->md5.source, m->source.buffer, m->source.length);
}

void
md5_decode_flush(Mime *m, void *data)
{
	SmtpCtx *ctx = data;

	md5_append(&ctx->md5.decode, m->decode.buffer, m->decode.length);
}

static MimeHooks md5_hook = {
	NULL,
	md5_mime_free,
	md5_header,
	md5_body_start,
	md5_body_finish,
	md5_source_flush,
	md5_decode_flush
};

MimeHooks *
md5_mime_init(SmtpCtx *ctx)
{
	lua_State *L;
	MimeHooks *hook;

	if ((hook = malloc(sizeof (*hook))) != NULL) {
		*hook = md5_hook;
		hook->data = ctx;

		L = ctx->script;
		lua_newtable(L);		/* mime */
		lua_newtable(L);		/* mime parts */
		lua_setfield(L, -2, "parts");	/* mime */
		lua_setglobal(L, "mime");	/* -- */

		if (verb_mime.value)
			syslog(LOG_DEBUG, LOG_FMT "%s top=%d", LOG_ID(ctx), __FUNCTION__, lua_gettop(L));
	}

	return hook;
}

void
uri_mime_found(URI *uri, void *data)
{
	lua_State *L;
	SmtpCtx *ctx = data;
	md5_state_t md5_key;
	md5_byte_t digest[16];
	char digest_string[33];

	if (ctx == NULL)
		return;

	md5_init(&md5_key);
	md5_append(&md5_key, (unsigned char*)uri->uri, strlen(uri->uri));
	md5_finish(&md5_key, digest);
	md5_digest_to_string(digest, digest_string);

	if (verb_uri.value)
		syslog(LOG_DEBUG, LOG_FMT "found uri=%s md5=%s", LOG_ID(ctx), uri->uri, digest_string);

	L = ctx->script;
	lua_getglobal(L, "uri");		/* uri{} */
	lua_getfield(L, -1, "found");		/* uri{} array */
	lua_remove(L, -2);			/* array */
	lua_getfield(L, -1, digest_string);	/* array uri|nil */

	/* Does the uri already exist in the table? */
	if (lua_istable(L, -1)) {
		if (verb_uri.value)
			syslog(LOG_DEBUG, LOG_FMT "uri already in table", LOG_ID(ctx));

		lua_pop(L, 2);			/* -- */
		return;
	}

	lua_pop(L, 1);				/* array */

	/* Add indexed entry of MD5 key for # operator. */
	lua_pushlstring(L, digest_string, sizeof (digest_string)-1);	/* array key */
	lua_pushvalue(L, -1);			/* array key key*/
	lua_array_push(L, -3);			/* array key */

	lua_pushuri(L, uri);			/* array key uri */

	/* Save the mime part location of where this uri was
	 * found. 0 for message headers, 1 first body part, etc.
	 */
	lua_pushinteger(L, ctx->mime->mime_part_number+1 - mimeIsHeaders(ctx->mime));	/* array key uri part */
	lua_setfield(L, -2, "mime_part");	/* array key uri */

	/* Add MD5 key and uri table to array. */
	lua_settable(L, -3);			/* array */

	if (verb_uri.value)
		syslog(LOG_DEBUG, LOG_FMT "uri.found length=%lu", LOG_ID(ctx), (unsigned long)lua_objlen(L, -1));

	lua_pop(L, 1);				/* -- */
}

MimeHooks *
uri_mime_init(SmtpCtx *ctx)
{
	lua_State *L;
	MimeHooks *hook;

	if ((hook = (MimeHooks *)uriMimeInit(uri_mime_found, 0, ctx)) != NULL) {
		L = ctx->script;
		lua_getglobal(L, "uri");
		lua_newtable(L);
		lua_setfield(L, -2, "found");
		lua_pop(L, 1);
	}

	return hook;
}

/***********************************************************************
 *** SMTP States
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
 *	ymd HMS ppppp 00
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
 *	ymd HMS ppppp cc
 *
 * cc is Base62 number excluding 00.
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
	lua_State *L;
	TRACE_CTX(ctx, 000);

	LUA_CALL0_SETJMP(reset);

	L = ctx->script;
	lua_getglobal(L, "client");		/* client */
	lua_table_clear(L, -1, "id_trans");
	lua_table_clear(L, -1, "msg_file");
	lua_pop(L, 1);				/* -- */

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
	mimeFree(ctx->mime);
	ctx->mime = NULL;
	ctx->state = ctx->state_helo;
	VectorRemoveAll(ctx->rcpts);
	VectorRemoveAll(ctx->headers);
	free(ctx->sender);
	ctx->sender = NULL;
}

extern SMTP_DEF(ehlo);
extern SMTP_DEF(data);
extern SMTP_DEF(content);

int
client_pipelining(SmtpCtx *ctx)
{
	if (socket3_has_input(ctx->client.socket, SMTP_PIPELINING_TIMEOUT)) {
		if (verb_info.value)
			syslog(LOG_INFO, LOG_FMT "pipeline detected", LOG_ID(ctx));
		ctx->client.is_pipelining = 1;
	}

	return ctx->client.is_pipelining;
}

void
client_write(SmtpCtx *ctx, Buffer *buffer)
{
	TRACE_CTX(ctx, 000);

	/* Always detect pipelining. */
	(void) client_pipelining(ctx);

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
	if (opt_rfc2920_pipelining_reject.value && ctx->client.is_pipelining
	&& ctx->state != SMTP_NAME(data) && ctx->state != SMTP_NAME(content)
	&& (!opt_rfc2920_pipelining.value || ctx->state_helo != SMTP_NAME(ehlo))) {
		ctx->reply.length = snprintf(ctx->reply.data, ctx->reply.size, fmt_pipeline, opt_smtp_error_url.string);
		buffer = &ctx->reply;
	}

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "< %ld:%.60s", LOG_ID(ctx), buffer->length, buffer->data);

	if (socket3_write(ctx->client.socket, (unsigned char *)buffer->data, buffer->length, NULL) != buffer->length) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		ctx->client.dropped = DROP_WRITE;
	}

	if (ctx->client.dropped)
		SIGLONGJMP(ctx->on_error, JMP_DROP);
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

	LUA_CALL_SETJMP(reply);

	if (opt_test.value)
		fputs(ctx->reply.data, stdout);
	else
		client_write(ctx, &ctx->reply);

	if (ctx->reply.size <= ctx->reply.length)
		SIGLONGJMP(ctx->on_error, JMP_ERROR);

	ctx->reply.length = 0;
}

static EVENT_DEF(client_io);

SMTP_DEF(quit)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (!opt_test.value && ctx->input.size != ctx->pipe.length - ctx->pipe.offset) {
		ctx->input.data[STRLEN("QUIT")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_REJECT, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	LUA_PT_CALL0(quit);

	client_send(ctx, fmt_quit, my_host_name, ctx->id_sess);

	if (opt_test.value)
		eventsStop(ctx->client.loop);

	SIGLONGJMP(ctx->on_error, JMP_DROP);

	PT_END(&ctx->pt);
}

SMTP_DEF(accept)
{
	PDQ_rr *rr;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	ctx->state = SMTP_NAME(accept);

	ctx->reply.length = 0;
	ctx->smtp_rc = rate_client(ctx);
	if (!SMTP_IS_OK(ctx->smtp_rc))
		goto error1;

	/* Preset by XCLIENT? */
	if (0 < ctx->host.length)
		goto error2;

	/* Copy the IP address into the host name in case there is no PTR. */
	ctx->host.data[0] = '[';
	ctx->host.length = TextCopy(ctx->host.data+1, ctx->host.size-2, ctx->addr.data)+1;
	ctx->host.data[ctx->host.length++] = ']';
	ctx->host.data[ctx->host.length] = '\0';

	if (dns_open(loop, event))
		goto error2;

	if (pdqQuery(ctx->pdq.pdq, PDQ_CLASS_IN, PDQ_TYPE_PTR, ctx->addr.data, NULL)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error3;
	}

	eventSetEnabled(&ctx->pdq.event, 1);
	PT_YIELD_UNTIL(&ctx->pt, dns_wait(ctx, 1) != EAGAIN);

	for (rr = ctx->pdq.answer; rr != NULL; rr = rr->next) {
		if (rr->section == PDQ_SECTION_QUERY)
			continue;
		if (rr->type == PDQ_TYPE_PTR)
			break;
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

#ifdef STRIP_ROOT_DOT
	/* Wait to remove the trailing dot for the root domain from
	 * the client's host name until after any multihomed PTR list
	 * is reviewed above.
	 */
	if (0 < ctx->host.length && ctx->host.data[ctx->host.length-1] == '.')
		ctx->host.data[--ctx->host.length] = '\0';
#endif
	TextLower(ctx->host.data, ctx->host.length);
error3:
	dns_close(loop, event);
error2:
	(void) client_pipelining(ctx);
	LUA_PT_CALL(accept);
error1:
	client_send(ctx, fmt_welcome, my_host_name, ctx->id_sess);

	/* Both normal and +test mode use this handler. The stdin bootstrap
	 * code disables this handler to prevent command line piped input
	 * from being read until after the banner has been written and the
	 * initial state completely setup.
	 */
	eventSetCbIo(event, EVENT_NAME(client_io));

	ctx->pipe.length = 0;

	PT_END(&ctx->pt);
}

SMTP_DEF(interpret)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	LUA_PROTECT_STATE(interpret);

	PT_END(&ctx->pt);
}

SMTP_DEF(unknown)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	LUA_PROTECT_STATE(unknown);
	client_send(ctx, fmt_unknown, opt_smtp_error_url.string, ctx->input.data);

	PT_END(&ctx->pt);
}

SMTP_DEF(out_seq)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	LUA_PROTECT_STATE(out_seq);
	client_send(ctx, fmt_out_seq, opt_smtp_error_url.string, ctx->input.data);

	PT_END(&ctx->pt);
}

SMTP_DEF(helo)
{
	int span;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	PT_BEGIN(&ctx->pt);

	span  = STRLEN("EHLO");
	span += strspn(ctx->input.data+span, " \t");
	if (ctx->input.data[span] == '\0') {
		client_send(ctx, fmt_missing_arg, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	ctx->state = SMTP_NAME(helo);
	ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, ctx->input.data+span);
	trim_buffer(&ctx->helo);

	LUA_PT_CALL(helo);

	client_send(ctx, fmt_helo, ctx->helo.data, ctx->addr.data, ctx->host.data);

	if (LUA_HOOK_OK(ctx->smtp_rc)) {
		ctx->state_helo = SMTP_NAME(helo);
		client_reset(ctx);
	} else {
		ctx->state = SMTP_NAME(accept);
	}

	PT_END(&ctx->pt);
}

SMTP_DEF(ehlo)
{
	int span;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	PT_BEGIN(&ctx->pt);

	if (!opt_test.value
	&& (ctx->input.size != ctx->pipe.length - ctx->pipe.offset
	|| socket3_has_input(ctx->client.socket, SMTP_PIPELINING_TIMEOUT))) {
		ctx->input.data[STRLEN("EHLO")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_REJECT, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	span  = STRLEN("EHLO");
	span += strspn(ctx->input.data+span, " \t");
	if (ctx->input.data[span] == '\0') {
		client_send(ctx, fmt_missing_arg, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	ctx->state = SMTP_NAME(ehlo);
	ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, ctx->input.data+span);
	trim_buffer(&ctx->helo);

	LUA_PT_CALL(ehlo);

	client_send(
		ctx, fmt_ehlo, ctx->helo.data, ctx->addr.data, ctx->host.data,
		opt_smtp_xclient.value ? "250-XCLIENT ADDR HELO NAME PROTO" CRLF : "",
		opt_rfc2920_pipelining.value ? "250-PIPELINING" CRLF : "",
		opt_smtp_max_size.value
	);

	if (LUA_HOOK_OK(ctx->smtp_rc)) {
		ctx->state_helo = SMTP_NAME(ehlo);
		client_reset(ctx);
	} else {
		ctx->state = SMTP_NAME(accept);
	}

	PT_END(&ctx->pt);
}

SMTP_DEF(auth)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_NAME(ehlo)) {
		PT_INIT(&ctx->pt);
		return SMTP_DO(unknown);
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

	ctx->state = SMTP_NAME(auth);
	LUA_PT_CALL(auth);
	ctx->state = ctx->state_helo;

	client_send(ctx, fmt_auth_ok);

	PT_END(&ctx->pt);
}

SMTP_DEF(mail)
{
	int span;
	MimeHooks *hook;
	const char *error;
	char *sender, *param;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_NAME(helo) && ctx->state != SMTP_NAME(ehlo)) {
		PT_INIT(&ctx->pt);
		return SMTP_DO(out_seq);
	}

	/* Find the end of the "MAIL FROM:" string. */
	if ((sender = strchr(ctx->input.data+5, ':')) == NULL) {
		client_send(ctx, fmt_syntax, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}
	sender++;

	span  = strspn(sender, " \t");
	if (sender[span] == '\0') {
		client_send(ctx, fmt_missing_arg, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}
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
			SIGLONGJMP(ctx->on_error, JMP_ERROR);
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

	if ((ctx->mime = mimeCreate()) == NULL) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
	}

	if ((hook = uri_mime_init(ctx)) == NULL) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
	}
	mimeHooksAdd(ctx->mime, hook);

	if ((hook = md5_mime_init(ctx)) == NULL) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
	}
	mimeHooksAdd(ctx->mime, hook);

	next_transaction(ctx);
	ctx->state = SMTP_NAME(mail);
	LUA_PT_CALL(mail);

	client_send(ctx, fmt_mail_ok, ctx->sender->address.string);

	if (!LUA_HOOK_OK(ctx->smtp_rc))
		ctx->state = ctx->state_helo;

	PT_END(&ctx->pt);
}

SMTP_DEF(rcpt)
{
	int span;
	ParsePath *rcpt;
	char *recipient;
	const char *error;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_NAME(mail) && ctx->state != SMTP_NAME(rcpt)) {
		PT_INIT(&ctx->pt);
		return SMTP_DO(out_seq);
	}

	/* Find the end of the "RCPT TO:" string. */
	if ((recipient = strchr(ctx->input.data+5, ':')) == NULL) {
		client_send(ctx, fmt_syntax, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}
	recipient++;

	span  = strspn(recipient, " \t");
	if (recipient[span] == '\0') {
		client_send(ctx, fmt_missing_arg, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}
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
			SIGLONGJMP(ctx->on_error, JMP_ERROR);
		}
		PT_EXIT(&ctx->pt);
	}
	if (rcpt->address.length == 0) {
		client_send(ctx, fmt_rcpt_null, opt_smtp_error_url.string);
		free(rcpt);
		PT_EXIT(&ctx->pt);
	}

	recipient = strdup(rcpt->address.string);
	free(rcpt);

	if (recipient == NULL || VectorAdd(ctx->rcpts, recipient)) {
		syslog(LOG_ERR, log_oom, LOG_INT(ctx));
		SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
	}

	ctx->rcpt = &recipient;
	ctx->state = SMTP_NAME(rcpt);

	LUA_PT_CALL(rcpt);

	recipient = VectorGet(ctx->rcpts, VectorLength(ctx->rcpts)-1);
	if (!LUA_HOOK_OK(ctx->smtp_rc))
		VectorRemove(ctx->rcpts, VectorLength(ctx->rcpts)-1);
	client_send(ctx, fmt_rcpt_ok, recipient);

	PT_END(&ctx->pt);
}

SMTP_DEF(data)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->state != SMTP_NAME(rcpt)) {
		PT_INIT(&ctx->pt);
		return SMTP_DO(out_seq);
	}

	if (VectorLength(ctx->rcpts) == 0) {
		if (ctx->input.size != ctx->pipe.length - ctx->pipe.offset) {
			ctx->input.data[STRLEN("DATA")] = '\0';
			client_send(ctx, fmt_no_piping, SMTP_TRANSACTION_FAILED, opt_smtp_error_url.string, ctx->input.data);
			ctx->pipe.length = 0;
		} else {
			client_send(ctx, fmt_no_rcpts, opt_smtp_error_url.string);
		}
		PT_EXIT(&ctx->pt);
	}

	if (*opt_spool_dir.string != '\0') {
		ctx->path.length = snprintf(ctx->path.data, ctx->path.size, "%s/%s", opt_spool_dir.string, ctx->id_trans);
		if ((ctx->spool_fp = fopen(ctx->path.data, "wb")) == NULL) {
			syslog(LOG_ERR, log_internal, LOG_INT(ctx));
			SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
		}
	}

	ctx->state = SMTP_NAME(data);
	LUA_PT_CALL(data);

	client_send(ctx, fmt_data);

	if (ctx->smtp_rc == 0 || ctx->smtp_rc == SMTP_WAITING)
		eventSetTimeout(event, opt_smtp_data_timeout.value);
	else
		ctx->state = SMTP_NAME(rcpt);

	ctx->length = 0;
	ctx->eoh = 0;

	PT_END(&ctx->pt);
}

static void
update_message(SmtpCtx *ctx)
{
	size_t nbytes;
	FILE *tfp, *sfp;
	char tmp[PATH_MAX], **hdr, buffer[SMTP_TEXT_LINE_LENGTH];

	if (*opt_spool_dir.string == '\0')
		return;

	if ((sfp = fopen(ctx->path.data, "rb")) == NULL) {
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));
		return;
	}

	/* Jump the original headers to the body in the spool file. */
	if (fseek(sfp, ctx->eoh, SEEK_SET)) {
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));
		goto error1;
	}

	(void) snprintf(tmp, sizeof (tmp), "%s/%s.tmp", opt_spool_dir.string, ctx->id_trans);
	if ((tfp = fopen(tmp, "wb")) == NULL) {
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));
		goto error2;
	}

	/* Write the modified headers to the temporary file. */
	for (hdr = (char **) VectorBase(ctx->headers); *hdr != NULL; hdr++) {
#ifdef FOLD_HEADERS
		char *word;
		int span, length;

		for (word = *hdr; *word != '\0'; word += span) {
			/* Start of next word. */
			span = strcspn(word, " \t");
			span += strspn(word+span, " \t");

			/* Fold line? */
			if (72 < length + span) {
				fputs(CRLF "    ", tfp);
				length = 4;
			}

			/* Write the word. */
			length += fprintf(tfp, "%.*s", span,  word);
		}
		fputs(CRLF, tfp);
#else
		fputs(*hdr, tfp);
		fputs(CRLF, tfp);
#endif
	}

	/* End-of-header marker. */
	fputs(CRLF, tfp);

	/* Copy the body from the spool file to the temporary. */
	while (0 < (nbytes = fread(buffer, 1, sizeof (buffer), sfp))) {
		if (fwrite(buffer, 1, nbytes, tfp) != nbytes) {
			syslog(LOG_ERR, log_internal, LOG_INT(ctx));
			goto error3;
		}
	}

	if (unlink(ctx->path.data)) {
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));
		goto error3;
	}

	/* Up to here, a file error can be ignored and the original
	 * spool file used, albeit without modifed headers. However
	 * failure to link the temporary file to the spool file name
	 * leaves us with no orginal nor updated spool, in which case
	 * temporarily fail the mail transaction.
	 */
	if (link(tmp, ctx->path.data)) {
		(void) unlink(tmp);
		syslog(LOG_ERR, log_internal, LOG_INT(ctx));
		SIGLONGJMP(ctx->on_error, JMP_INTERNAL);
	}
error3:
	(void) unlink(tmp);
error2:
	(void) fclose(tfp);
error1:
	(void) fclose(sfp);
}

SMTP_DEF(content)
{
	const char *fmt, *nl;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	PT_BEGIN(&ctx->pt);

	ctx->state = SMTP_NAME(content);

	/* Does input starts with end-of-message. */
	ctx->is_dot = 0;
	nl = ctx->input.data;
	if (nl[0] == '.' && nl[1] == '\n') {
		ctx->is_dot = STRLEN(DOT_LF);
		ctx->input.length = 0;
	} else if (nl[0] == '.' && nl[1] == '\r' && nl[2] == '\n') {
		ctx->is_dot = STRLEN(DOT_CRLF);
		ctx->input.length = 0;
	} else {
		/* Feed the input to the MIME parser. */
		for ( ; *nl != '\0'; nl++) {
			/* Check for end-of-message. */
			if (nl[0] == '\n' && nl[1] == '.') {
				if (nl[2] == '\n')
					ctx->is_dot = STRLEN(DOT_LF);
				else if (nl[2] == '\r' && nl[3] == '\n')
					ctx->is_dot = STRLEN(DOT_CRLF);
				else
					continue;

				/* Shorten the input length. */
				ctx->input.length = nl - ctx->input.data + 1;
				break;
			}

			mimeNextCh(ctx->mime, *nl);
		}
	}

	/* Update the input size for pipeline handling. */
	ctx->input.size = ctx->input.length + ctx->is_dot;

	if (ctx->spool_fp != NULL)
		(void) fwrite(ctx->input.data, ctx->input.length, 1, ctx->spool_fp);

	LUA_PT_CALL(content);

	/* Advance the pipe to the next chunk or command.
	 * We do this now in case we return for more headers,
	 * body content, or pipeline commands (see below).
	 */
	ctx->pipe.offset += ctx->input.size;

	if (ctx->eoh == 0) {
		/* Process headers line by line. */
		while (ctx->eoh == 0 && ctx->input.offset < ctx->input.length) {
			LUA_PT_CALL(header);
		}

		/* More input needed. */
		if (ctx->eoh == 0 && !ctx->is_dot)
			PT_EXIT(&ctx->pt);

		/* End of headers */
		LUA_PT_CALL0(eoh);
	}

	/* Process body line by line. */
	while (ctx->input.offset < ctx->input.length) {
		LUA_PT_CALL(body);
	}

	if (!ctx->is_dot)
		PT_EXIT(&ctx->pt);

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "> %d:%s", LOG_ID(ctx), ctx->is_dot, ctx->input.data+ctx->input.length);

	if (ctx->spool_fp != NULL) {
		(void) fclose(ctx->spool_fp);
		ctx->spool_fp = NULL;
	}

	mimeNextCh(ctx->mime, EOF);

	if (ctx->mime->mime_message_length == 0) {
		/* RFC 2920 section 3.2 point 4 states
		 * we must not deliver empty messages.
		 */
		fmt = fmt_msg_empty;
		goto empty_message;
	}

	LUA_PT_CALL(dot);

	if (LUA_HOOK_OK(ctx->smtp_rc)) {
		update_message(ctx);
		LUA_PT_CALL(forward);

		if (LUA_HOOK_DEFAULT(ctx->smtp_rc)
		&& *opt_smtp_smart_host.string != '\0' && 0 < ctx->path.length) {
			PT_SPAWN(
				&ctx->pt, &ctx->mx.pt,
				mx_send(
					ctx, smart_hosts,
					ctx->sender->address.string,
					ctx->rcpts, ctx->path.data, 0
				)
			);

			/*** TODO: check failed recipients and
			 *** generate a DSN to the sender.
			 ***/

			/* Update the transaction's result based on
			 * the result of forwarding the message.
			 */
			ctx->smtp_rc = ctx->mx.read.smtp_rc;

			if (SMTP_IS_OK(ctx->mx.read.smtp_rc))
				fmt = fmt_msg_ok;
			else if (SMTP_IS_TEMP(ctx->mx.read.smtp_rc))
				fmt = fmt_msg_try_again;
			else
				fmt = fmt_msg_reject;
empty_message:
			ctx->reply.length = snprintf(
				ctx->reply.data, ctx->reply.size,
				fmt, opt_smtp_error_url.string, ctx->id_trans
			);
		}
	}

	client_send(ctx, smtp_default_at_dot, opt_smtp_error_url.string, ctx->id_trans);
	eventSetTimeout(event, opt_smtp_command_timeout.value);
	client_reset(ctx);

	/* When there is input remaining, it is possibly a
	 * pipelined QUIT or the start of another transaction
	 * and we need to process the commands in the pipe
	 * before we return and resume the IO event loop.
	 */
	if (0 < ctx->pipe.offset && ctx->pipe.offset < ctx->pipe.length)
		eventDoIo(EVENT_NAME(client_io), loop, event, EVENT_READ);

	PT_END(&ctx->pt);
}

SMTP_DEF(rset)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	PT_BEGIN(&ctx->pt);

	LUA_PROTECT_STATE0(rset);

	client_send(ctx, fmt_ok);
	client_reset(ctx);

	PT_END(&ctx->pt);
}

SMTP_DEF(noop)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (!opt_test.value
	&& (ctx->input.size != ctx->pipe.length - ctx->pipe.offset
	|| socket3_has_input(ctx->client.socket, SMTP_PIPELINING_TIMEOUT))) {
		ctx->input.data[STRLEN("NOOP")] = '\0';
		client_send(ctx, fmt_no_piping, SMTP_REJECT, opt_smtp_error_url.string, ctx->input.data);
		ctx->pipe.length = 0;
		PT_EXIT(&ctx->pt);
	}

	LUA_PROTECT_STATE0(noop);

	client_send(ctx, fmt_ok);

	PT_END(&ctx->pt);
}

SMTP_DEF(help)
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
		buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, CRLF);
	buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, prefix);

	cols = 0;
	for (opt = verb_table; *opt != NULL; opt++) {
		o = *opt;

		if (LINE_WRAP <= cols % LINE_WRAP + strlen(o->name) + 2) {
			buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, CRLF);
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

	buf->length += TextCopy(buf->data+buf->length, buf->size-buf->length, CRLF);
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

SMTP_DEF(verb)
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

	/* The ctx->reply has already been prepared. */
	ctx->smtp_rc = SMTP_HELP;
	client_send(ctx, "");

	PT_END(&ctx->pt);
}

SMTP_DEF(xclient)
{
	Vector args;
	const char **list, *value;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

#ifdef XCLIENT_REQUIRE_EHLO
	if (ctx->state != ctx->state_helo) {
		PT_INIT(&ctx->pt);
		return SMTP_DO(out_seq);
	}
#endif
	if (ctx->state != NULL) {
		LUA_PT_CALL0(xclient);
	}
	if (!LUA_HOOK_OK(ctx->smtp_rc)) {
		PT_INIT(&ctx->pt);
		return SMTP_DO(out_seq);
	}

	args = TextSplit(ctx->input.data+STRLEN("XCLIENT "), " "CRLF, 0);
	for (list = (const char **) VectorBase(args);  *list != NULL; list++) {
		if (0 <= TextInsensitiveStartsWith(*list, "NAME=")) {
			value = *list + STRLEN("NAME=");
			if (TextInsensitiveCompare(value, "[UNAVAILABLE]") == 0) {
				;
			} else if (TextInsensitiveCompare(value, "[TEMPUNAVAIL]") == 0) {
				;
			} else {
				ctx->host.length = TextCopy(ctx->host.data, ctx->host.size, value);
#ifdef STRIP_ROOT_DOT
				if (0 < ctx->host.length && ctx->host.data[ctx->host.length-1] == '.')
					ctx->host.data[--ctx->host.length] = '\0';
#endif
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
				ctx->state_helo = SMTP_NAME(helo);
			} else if (TextInsensitiveCompare(value, "ESMTP") == 0) {
				ctx->state_helo = SMTP_NAME(ehlo);
			}
			continue;
		}

		client_send(ctx, fmt_bad_args, opt_smtp_error_url.string, *list);
		VectorDestroy(args);
		PT_EXIT(&ctx->pt);
	}

	VectorDestroy(args);
	PT_INIT(&ctx->pt);
	return SMTP_DO(accept);

	PT_END(&ctx->pt);
}

static Command smtp_cmd_table[] = {
	{ "HELO", SMTP_NAME(helo) },
	{ "EHLO", SMTP_NAME(ehlo) },
	{ "AUTH", SMTP_NAME(auth) },
	{ "MAIL", SMTP_NAME(mail) },
	{ "RCPT", SMTP_NAME(rcpt) },
	{ "DATA", SMTP_NAME(data) },
	{ "RSET", SMTP_NAME(rset) },
	{ "NOOP", SMTP_NAME(noop) },
	{ "QUIT", SMTP_NAME(quit) },
	{ "HELP", SMTP_NAME(help) },
	{ "VERB", SMTP_NAME(verb) },
	{ "XCLIENT", SMTP_NAME(xclient) },
	{ NULL, NULL }
};

/***********************************************************************
 *** Client IO Event Callbacks
 ***********************************************************************/

#define ENDS_WITH(s)	(STRLEN(s) <= ctx->pipe.length && strcasecmp(ctx->pipe.data+ctx->pipe.length-STRLEN(s), s) == 0)

#define CRLF_DOT_CRLF		CRLF DOT_CRLF
#define LF_DOT_LF		LF DOT_LF

SmtpCtx *
client_event_new(void)
{
	SmtpCtx *ctx;

	if ((ctx = calloc(1, SMTP_CTX_SIZE)) == NULL)
		goto error0;

	ctx->transaction_count = (int) RAND_MSG_COUNT;

	ctx->path.size = PATH_MAX;
	ctx->addr.size = SMTP_DOMAIN_LENGTH+1;
	ctx->host.size = SMTP_DOMAIN_LENGTH+1;
	ctx->helo.size = SMTP_DOMAIN_LENGTH+1;
	ctx->auth.size = SMTP_DOMAIN_LENGTH+1;
	ctx->work.size = SMTP_TEXT_LINE_LENGTH+1;
	ctx->reply.size = SMTP_TEXT_LINE_LENGTH+1;
	ctx->pipe.size = SMTP_MINIMUM_MESSAGE_LENGTH;

	ctx->path.data = (char *) &ctx[1];
	ctx->addr.data = &ctx->path.data[ctx->path.size];
	ctx->host.data = &ctx->addr.data[ctx->addr.size];
	ctx->helo.data = &ctx->host.data[ctx->host.size];
	ctx->auth.data = &ctx->helo.data[ctx->helo.size];
	ctx->work.data = &ctx->auth.data[ctx->auth.size];
	ctx->reply.data = &ctx->work.data[ctx->work.size];
	ctx->pipe.data = &ctx->reply.data[ctx->reply.size];

	if ((ctx->rcpts = VectorCreate(10)) == NULL)
		goto error1;
	VectorSetDestroyEntry(ctx->rcpts, free);

	if ((ctx->headers = VectorCreate(10)) == NULL)
		goto error1;
	VectorSetDestroyEntry(ctx->headers, free);

	return ctx;
error1:
	free(ctx);
error0:
	return NULL;
}

void
client_event_free(void *_event)
{
	SmtpCtx *ctx;

	if (_event != NULL) {
		ctx = ((Event *)_event)->data;
		TRACE_CTX(ctx, 000);

		*ctx->id_trans = '\0';
		LUA_CALL_SETJMP(close);
		lua_close(ctx->script);

		dns_close(ctx->client.loop, &ctx->client.event);
//		service_close_all(&ctx->services);
		if (0 < ctx->client.socket)
			socket3_close(ctx->client.socket);
		VectorDestroy(ctx->headers);
		VectorDestroy(ctx->rcpts);
		mimeFree(ctx->mime);
		free(ctx->sender);
		free(ctx);
	}
}

EVENT_DEF(client_close)
{
	Event *event = eventGetBase(_ev);
	SmtpCtx *ctx = event->data;
	LUA_CALL_SETJMP(error);
	eventRemove(loop, event);
}

EVENT_DEF(client_io)
{
	Event *event = eventGetBase(_ev);
	JmpCode jc;
	long nbytes;
	lua_State *L;
	Command *entry;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	eventResetTimeout(event);

	SETJMP_PUSH(&ctx->on_error);
	if ((jc = SIGSETJMP(ctx->on_error, 1)) != JMP_SET)
		goto setjmp_pop;

	ctx->smtp_rc = 0;
	ctx->client.loop = loop;

	/* Read the SMTP command line or DATA input. */
	if (ctx->pipe.offset <= 0 || ctx->pipe.length <= ctx->pipe.offset) {
		if (ctx->pipe.length <= ctx->pipe.offset)
			ctx->pipe.length = ctx->pipe.offset = 0;

		if (!opt_daemon.value) {
			fflush(stderr);
			fflush(stdout);
		}
		if (opt_test.value) {
			nbytes = read(
				0, ctx->pipe.data+ctx->pipe.length,
				ctx->pipe.size-1-ctx->pipe.length
			);
		} else {
			nbytes = socket3_read(
				event->fd,
				(unsigned char *)ctx->pipe.data+ctx->pipe.length,
				ctx->pipe.size-1-ctx->pipe.length,
				NULL
			);
		}

		/* EOF or error? */
		if (nbytes <= 0)
			SIGLONGJMP(ctx->on_error, JMP_ERROR);

		ctx->pipe.length += nbytes;
		ctx->pipe.data[ctx->pipe.length] = '\0';
	}

	if (ctx->state == SMTP_NAME(data) || ctx->state == SMTP_NAME(content)) {
piped_test_data:
		/* Set input to be remainder of pipe buffer. This
		 * might be adjusted downwards while processing
		 * the content when the end-of-message is seen.
		 */
		ctx->input.offset = 0;
		ctx->input.data = ctx->pipe.data + ctx->pipe.offset;
		ctx->input.length = ctx->pipe.length - ctx->pipe.offset;
		ctx->input.size = ctx->input.length;

		PT_INIT(&ctx->pt);
		(void) SMTP_DO(content);

		goto setjmp_pop;
	}

	/* Wait for a complete line unit. */
	else if (ctx->pipe.data[ctx->pipe.length-1] != '\n') {
		goto setjmp_pop;
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

		L = ctx->script;
		lua_getglobal(L, "client");
		if (lua_istable(L, -1)) {
			lua_pushlstring(L, ctx->input.data, ctx->input.length);
			lua_setfield(L, -2, "input");

			lua_pushboolean(L, ctx->client.is_pipelining);		/* fn client flag */
			lua_setfield(L, -2, "is_pipelining");			/* fn client */
		}
		lua_pop(L, 1);

		/* Lookup command. */
		PT_INIT(&ctx->pt);
		for (entry = smtp_cmd_table; entry->cmd != NULL; entry++) {
			if (0 < TextInsensitiveStartsWith(ctx->input.data, entry->cmd)) {
				(*entry->hook)(loop, event);

				if (ctx->state == SMTP_NAME(data)) {
					/* Next input chunk in pipe. */
					ctx->pipe.offset += ctx->input.size;

					/*** REALLY BAD FORM TO JUMP BACKWARDS!
					 *** Done so that command line piped
					 *** input can be sent to DATA from
					 *** test scripts.
					 ***/
					goto piped_test_data;
				}
				break;
			}
		}

		if (entry->cmd == NULL) {
			if (opt_test.value)
				(void) SMTP_DO(interpret);
			else
				(void) SMTP_DO(unknown);
		}
	}

	ctx->pipe.length = ctx->pipe.offset = 0;
setjmp_pop:
	SETJMP_POP(&ctx->on_error);
	sigsetjmp_action(ctx, jc);
}

EVENT_DEF(stdin_bootstrap)
{
	Event *event = eventGetBase(_ev);
	SmtpCtx *ctx;

	if ((ctx = client_event_new()) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		return;
	}

	next_session(ctx->id_sess);

	TRACE_CTX(ctx, 000);

	eventSetCbIo(event, NULL);
	eventSetTimeout(event, -1);

	ctx->client.loop = loop;
	ctx->client.socket = event->fd;

	if ((ctx->script = hook_init(ctx)) == NULL) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error1;
	}

	eventInit(&ctx->client.event, ctx->client.socket, EVENT_READ);
	ctx->client.enabled = eventGetEnabled(&ctx->client.event);
	eventSetCbTimer(&ctx->client.event, EVENT_NAME(client_close));
	eventSetCbIo(&ctx->client.event, EVENT_NAME(client_io));
	ctx->client.event.free = client_event_free;
	ctx->client.event.data = ctx;
	ctx->client.loop = loop;

	if (eventAdd(loop, &ctx->client.event)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error2;
	}

	ctx->pipe.length = snprintf(ctx->pipe.data, ctx->pipe.size, " XCLIENT ADDR=127.0.0.1\r\n");
	ctx->pipe.offset = 1;

	eventDoIo(EVENT_NAME(client_io), loop, &ctx->client.event, EVENT_READ);
	return;
error2:
	lua_close(ctx->script);
error1:
	free(ctx);
}

EVENT_DEF(server_io)
{
	Event *event = eventGetBase(_ev);
	SmtpCtx *ctx;
	SOCKET client;
	char id_sess[ID_SIZE];
	SocketAddress caddr;

	eventResetTimeout(event);
	if (errno == ETIMEDOUT)
		return;

	next_session(id_sess);

	if (verb_trace.value)
		syslog(LOG_DEBUG, LOG_FMT "%s", id_sess, __FUNCTION__);

	rate_global();

	if ((client = socket3_accept(event->fd, &caddr)) < 0) {
		if (verb_warn.value)
			syslog(LOG_WARN, log_error, id_sess, LOG_LINE, strerror(errno), errno);
		return;
	}

//	(void) socket3_set_nagle(client, 1);
	(void) socket3_set_linger(client, 0);
	(void) socket3_set_keep_alive(client, 1, -1, -1, -1);
	(void) socket3_set_nonblocking(client, 1);
	(void) fileSetCloseOnExec(client, 1);

	if ((ctx = client_event_new()) == NULL) {
		syslog(LOG_ERR, log_oom, id_sess, LOG_LINE);
		goto error0;
	}

	ctx->client.addr = caddr;
	ctx->client.socket = client;

	if ((ctx->script = hook_init(ctx)) == NULL) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error1;
	}

	eventInit(&ctx->client.event, client, EVENT_READ);
	eventSetTimeout(&ctx->client.event, opt_smtp_command_timeout.value);
	ctx->client.enabled = eventGetEnabled(&ctx->client.event);
	eventSetCbTimer(&ctx->client.event, EVENT_NAME(client_close));
	eventSetCbIo(&ctx->client.event, EVENT_NAME(client_io));
	ctx->client.event.free = client_event_free;
	ctx->client.event.data = ctx;
	ctx->client.loop = loop;

	if (eventAdd(loop, &ctx->client.event)) {
		syslog(LOG_ERR, log_error, LOG_INT(ctx), strerror(errno), errno);
		goto error2;
	}

	TextCopy(ctx->id_sess, sizeof (ctx->id_sess), id_sess);

	socketAddressGetIPv6(&caddr, 0, ctx->ipv6);
	ctx->addr.length = socketAddressGetString(&caddr, 0, ctx->addr.data, ctx->addr.size);

	/* Prime the input pipeline so that we can enter the command
	 * loop to setup the on_error setjmp and then do an XCLIENT
	 * to chain into client accept code and welcome banner.
	 */
	ctx->pipe.length = snprintf(ctx->pipe.data, ctx->pipe.size, " XCLIENT\r\n");
	ctx->pipe.offset = 1;

	eventDoIo(EVENT_NAME(client_io), loop, &ctx->client.event, EVENT_READ);
	return;
error2:
	lua_close(ctx->script);
error1:
	free(ctx);
error0:
	/* Ideally we want to use client_send(), but we don't have a
	 * ctx at this point.
	 */
	socket3_write(client, (unsigned char *)fmt_internal2, sizeof (fmt_internal2)-1, NULL);
	socket3_close(client);
}

/***********************************************************************
 *** Server
 ***********************************************************************/

void
at_exit_cleanup(void)
{
	optionFree(opt_table, NULL);
	VectorDestroy(smart_hosts);
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

	if (opt_test.value) {
		optionString("-daemon", opt_table, NULL);
		optionString("events-wait=poll", opt_table, NULL);
	}

	parse_path_flags = 0;
	if (opt_rfc2821_angle_brackets.value)
		parse_path_flags |= STRICT_SYNTAX;
	if (opt_rfc2821_local_length.value)
		parse_path_flags |= STRICT_LOCAL_LENGTH;
	if (opt_rfc2821_domain_length.value)
		parse_path_flags |= STRICT_DOMAIN_LENGTH;
	if (opt_rfc2821_literal_plus.value)
		parse_path_flags |= STRICT_LITERAL_PLUS;

	/* Convert the smtp-default-at-dot option into the matching default reply. */
	switch (opt_smtp_default_at_dot.value) {
	case SMTP_OK:
		smtp_default_at_dot = fmt_msg_ok;
		break;
	case SMTP_REJECT:
		smtp_default_at_dot = fmt_msg_reject;
		break;
	default:
		smtp_default_at_dot = fmt_msg_try_again;
		break;
	}

	if ((smart_hosts = TextSplit(opt_smtp_smart_host.string, ";, ", 0)) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		exit(EX_SOFTWARE);
	}

	optionString(opt_verbose.string, verb_table, NULL);
}

void
sig_term(int signum)
{
	syslog(LOG_INFO, "signal %d received", signum);
	eventsStop(main_loop);
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

static int
hook_setup(void)
{
#ifdef HOOK_INIT
	int rc = -1;
	lua_State *L;

	if ((L = luaL_newstate()) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		goto error0;
	}

	/* Stop collector during initialization. */
	lua_gc(L, LUA_GCSTOP, 0);
	luaL_openlibs(L);
	lua_gc(L, LUA_GCRESTART, 0);

	lua_define_syslog(L);

	lua_getglobal(L, "syslog");			/* syslog */
	lua_getfield(L, -1, "error");			/* syslog errfn */
	lua_remove(L, -2);				/* errfn */

	switch (luaL_loadfile(L, opt_script.string)) {	/* errfn file */
	case LUA_ERRFILE:
	case LUA_ERRMEM:
	case LUA_ERRSYNTAX:
		syslog(LOG_ERR, "%s", lua_tostring(L, -1));
		goto error1;
	}

	if (lua_pcall(L, 0, LUA_MULTRET, -2)) {		/* errfn file */
		syslog(LOG_ERR, "%s init: %s", opt_script.string, TextNull(lua_tostring(L, -1))); /* errfn errmsg */
		goto error1;
	}

	lua_getglobal(L, "hook");			/* errfn hook */
	lua_getfield(L, -1, "init");			/* errfn hook fn */
	lua_remove(L, -2);				/* errfn fn */

	if (lua_pcall(L, 0, 0, -2)) {			/* errfn fn */
		syslog(LOG_ERR, "%s init: %s", opt_script.string, TextNull(lua_tostring(L, -1))); /* errfn errmsg */
		goto error1;
	}

	rc = 0;
error1:
	lua_close(L);
error0:
	return rc;
#else
	return 0;
#endif
}

int
serverMain(void)
{
	int rc;
	Event event;
	SOCKET socket;
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

	if (socket3_init()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_OSERR;
		goto error0;
	}

	networkGetMyName(my_host_name);

	PDQ_OPTIONS_SETTING(verb_dns.value);
	if (pdqInit()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_OSERR;
		goto error1;
	}

	if (hook_setup()) {
		rc = EX_SOFTWARE;
		goto error2;
	}

	main_loop = eventsNew();
	eventsWaitFnSet(opt_events_wait_fn.string);

	if (opt_test.value) {
		saddr = NULL;
		setvbuf(stdout, NULL, _IOLBF, 0);
		setvbuf(stderr, NULL, _IOLBF, 0);
		eventInit(&event, 0, EVENT_READ);

		/* Boot strap the startup by causing an initial timeout
		 * to simulate a new connection and bring up the banner.
		 */
		eventSetTimeout(&event, 1);
		eventSetCbTimer(&event, EVENT_NAME(stdin_bootstrap));
	} else {
		if ((saddr = socketAddressNew("0.0.0.0", opt_smtp_server_port.value)) == NULL) {
			syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
			rc = EX_SOFTWARE;
			goto error2;
		}
		if ((socket = socket3_server(saddr, 1, opt_smtp_server_queue.value)) < 0) {
			syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
			rc = EX_SOFTWARE;
			goto error3;
		}

		(void) fileSetCloseOnExec(socket, 1);
		(void) socket3_set_nonblocking(socket, 1);
		(void) socket3_set_linger(socket, 0);
		(void) socket3_set_reuse(socket, 1);

		eventInit(&event, socket, EVENT_READ);
		eventSetCbIo(&event, EVENT_NAME(server_io));
//		eventSetTimeout(&event, -1);
	}

	if (eventAdd(main_loop, &event)) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error4;
	}

	eventsRun(main_loop);
	eventsFree(main_loop);
	syslog(LOG_INFO, "terminated");
	rc = EXIT_SUCCESS;
error4:
	if (rc != EXIT_SUCCESS)
		socket3_close(socket);
error3:
	free(saddr);
error2:
	pdqFini();
error1:
error0:
	return rc;
}

int
main(int argc, char **argv)
{
	verboseInit();
	serverOptions(argc, argv);
	if (atexit(at_exit_cleanup))
		exit(EX_SOFTWARE);

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

