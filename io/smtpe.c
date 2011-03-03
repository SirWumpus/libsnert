/*
 * smtpe.c
 *
 * SMTP using IO events; a SocketEvent test.
 *
 * Copyright 2011 by Anthony Howe. All rights reserved.
 */

#define _NAME			"smtpe"
#define _VERSION		"0.1"
#define _COPYRIGHT		LIBSNERT_COPYRIGHT

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

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/util/time62.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct {
	size_t size;
	long length;
	char *data;
} Buffer;

typedef struct stmp_ctx SmtpCtx;
typedef pt_word_t (*SmtpCmdHook)(SocketEvents *loop, SocketEvent *event);

#define SMTP_CMD_HOOK(fn)	cmd_ ## fn
#define SMTP_CMD_DEF(fn)	pt_word_t SMTP_CMD_HOOK(fn)(SocketEvents *loop, SocketEvent *event)
#define SMTP_CMD(fn)		(void)(*cmd_ ## fn)(loop, event)

typedef struct {
	const char *cmd;
	SmtpCmdHook hook;
} Command;

#define ID_SIZE		18

struct stmp_ctx {
	char id_log[ID_SIZE];
	char id_trans[ID_SIZE];
	JMP_BUF on_error;
	ParsePath *sender;
	Vector recipients;
	long mail_size;		/* RFC 1870 */
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	Buffer addr;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer host;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer helo;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer auth;		/* SMTP_DOMAIN_LENGTH+1 */
	Buffer input;		/* SMTP_TEXT_LINE_LENGTH+1 */
	Buffer reply;		/* SMTP_TEXT_LINE_LENGTH+1 */
	Buffer chunk;		/* SMTP_MINIMUM_MESSAGE_LENGTH */

	pt_t pt;		/* Proto-thread state of current command. */
	int is_dot;		/* 0 no trailing dot, else length of "dot" tail */
	SmtpCmdHook state;
	SmtpCmdHook state_helo;

	Socket2 *forward;
	Socket2 *client;
	FILE *spool_fp;
	int is_enabled;

	PDQ *pdq;
	PDQ_rr *pdq_answer;
	Socket2 *pdq_socket;
	SocketEvent pdq_event;
	time_t pdq_stop_after;	/* overall timeout for PDQ lookup */
};

#define SMTP_CTX_SIZE		(sizeof (SmtpCtx) 		\
				+ 4 * (SMTP_DOMAIN_LENGTH+1)	\
				+ 2 * (SMTP_TEXT_LINE_LENGTH+1)	\
				+ SMTP_MINIMUM_MESSAGE_LENGTH)

/***********************************************************************
 *** Strings
 ***********************************************************************/

#define STRLEN(const_str)	(sizeof (const_str)-1)

#define LOG_FMT			"%s "
#define LOG_LINE		__FILE__, __LINE__
#define LOG_ID(ctx)		(ctx)->id_log, LOG_LINE

#define TRACE_FN(n)		if (verb_trace.value) syslog(LOG_DEBUG, "%s", __FUNCTION__)
#define TRACE_CTX(s, n)		if (verb_trace.value) syslog(LOG_DEBUG, LOG_FMT "%s", (s)->id_log, __FUNCTION__)

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
static const char fmt_no_rcpts[] =  "554 5.5.0" FMT(000) "no recipients\r\n";
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

static const char fmt_helo[] = "250 Hello %s (%s)\r\n";

static const char fmt_ehlo[] =
"250-Hello %s (%s)\r\n"
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
Option opt_smtp_smart_host	= { "smtp-smart-host", 		"127.0.0.1:26", 		usage_smtp_smart_host };
Option opt_smtp_xclient		= { "smtp-xclient", 		"-", 				usage_smtp_xclient };
Option opt_spool_dir		= { "spool-dir",		"",				usage_spool_dir };


static const char usage_rfc2920_pipelining[] =
  "Enables support for RFC 2920 SMTP command pipelining when the client\n"
"# sends EHLO. When there is early input before HELO/EHLO, HELO is used,\n"
"# or EHLO PIPELINING has been disabled by this option, earlier talkers\n"
"# are detected and rejected.\n"
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

Option opt_rfc2920_pipelining		= { "rfc2920-pipelining",	"+",		usage_rfc2920_pipelining };
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
	&opt_version,

	&opt_rfc2920_pipelining,
	&opt_rfc2821_angle_brackets,
	&opt_rfc2821_local_length,
	&opt_rfc2821_domain_length,
	&opt_rfc2821_literal_plus,

	&opt_smtp_accept_timeout,
	&opt_smtp_command_timeout,
	&opt_smtp_data_timeout,
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

	&verb_smtp,
#ifdef NOT_USED
	&verb_smtp_data,
	&verb_smtp_dot,
#endif
	NULL
};

static int parse_path_flags;
static SocketEvents main_loop;

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
 * Set the next message-id.
 *
 * The message-id is composed of
 *
 *	ymd HMS ppppp nnnnn
 */
static void
next_id(char buffer[ID_SIZE])
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
		"%05u%05u", getpid(), count
	);
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

	if (*opt_spool_dir.string != '\0') {
		(void) snprintf(ctx->input.data, ctx->input.size, "%s/%s", opt_spool_dir.string, ctx->id_log);
		(void) unlink(ctx->input.data);
		if (ctx->spool_fp != NULL) {
			(void) fclose(ctx->spool_fp);
			ctx->spool_fp = NULL;
		}
	}

	*ctx->id_trans = '\0';
	ctx->input.length = 0;
	ctx->chunk.length = 0;
	ctx->state = ctx->state_helo;
	VectorRemoveAll(ctx->recipients);
	free(ctx->sender);
	ctx->sender = NULL;
}

extern SMTP_CMD_DEF(ehlo);

void
client_send(SmtpCtx *ctx, const char *fmt, ...)
{
	va_list args;

	TRACE_CTX(ctx, 000);

	if (opt_rfc2920_pipelining.value && ctx->state_helo != SMTP_CMD_HOOK(ehlo)) {
		ctx->chunk.length = socketPeek(ctx->client, ctx->chunk.data, ctx->chunk.size-1);
		if (0 < ctx->chunk.length && *ctx->chunk.data != '\r' && *ctx->chunk.data != '\n') {
			ctx->chunk.data[ctx->chunk.length] = '\0';
			if (verb_info.value)
				syslog(LOG_INFO, "pipeline=%ld:%s", ctx->chunk.length, ctx->chunk.data);
			fmt = fmt_pipeline;
		}
	}

	va_start(args, fmt);
	ctx->reply.length = vsnprintf(ctx->reply.data, ctx->reply.size, fmt, args);
	va_end(args);

	if (ctx->reply.size <= ctx->reply.length) {
		syslog(LOG_ERR, log_buffer, LOG_ID(ctx));
		fmt = fmt_buffer;
	}

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "< %ld:%.60s", ctx->id_log, ctx->reply.length, ctx->reply.data);

	if (socketWrite(ctx->client, ctx->reply.data, ctx->reply.length) != ctx->reply.length) {
		syslog(LOG_ERR, log_error, LOG_ID(ctx), strerror(errno), errno);
		SIGLONGJMP(ctx->on_error, 1);
	}
	if (fmt == fmt_pipeline || fmt == fmt_buffer)
		SIGLONGJMP(ctx->on_error, 1);
}

void
dns_io_cb(SocketEvents *loop, SocketEvent *event)
{
	SocketEvent *client_event = event->data;
	SmtpCtx *ctx = client_event->data;

	TRACE_CTX(ctx, 000);
	(*ctx->state)(loop, client_event);
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
			syslog(LOG_DEBUG, LOG_FMT "%s ms=%ld", ctx->id_log, __FUNCTION__, ms);

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
		syslog(LOG_ERR, log_error, LOG_ID(ctx), strerror(errno), errno);
		goto error0;
	}

	/* Create a Socket2 wrapper for socketEventInit(). */
	if ((ctx->pdq_socket = socketFdOpen(pdqGetFd(ctx->pdq))) == NULL) {
		syslog(LOG_ERR, log_error, LOG_ID(ctx), strerror(errno), errno);
		goto error1;
	}

	socketSetTimeout(ctx->pdq_socket, PDQ_TIMEOUT_START * 1000);

	/* Create an event for the DNS lookup. */
	socketEventInit(&ctx->pdq_event, ctx->pdq_socket, SOCKET_EVENT_READ);

	ctx->pdq_event.data = event;
	ctx->pdq_event.on.io = dns_io_cb;
	ctx->pdq_event.on.error = dns_error_cb;

	if (socketEventAdd(loop, &ctx->pdq_event)) {
		syslog(LOG_ERR, log_oom, LOG_ID(ctx));
		goto error2;
	}

	/* Disable the client event until the DNS lookup completes. */
	ctx->is_enabled = socketEventEnable(event, 0);
	ctx->pdq_answer = NULL;

	return 0;
error2:
	/* Don't use socketFdClose() or socketClose(). */
	free(ctx->pdq_socket);
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
		(void) socketEventEnable(event, ctx->is_enabled);
		socketEventRemove(loop, &ctx->pdq_event);
		pdqListFree(ctx->pdq_answer);

		/* Don't use socketFdClose() or socketClose(). pdqClose() will
		 * close the socket file descriptor, so just free the Socket2
		 * wrapper.
		 */
		free(ctx->pdq_socket);
		pdqClose(ctx->pdq);
		ctx->pdq = NULL;
	}
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

	if (dns_open(loop, event))
		goto error1;

	if (pdqQuery(ctx->pdq, PDQ_CLASS_IN, PDQ_TYPE_PTR, ctx->addr.data, NULL)) {
		syslog(LOG_ERR, log_error, LOG_ID(ctx), strerror(errno), errno);
		goto error2;
	}

	dns_reset(ctx);
	PT_YIELD_UNTIL(&ctx->pt, dns_wait(ctx, 1) != EAGAIN);

	for (rr = ctx->pdq_answer; rr != NULL; rr = rr->next) {
		if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_PTR) {
			break;
		}
	}

	if (rr == NULL)
		goto error2;

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
error2:
	dns_close(loop, event);
error1:
	client_send(ctx, fmt_welcome, ctx->host.data, ctx->id_log);

	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(unknown)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
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
	client_send(ctx, fmt_helo, ctx->helo.data, ctx->addr.data);
	ctx->state = ctx->state_helo = SMTP_CMD_HOOK(helo);
	client_reset(ctx);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(ehlo)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, ctx->input.data+5);

	client_send(
		ctx, fmt_ehlo, ctx->helo.data, ctx->addr.data,
		opt_smtp_xclient.value ? "250-XCLIENT ADDR HELO NAME PROTO\r\n" : "",
		opt_rfc2920_pipelining.value ? "250-PIPELINING\r\n" : "",
		opt_smtp_max_size.value
	);

	ctx->state = ctx->state_helo = SMTP_CMD_HOOK(ehlo);
	client_reset(ctx);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(auth)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	if (ctx->state != SMTP_CMD_HOOK(ehlo)) {
		SMTP_CMD(unknown);
		PT_EXIT(&ctx->pt);
	}

	if (0 < ctx->auth.length) {
		client_send(ctx, fmt_auth_already, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	ctx->auth.length = TextCopy(ctx->auth.data, ctx->auth.size, ctx->auth.data+5);
	if (TextInsensitiveStartsWith(ctx->auth.data, "PLAIN") < 0) {
		client_send(ctx, fmt_auth_mech, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

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
		SMTP_CMD(out_seq);
		PT_EXIT(&ctx->pt);
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
			syslog(LOG_ERR, log_internal, LOG_ID(ctx));
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

	client_send(ctx, fmt_mail_ok, ctx->sender->address.string);
	ctx->state = SMTP_CMD_HOOK(mail);
	next_id(ctx->id_trans);
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
		SMTP_CMD(out_seq);
		PT_EXIT(&ctx->pt);
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
			syslog(LOG_ERR, log_internal, LOG_ID(ctx));
			SIGLONGJMP(ctx->on_error, 1);
		}
		PT_EXIT(&ctx->pt);
	}
	if (rcpt->address.length == 0) {
		client_send(ctx, fmt_rcpt_null, opt_smtp_error_url.string);
		free(rcpt);
		PT_EXIT(&ctx->pt);
	}
	if (VectorAdd(ctx->recipients, rcpt)) {
		client_send(ctx, fmt_internal, opt_smtp_error_url.string, strerror(errno));
		syslog(LOG_ERR, log_oom, LOG_ID(ctx));
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
		SMTP_CMD(out_seq);
		PT_EXIT(&ctx->pt);
	}
	if (VectorLength(ctx->recipients) == 0) {
		client_send(ctx, fmt_no_rcpts, opt_smtp_error_url.string);
		PT_EXIT(&ctx->pt);
	}

	if (*opt_spool_dir.string != '\0') {
		(void) snprintf(ctx->input.data, ctx->input.size, "%s/%s", opt_spool_dir.string, ctx->id_log);
		if ((ctx->spool_fp = fopen(ctx->input.data, "w")) == NULL) {
			client_send(ctx, fmt_internal, opt_smtp_error_url.string, strerror(errno));
			syslog(LOG_ERR, log_internal, LOG_ID(ctx));
			SIGLONGJMP(ctx->on_error, 1);
		}
	}

	client_send(ctx, fmt_data);
	ctx->state = SMTP_CMD_HOOK(data);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(content)
{
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);

	if (ctx->spool_fp != NULL)
		(void) fwrite(ctx->chunk.data, ctx->chunk.length-ctx->is_dot, 1, ctx->spool_fp);

	if (ctx->is_dot) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "> %d:%s", ctx->id_log, ctx->is_dot, ctx->chunk.data+ctx->chunk.length-ctx->is_dot);

		if (ctx->spool_fp != NULL) {
			(void) fclose(ctx->spool_fp);
			ctx->spool_fp = NULL;
		}

		/*** for testing only ***/
//		client_send(ctx, fmt_try_again, opt_smtp_error_url.string);
		client_send(ctx, fmt_msg_ok, ctx->id_trans);

		client_reset(ctx);
	}

	ctx->chunk.length = 0;
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(rset)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	client_send(ctx, fmt_ok);
	client_reset(ctx);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(noop)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	client_send(ctx, fmt_ok);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(quit)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
	client_send(ctx, fmt_quit, ctx->host.data, ctx->id_log);
	SIGLONGJMP(ctx->on_error, 1);
	PT_END(&ctx->pt);
}

SMTP_CMD_DEF(help)
{
	SmtpCtx *ctx = event->data;
	TRACE_CTX(ctx, 000);
	PT_BEGIN(&ctx->pt);
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
	static Buffer usage = { sizeof (buffer), 0, buffer };

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
		syslog(LOG_ERR, log_error, LOG_ID(ctx), strerror(errno), errno);
		SIGLONGJMP(ctx->on_error, 1);
	}
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
		SMTP_CMD(out_seq);
		PT_EXIT(&ctx->pt);
	}

	args = TextSplit(ctx->input.data+sizeof ("XCLIENT ")-1, " ", 0);
	for (list = (const char **) VectorBase(args);  *list != NULL; list++) {
		if (0 <= TextInsensitiveStartsWith(*list, "NAME=")) {
			value = *list + sizeof ("NAME=")-1;
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
			value = *list + sizeof ("ADDR=")-1;
			if (0 < parseIPv6(value, ctx->ipv6)) {
				ctx->addr.length = TextCopy(ctx->addr.data, ctx->addr.size, value);
				continue;
			}
		} else if (0 <= TextInsensitiveStartsWith(*list, "HELO=")) {
			value = *list + sizeof ("HELO=")-1;
			ctx->helo.length = TextCopy(ctx->helo.data, ctx->helo.size, value);
			continue;
		} else if (0 <= TextInsensitiveStartsWith(*list, "PROTO=")) {
			value = *list + sizeof ("PROTO=")-1;
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

	client_send(ctx, fmt_welcome, ctx->host.data, ctx->id_log);

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

#define ENDS_WITH(s)	(STRLEN(s) <= ctx->chunk.length && strcasecmp(ctx->chunk.data+ctx->chunk.length-STRLEN(s), s) == 0)
#define DOT_QUIT_CRLF	"\r\n.\r\nQUIT\r\n"
#define DOT_QUIT_LF	"\n.\nQUIT\n"
#define DOT_CRLF	"\r\n.\r\n"
#define DOT_LF		"\n.\n"

void
client_io_cb(SocketEvents *loop, SocketEvent *event)
{
	Command *entry;
	SmtpCtx *ctx = event->data;

	TRACE_CTX(ctx, 000);

	if (SIGSETJMP(ctx->on_error, 1) != 0) {
		if (verb_smtp.value)
			syslog(LOG_DEBUG, LOG_FMT "close %s cc=%ld", ctx->id_log, ctx->addr.data, VectorLength(loop->events)-1);
		if (event->on.error != NULL)
			(*event->on.error)(loop, event);
		return;
	}

	if (ctx->state == SMTP_CMD_HOOK(data)) {
		int is_pipe_quit, is_dot;

		ctx->chunk.length += socketRead(
			event->socket,
			ctx->chunk.data+ctx->chunk.length, ctx->chunk.size-1-ctx->chunk.length
		);
		ctx->chunk.data[ctx->chunk.length] = '\0';

		is_dot = is_pipe_quit = 0;
		if (ENDS_WITH(DOT_QUIT_CRLF))
			is_dot = is_pipe_quit = STRLEN(DOT_QUIT_CRLF);
		else if (ENDS_WITH(DOT_QUIT_LF))
			is_dot = is_pipe_quit = STRLEN(DOT_QUIT_LF);
		else if (ENDS_WITH(DOT_CRLF))
			is_dot = STRLEN(DOT_CRLF);
		else if (ENDS_WITH(DOT_LF))
			is_dot = STRLEN(DOT_CRLF);
		ctx->is_dot = is_dot;

		if (is_dot || ctx->chunk.size <= ctx->chunk.length+SMTP_TEXT_LINE_LENGTH)
			SMTP_CMD(content);
		if (is_pipe_quit)
			SMTP_CMD(quit);
		return;
	}

	/* Read the SMTP command line. */
	ctx->input.length += socketRead(
		event->socket,
		ctx->input.data+ctx->input.length, ctx->input.size-1-ctx->input.length
	);
	ctx->input.data[ctx->input.length] = '\0';

	/* Wait for a complete command line. */
	if (ctx->input.data[ctx->input.length-1] != '\n')
		return;

	trim_buffer(&ctx->input);

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "> %ld:%.60s", ctx->id_log, ctx->input.length, ctx->input.data);

	/* Lookup command. */
	for (entry = smtp_cmd_table; entry->cmd != NULL; entry++) {
		if (0 < TextInsensitiveStartsWith(ctx->input.data, entry->cmd)) {
			PT_INIT(&ctx->pt);
			(*entry->hook)(loop, event);
			ctx->input.length = 0;
			return;
		}
	}

	/* Command unknown. */
	SMTP_CMD(unknown);
	ctx->input.length = 0;
}

void
client_close_cb(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx = event->data;

	if (ctx != NULL) {
		TRACE_CTX(ctx, 000);

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
		syslog(LOG_ERR, log_error, LOG_ID(ctx), strerror(errno), errno);
	socketEventRemove(loop, event);
}

void
server_io_cb(SocketEvents *loop, SocketEvent *event)
{
	SmtpCtx *ctx;
	Socket2 *client;
	SocketEvent *client_event;
	char id_log[ID_SIZE];

	next_id(id_log);
	if (verb_trace.value)
		syslog(LOG_DEBUG, LOG_FMT "%s", id_log, __FUNCTION__);

	if ((client = socketAccept(event->socket)) == NULL) {
		if (verb_warn.value)
			syslog(LOG_WARN, log_error, id_log, LOG_LINE, strerror(errno), errno);
		return;
	}

//	(void) socketSetNagle(client, 0);
	(void) socketSetLinger(client, 0);
	(void) socketSetKeepAlive(client, 1);
	(void) fileSetCloseOnExec(socketGetFd(client), 1);
	socketSetTimeout(client, opt_smtp_command_timeout.value);

	if ((client_event = socketEventAlloc(client, SOCKET_EVENT_READ)) == NULL) {
		syslog(LOG_ERR, log_oom, id_log, LOG_LINE);
		socketClose(client);
		return;
	}
	if ((ctx = calloc(1, SMTP_CTX_SIZE)) == NULL) {
		syslog(LOG_ERR, log_oom, id_log, LOG_LINE);
		socketEventFree(client_event);
		return;
	}

	client_event->data = ctx;
	client_event->on.io = client_io_cb;
	client_event->on.close = client_close_cb;
	client_event->on.error = client_error_cb;

	TextCopy(ctx->id_log, sizeof (ctx->id_log), id_log);

	ctx->client = client;

	ctx->addr.size = SMTP_DOMAIN_LENGTH+1;
	ctx->host.size = SMTP_DOMAIN_LENGTH+1;
	ctx->helo.size = SMTP_DOMAIN_LENGTH+1;
	ctx->auth.size = SMTP_DOMAIN_LENGTH+1;
	ctx->input.size = SMTP_TEXT_LINE_LENGTH+1;
	ctx->reply.size = SMTP_TEXT_LINE_LENGTH+1;
	ctx->chunk.size = SMTP_MINIMUM_MESSAGE_LENGTH;

	ctx->addr.data = (unsigned char *) &ctx[1];
	ctx->host.data = &ctx->addr.data[ctx->addr.size];
	ctx->helo.data = &ctx->host.data[ctx->host.size];
	ctx->auth.data = &ctx->helo.data[ctx->helo.size];
	ctx->input.data = &ctx->auth.data[ctx->auth.size];
	ctx->reply.data = &ctx->input.data[ctx->input.size];
	ctx->chunk.data = &ctx->reply.data[ctx->reply.size];

	if ((ctx->recipients = VectorCreate(10)) == NULL) {
		syslog(LOG_ERR, log_oom, id_log, LOG_LINE);
		socketEventFree(client_event);
		return;
	}
	VectorSetDestroyEntry(ctx->recipients, free);

	socketAddressGetIPv6(&client->address, 0, ctx->ipv6);
	socketAddressGetString(&client->address, 0, ctx->addr.data, ctx->addr.size);

	if (socketEventAdd(loop, client_event)) {
		syslog(LOG_ERR, log_oom, id_log, LOG_LINE);
		socketEventFree(client_event);
		return;
	}

	if (verb_smtp.value)
		syslog(LOG_DEBUG, LOG_FMT "accept %s cc=%ld", ctx->id_log, ctx->addr.data, VectorLength(loop->events)-1);

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

int
serverMain(void)
{
	int rc;
	Socket2 *socket;
	SocketEvent event;
	SocketAddress *saddr;

	TRACE_FN(000);
	syslog(LOG_INFO, _NAME " " _VERSION " " _COPYRIGHT);

	(void) umask(0002);
	(void) signal(SIGPIPE, SIG_IGN);
	(void) signal(SIGINT, sig_term);
	(void) signal(SIGHUP, sig_term);
	(void) signal(SIGQUIT, sig_term);
	(void) signal(SIGTERM, sig_term);

	if (socketInit()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_OSERR;
		goto error0;
	}
	if (pdqInit()) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_OSERR;
		goto error0;
	}
	if ((saddr = socketAddressNew("0.0.0.0", SMTP_PORT)) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error1;
	}
	if ((socket = socketOpen(saddr, 1)) == NULL) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error2;
	}

	(void) fileSetCloseOnExec(socketGetFd(socket), 1);
	(void) socketSetNonBlocking(socket, 1);
	(void) socketSetLinger(socket, 0);
	(void) socketSetReuse(socket, 1);
//	(void) socketSetNagle(socket, 0);

	if (socketServer(socket, opt_smtp_server_queue.value)) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error3;
	}

	socketEventsInit(&main_loop);
	socketEventInit(&event, socket, SOCKET_EVENT_READ);
	if (socketEventAdd(&main_loop, &event)) {
		syslog(LOG_ERR, log_init, LOG_LINE, strerror(errno), errno);
		rc = EX_SOFTWARE;
		goto error3;
	}

	event.on.io = server_io_cb;
	(void) socketSetTimeout(socket, -1);

	socketEventsRun(&main_loop);
	socketEventsFree(&main_loop);
	syslog(LOG_INFO, "terminated");
	rc = EXIT_SUCCESS;
error3:
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

