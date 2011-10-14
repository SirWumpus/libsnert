/*
 * smf.c
 *
 * Sendmail Filter Support
 *
 * Copyright 2004, 2010 by Anthony Howe. All rights reserved.
 */

#define ENABLE_COMBO_TAGS

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define _REENTRANT	1

#include <com/snert/lib/version.h>

#if defined(HAVE_LIBMILTER_MFAPI_H) && defined(HAVE_PTHREAD_CREATE)

#include <com/snert/lib/berkeley_db.h>

#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>

#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif
#if defined(HAVE_NETDB_H) && ! defined(__MINGW32__)
# include <netdb.h>
#endif
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_REGEX_H
# include <regex.h>
#endif
#ifdef HAVE_SQLITE3_H
# include <sqlite3.h>
#endif
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/type/Data.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>
#include <com/snert/lib/util/option.h>
#include <com/snert/lib/util/setBitWord.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/mail/smdb.h>
#include <com/snert/lib/mail/smtp2.h>
#include <com/snert/lib/mail/smf.h>
#include <com/snert/lib/mail/tlds.h>

extern void rlimits(void);

#define	TAG_FORMAT		"%05d %s: "
#define	TAG_ARGS		work->cid, work->qid

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

pthread_mutex_t smfMutex;

long smfLogDetail;
long smfFlags = \
	  SMF_FLAG_STRICT_SYNTAX \
	| SMF_FLAG_REJECT_PERCENT_RELAY \
	| SMF_FLAG_REJECT_RFC2606 \
	| SMF_FLAG_REJECT_UNKNOWN_TLD \
	| SMF_FLAG_SMTP_AUTH_OK
;

const char smfNo[] = "NO";
const char smfYes[] = "YES";
const char smfNoQueue[] = "NOQUEUE";
const char smfUndefined[] = "";
const char * const smfPrecedence[] = {
	"bulk", "junk", "list", "first-class", "special-delivery", NULL
};

#ifdef HAVE_POP_BEFORE_SMTP
/* This macro is apparently only defined during the RCPT
 * handler after the MAIL rulesets have been applied.
 * Requested by Michael Elliott <elliott@rod.msen.com>.
 */

/* const */ char smMacro_popauth_info[] = "{popauth_info}";
#endif

/* const */ char smMacro_auth_ssf[] = "{auth_ssf}";
/* const */ char smMacro_auth_type[] = "{auth_type}";
/* const */ char smMacro_auth_authen[] = "{auth_authen}";
/* const */ char smMacro_auth_author[] = "{auth_author}";
/* const */ char smMacro_client_addr[] = "{client_addr}";
/* const */ char smMacro_client_name[] = "{client_name}";
/* const */ char smMacro_client_resolv[] = "{client_resolv}";
/* const */ char smMacro_if_addr[] = "{if_addr}";
/* const */ char smMacro_if_name[] = "{if_name}";
/* const */ char smMacro_verify[] = "{verify}";

static smfInfo *smfDesc;
static volatile unsigned short smfCount;

/***********************************************************************
 *** Error Handling routines.
 ***********************************************************************/

static struct bitword logBits[] = {
	{ SMF_LOG_ALL,		"all" },
	{ SMF_LOG_WARN,		"warn" },
	{ SMF_LOG_INFO,		"info" },
	{ SMF_LOG_TRACE,	"trace" },
	{ SMF_LOG_PARSE,	"parse" },
	{ SMF_LOG_DEBUG,	"debug" },
	{ SMF_LOG_DIALOG,	"dialog" },
	{ SMF_LOG_STATE,	"state" },
	{ SMF_LOG_DNS,		"dns" },
	{ SMF_LOG_CACHE,	"cache" },
	{ SMF_LOG_DATABASE,	"db" },
	{ SMF_LOG_DATABASE,	"database" },
	{ SMF_LOG_SOCKET_FD,	"socket-fd" },
	{ SMF_LOG_SOCKET_ALL,	"socket-all" },
	{ SMF_LOG_LIBMILTER,	"libmilter" },
	{ 0, NULL }
};

void
smfSetLogDetail(const char *detail)
{
	smfLogDetail = setBitWord(logBits, detail);
	smfOptVerbose.value = smfLogDetail;
}

void
smfLog(int category, const char *fmt, ...)
{
	int level;
	va_list args;
	va_start(args, fmt);

	if (category == SMF_LOG_ERROR || category == SMF_LOG_WARN || (smfLogDetail & category)) {
		switch (category) {
		case SMF_LOG_ERROR:
			level = LOG_ERR; break;
		case SMF_LOG_WARN:
			level = LOG_WARNING; break;
		case SMF_LOG_INFO:
			level = LOG_INFO; break;
		default:
			level = LOG_DEBUG; break;
		}

		vsyslog(level, fmt, args);
	}

	va_end(args);
}

#define USAGE_HELP							\
  "Write the option summary to standard output and exit. The output\n"	\
"# is suitable for use as an option file.\n"				\
"#"


#define USAGE_SOCKET							\
  "The sendmail/milter socket type & name (required):\n"		\
"#\n"									\
"#    {unix|local}:/path/to/file\n"					\
"#    inet:port@{hostname|ip-address}\n"				\
"#    inet6:port@{hostname|ip-address}\n"				\
"#"

#define USAGE_VERBOSE							\
  "A comma separated word list of what to write to the mail log:\n"	\
"#\n"									\
"#        all = all messages\n"						\
"#       info = general info messages\n"				\
"#      trace = trace progress through the milter\n"			\
"#      parse = details from parsing addresses or special strings\n"	\
"#      debug = lots of debug messages\n"				\
"#     dialog = I/O from smtp dialog\n"					\
"#      state = state transitions\n"					\
"#        dns = trace & debug of DNS operations\n"			\
"#      cache = cache get/put/gc operations\n"				\
"#   database = sendmail database lookups\n"				\
"#  socket-fd = socket open & close calls \n"				\
"# socket-all = all socket operations\n"				\
"#  libmilter = libmilter engine diagnostics\n"				\
"#"

#define USAGE_INTERFACE_IP						\
  "One of the IP addresses for this host. When undefined, then the\n"	\
"# IP address will be determined at start-up.\n"			\
"#"

#define USAGE_INTERFACE_NAME						\
  "One of the FQDN for this host. If empty, then the host name will\n"	\
"# be automatically determined at start-up. If interface-ip is \n"	\
"# undefined, then the name specified here or determined at start-up\n"	\
"# will influence the IP address used.\n"				\
"#"

/*
 * Common milter options.
 */
Option smfOptRFC2821Syntax		= { "rfc2821-syntax", 		"+", "Strict RFC 2821 grammar for mail addresses." };
Option smfOptRFC2821LocalLength		= { "rfc2821-local-length", 	"-", "Strict RFC 2821 local-part length limit." };
Option smfOptRFC2821DomainLength	= { "rfc2821-domain-length", 	"-", "Strict RFC 2821 domain name length limit." };
Option smfOptRFC2821LiteralPlus 	= { "rfc2821-literal-plus", 	"-", "Treat plus-sign as itself; not a sendmail plussed address." };
Option smfOptRejectPercentRelay 	= { "reject-percent-relay", 	"+", "Reject occurences of % relay hack in addresses." };
Option smfOptRejectRFC2606		= { "reject-rfc2606", 		"+", "Reject RFC 2606 reserved domains." };
Option smfOptRejectUnknownTLD 		= { "reject-unknown-tld", 	"+", "Reject top-level-domains not listed by IANA." };
Option smfOptSmtpAuthOk			= { "smtp-auth-ok", 		"+", "Assume SMTP authenticated mail is white listed." };

Option smfOptAccessDb		= { "access-db",	"/etc/mail/access.db",	"Path to the access.db file." };
Option smfOptDaemon		= { "daemon",		"+",		"Start as a background daemon or foreground application." };
Option smfOptFile		= { "file", 		"/etc/mail/%s.cf",	"Read option file before command line options." };
Option smfOptHelp		= { "help", 		NULL,		USAGE_HELP };
Option smfOptMilterQueue	= { "milter-queue",	"20",		"The sendmail/milter connection queue size." };
Option smfOptMilterSocket	= { "milter-socket",	"unix:/var/run/milter/%s.socket",	USAGE_SOCKET };
Option smfOptMilterTimeout	= { "milter-timeout",	"7210",		"The sendmail/milter I/O timeout in seconds." };
Option smfOptPidFile 		= { "pid-file", 	"/var/run/milter/%s.pid",	"The file path of where to save the process-id." };
Option smfOptQuit		= { "quit", 		NULL,		"Quit an already running instance and exit." };
Option smfOptRestart		= { "restart", 		NULL,		"Terminate an already running instance before starting." };
Option smfOptRunGroup 		= { "run-group", 	"milter", 	"The process runtime group name to be used when started by root." };
Option smfOptRunUser 		= { "run-user", 	"milter", 	"The process runtime user name to be used when started by root." };
Option smfOptSendmailCf		= { "sendmail-cf",	"/etc/mail/sendmail.cf",	"Path to the sendmail.cf file." };
Option smfOptVerbose		= { "verbose",		"",		USAGE_VERBOSE };
Option smfOptWorkDir 		= { "work-dir", 	"/var/tmp", 	"The working directory of the process." };
Option smfOptInterfaceIp 	= { "interface-ip",	"",	 	USAGE_INTERFACE_IP };
Option smfOptInterfaceName 	= { "interface-name", 	"", 		USAGE_INTERFACE_NAME };

Option *smfOptTable[] = {
	&smfOptAccessDb,
	&smfOptDaemon,
	&smfOptFile,
	&smfOptHelp,
	&smfOptInterfaceIp,
	&smfOptInterfaceName,
	&smfOptMilterQueue,
	&smfOptMilterSocket,
	&smfOptMilterTimeout,
	&smfOptPidFile,
	&smfOptQuit,
	&smfOptRFC2821DomainLength,
	&smfOptRFC2821LiteralPlus,
	&smfOptRFC2821LocalLength,
	&smfOptRFC2821Syntax,
	&smfOptRejectPercentRelay,
	&smfOptRejectRFC2606,
	&smfOptRejectUnknownTLD,
	&smfOptRestart,
	&smfOptRunGroup,
	&smfOptRunUser,
	SMDB_OPTIONS_TABLE,
	&smfOptSmtpAuthOk,
	&tldOptLevelOne,
	&tldOptLevelTwo,
	&smfOptVerbose,
	&smfOptWorkDir,
	NULL
};

typedef struct {
	Option *option;
	long bit;
} OptionFlag;

static OptionFlag optionFlagMapping[] = {
	{ &smfOptSmtpAuthOk, SMF_FLAG_SMTP_AUTH_OK  },
	{ &smfOptRFC2821Syntax, SMF_FLAG_STRICT_SYNTAX },
	{ &smfOptRFC2821LocalLength, SMF_FLAG_STRICT_LOCAL_LENGTH },
	{ &smfOptRFC2821DomainLength, SMF_FLAG_STRICT_DOMAIN_LENGTH },
	{ &smfOptRFC2821LiteralPlus, SMF_FLAG_STRICT_LITERAL_PLUS },
	{ &smfOptRejectPercentRelay, SMF_FLAG_REJECT_PERCENT_RELAY },
	{ &smfOptRejectRFC2606, SMF_FLAG_REJECT_RFC2606 },
	{ &smfOptRejectUnknownTLD, SMF_FLAG_REJECT_UNKNOWN_TLD },
	{ NULL, 0 }
};

/* DEPRICATED */
void
smfSetFlags(const char *flags)
{
	int ac;
	char **av;
	OptionFlag *of;

	if (!TokenSplit(flags, ",", &av, &ac, 1)) {
		(void) optionArrayL(ac, av, smfOptTable, NULL);
		free(av);
	}

	for (of = optionFlagMapping; of->option != NULL; of++) {
		if (of->option->value)
			smfFlags |= of->bit;
		else
			smfFlags &= ~of->bit;
	}
}

sfsistat
smfNullWorkspaceError(const char *where)
{
	/* Without the workspace we can't do a hell of a lot. */
	syslog(LOG_ERR, "%s(): internal error, null workspace", where);

	/* In the case of an internal error, we can reject the message or
	 * accept it. Accepting it would by-pass the milter, but allow the
	 * message to be received and maybe filtered by something else.
	 */
	return SMFIS_ACCEPT;
}

sfsistat
smfReplyV(smfWork *work, int code, const char *ecode, const char *fmt, va_list args)
{
	int length;
	char *reply;
	sfsistat rc;
	char rcode[4], xcode[10];
	unsigned class, subject, detail;

	work->replyLine[0] = '\0';

	if (SMTP_IS_PERM(code))
		rc = SMFIS_REJECT;
	else if (SMTP_IS_TEMP(code))
		rc = SMFIS_TEMPFAIL;
	else
		rc = SMFIS_CONTINUE;

	/* Make sure the return code is present and in an acceptable range. */
	if (!SMTP_IS_VALID(code))
		code = 550;

	/* Convert int value to 3-digit string for smfi_smfReply(). */
	rcode[3] = '\0';
	rcode[2] = '0' + (code / 1   % 10);
	rcode[1] = '0' + (code / 10  % 10);
	rcode[0] = '0' + (code / 100 % 10);

	/* See RFC 3463 Enhanced Mail System Status Codes */
	if (sscanf(fmt, "%1u.%u.%u", &class, &subject, &detail) == 3) {
		length = snprintf(xcode, sizeof (xcode), "%u.%u.%u", class, subject, detail);
		fmt += length + (isspace(fmt[length]) ? 1 : 0);
		ecode = xcode;
	} else if (ecode == NULL) {
		TextCopy(xcode, sizeof (xcode), "x.7.1");
		xcode[0] = rcode[0];
		ecode = xcode;
	}

	/* Build reply message. */
	(void) vsnprintf(work->replyLine, sizeof (work->replyLine), fmt, args);

	/* Remove non-printable characters like CR, which smfi_setreply() fails on. */
	for (reply = work->replyLine; *reply != '\0'; reply++) {
		if (isspace(*reply))
			*reply = ' ';
	}

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "reply %s %s %s", TAG_ARGS, rcode, ecode, work->replyLine);

	/* Tell sendmail what error to report to the sender. */
	if (smfi_setreply(work->ctx, rcode, (char *) ecode, work->replyLine) == MI_FAILURE)
		syslog(LOG_ERR, TAG_FORMAT "smfReplyV(): smfi_setreply() failed: %s (%d)", TAG_ARGS, strerror(errno), errno);

	return rc;
}

sfsistat
smfReply(smfWork *work, int code, const char *ecode, const char *fmt, ...)
{
	sfsistat rc;

	va_list args;
	va_start(args, fmt);

	rc = smfReplyV(work, code, ecode, fmt, args);

	va_end(args);

	return rc;
}

#ifdef HAVE_SMFI_SETMLREPLY

sfsistat
smfMultiLineReplyA(smfWork *work, int code, const char *ecode, char **lines)
{
	sfsistat rc;
	int i, length;
	char rcode[4], xcode[10];
	unsigned class, subject, detail;

	smfLog(SMF_LOG_TRACE, TAG_FORMAT "enter smfMultiLineReplyA()", TAG_ARGS);

	if (SMTP_IS_PERM(code))
		rc = SMFIS_REJECT;
	else if (SMTP_IS_TEMP(code))
		rc = SMFIS_TEMPFAIL;
	else
		rc = SMFIS_CONTINUE;

	/* Make sure the return code is present and in an acceptable range. */
	if (!SMTP_IS_VALID(code))
		code = 550;

	/* Convert int value to 3-digit string for smfi_setmlreply(). */
	rcode[3] = '\0';
	rcode[2] = '0' + (code / 1   % 10);
	rcode[1] = '0' + (code / 10  % 10);
	rcode[0] = '0' + (code / 100 % 10);

	if (sscanf(lines[0], "%1u.%u.%u", &class, &subject, &detail) == 3) {
		length = snprintf(xcode, sizeof (xcode), "%u.%u.%u", class, subject, detail);
		lines[0] += length + (isspace(lines[0][length]) ? 1 : 0);
		ecode = xcode;
	} else if (ecode == NULL) {
		TextCopy(xcode, sizeof (xcode), "x.7.1");
		xcode[0] = rcode[0];
		ecode = xcode;
	}

	if (smfLogDetail & SMF_LOG_DEBUG) {
		syslog(LOG_DEBUG, TAG_FORMAT "multi-line reply %s %s ...", TAG_ARGS, rcode, ecode);
		for (i = 0; i < 32 && lines[i] != NULL; i++)
			syslog(LOG_DEBUG, TAG_FORMAT "line %d: %s", TAG_ARGS, i, lines[i]);
	}

	i = smfi_setmlreply(
		work->ctx, rcode, (char *) ecode,
		lines[ 0],lines[ 1],lines[ 2],lines[ 3],lines[ 4],lines[ 5],lines[ 6],lines[ 7],
		lines[ 8],lines[ 9],lines[10],lines[11],lines[12],lines[13],lines[14],lines[15],
		lines[16],lines[17],lines[18],lines[19],lines[20],lines[21],lines[22],lines[23],
		lines[24],lines[25],lines[26],lines[27],lines[28],lines[29],lines[30],lines[31],
		NULL
	);

	if (i == MI_FAILURE)
		syslog(LOG_ERR, TAG_FORMAT "smfMultiLineReplyA(): smfi_setmlreply() failed", TAG_ARGS);

	if (smfLogDetail & SMF_LOG_TRACE)
		syslog(LOG_DEBUG, TAG_FORMAT "exit  smfMultiLineReplyA() rc=%d", TAG_ARGS, rc);

	return rc;
}

sfsistat
smfMultiLineReplyV(smfWork *work, int code, const char *ecode, va_list args)
{
	int i;
	char *s, *lines[32];

	memset(lines, 0, sizeof (lines));

	for (i = 0; i < 32 && (s = va_arg(args, char *)) != NULL; i++) {
		smfLog(SMF_LOG_DEBUG, TAG_FORMAT "line %d: %s", TAG_ARGS, i, s);
		lines[i] = s;
	}

	return smfMultiLineReplyA(work, code, ecode, lines);
}

sfsistat
smfMultiLineReply(smfWork *work, int code, const char *ecode, ...)
{
	long j;
	char *s;
	sfsistat rc;
	va_list args;
	Vector lines = NULL, list = NULL;

	va_start(args, ecode);

	while ((s = va_arg(args, char *)) != NULL) {
		VectorDestroy(lines);

		if ((lines = TextSplit(s, "\r\n", 0)) == NULL) {
			syslog(LOG_ERR, TAG_FORMAT "smfMultiLineReply(): TextSplit() failed: %s (%d)", TAG_ARGS, strerror(errno), errno);
			rc = smfReply(work, code, ecode, "generic error");
			goto error1;
		}

		for (j = 0; j < VectorLength(lines); j++) {
			if (VectorAdd(list, VectorGet(lines, j))) {
				syslog(LOG_ERR, TAG_FORMAT "smfMultiLineReply(): VectorAdd() failed: %s (%d)", TAG_ARGS, strerror(errno), errno);
				rc = smfReply(work, code, ecode, "generic error");
				goto error2;
			}
		}
	}

	rc = smfMultiLineReplyA(work, code, ecode, (char **) VectorBase(list));
error2:
	VectorDestroy(lines);
error1:
	VectorDestroy(list);

	va_end(args);

	return rc;
}
#endif

/***********************************************************************
 *** Access Database White List Tests
 ***********************************************************************/

/*
 * @return
 *	Pointer to the next delimiter in string or the end of string.
 */
static char *
find_delim(const char *start, const char *delims)
{
	const char *s;

	for (s = start; *(s += strcspn(s, delims)) != '\0'; s++) {
		if (start < s && s[-1] != '\\')
			break;
	}

	return (char *) s;
}

/**
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param hay
 *	A C string to search.
 *
 * @param pins
 *	A C string containing an optional list of whitespace separated
 *	pattern/action pairs followed by an optional default action.
 *
 *	( !pattern!action | /regex/action  | [network/cidr]action )* default-action?
 *
 *	The !pattern! uses the simple TextMatch() function with * and ?
 *	wild cards. The /regex/ uses Exteneded Regular Expressions (or
 *	Perl Compatible Regular Expressions if selected at compile time).
 *
 * @param action
 *	A pointer to a C string pointer, which can be NULL. Used to
 *	passback an allocated copy of the action string or NULL. Its
 *	the caller's responsiblity to free() this string.
 *
 * @return
 *	 A SMDB_ACCESS_* code.
 */
int
smfAccessPattern(smfWork *work, const char *hay, char *pins, char **actionp)
{
	long cidr, length;
	char *action, *next, *pin;
	int access, is_hay_ip, match;
	unsigned char net[IPV6_BYTE_LENGTH], ipv6[IPV6_BYTE_LENGTH];

	access = SMDB_ACCESS_UNKNOWN;

	smfLog(
		SMF_LOG_DEBUG,
		TAG_FORMAT "enter smfAccessPattern(%lx, \"%s\", \"%.50s...\", %lx)", TAG_ARGS,
		(long) work, TextNull(hay), TextNull(pins), (long) actionp
	);

	if (hay == NULL || pins == NULL || *pins == '\0') {
		access = SMDB_ACCESS_NOT_FOUND;
		goto error0;
	}

	if (actionp != NULL)
		*actionp = NULL;

	action = "";
	is_hay_ip = 0 < parseIPv6(hay, ipv6);

	for (pin = pins; *pin != '\0'; pin = next) {
		/* Pattern/action pairs cannot contain white space, because
		 * the strings they are intended to match: ips, domains, host
		 * names, addresses cannot contain whitespace. I do it this
		 * way because TextSplit() dequotes the string and thats bad
		 * for regex patterns.
		 */
		pin += strspn(pin, " \t");
		next = pin + strcspn(pin, " \t");

		smfLog(SMF_LOG_DEBUG, TAG_FORMAT "pin=\"%.50s...\"", TAG_ARGS, pin);

		/* !pattern!action */
		if (*pin == '!') {
			/* Find first unescaped exclamation to end pattern.
			 * An exclamation is permitted in the local-part of
			 * an email address and so must be backslash escaped.
			 */
			action = find_delim(pin+1, "!");
			if (*action == '\0') {
				smfLog(SMF_LOG_ERROR, TAG_FORMAT "pattern delimiter error: \"%.50s...\"", TAG_ARGS, pin);
				continue;
			}

			*action++ = '\0';
			smfLog(SMF_LOG_DEBUG, TAG_FORMAT "pattern=!%s! action=%.6s", TAG_ARGS, pin+1, action);
			match = TextMatch(hay, pin+1, -1, 1);
			action[-1] = '!';

			if (match) {
				smfLog(SMF_LOG_DATABASE, TAG_FORMAT "\"%s\" matched \"%.50s...\"", TAG_ARGS, hay, pin);
				access = smdbAccessCode(action);
				break;
			}
		}

		/* '[' network [ '/' cidr ] ']' action
		 *
		 * Valid forms:
		 *
		 *	[192.0.2.1]OK
		 *	[192.0.2.0/24]REJECT
		 *	[2001:DB8::1]OK
		 *	[2001:DB8::0/32]REJECT
		 *	[::192.0.2.0/104]DISCARD
		 *
		 *	[192.0.2.1]some@example.com
		 *	[192.0.2.1]some@[192.0.2.254]
		 */
		else if (*pin == '[') {
			if (!is_hay_ip)
				continue;

			/* Find first unescaped right-square bracket to end pattern.
			 * A right-square bracket is permitted for an IP-as-domain
			 * literal in an email address and so must be backslash escaped.
			 */
			action = find_delim(pin+1, "]");
			if (*action == '\0') {
				smfLog(SMF_LOG_ERROR, TAG_FORMAT "network delimiter error: \"%.50s...\"", TAG_ARGS, pin);
				continue;
			}

			pin++;
			*action++ = '\0';
			smfLog(SMF_LOG_DEBUG, TAG_FORMAT "network=[%s] action=%.6s...", TAG_ARGS, pin, action);
			length = parseIPv6(pin, net);
			action[-1] = ']';

			if (length <= 0) {
				smfLog(
					SMF_LOG_ERROR, TAG_FORMAT "network specifier error: \"%.50s...\"",
					TAG_ARGS, pin-1
				);
				continue;
			}

			/* When the /cidr portion is missing, assume /128. */
			if (pin[length] == '\0') {
				/* This could be IPV4_BIT_LENGTH, but we
				 * treat all our IPv4 as IPv6 addresses.
				 */
				cidr = IPV6_BIT_LENGTH;
			}

			else if (pin[length] == '/') {
				cidr = strtol(pin+length+1, NULL, 10);
				/* If no colons, assume IPv4 address. */
				if (strchr(pin, ':') == NULL)
					cidr = IPV6_BIT_LENGTH - 32 + cidr;
			}

			else {
				smfLog(
					SMF_LOG_ERROR, TAG_FORMAT "network specifier error: \"%.50s...\"",
					TAG_ARGS, pin-1
				);
				continue;
			}

			if (networkContainsIp(net, cidr, ipv6)) {
				smfLog(SMF_LOG_DATABASE, TAG_FORMAT "\"%s\" matched \"%.50s...\"", TAG_ARGS, hay, pin-1);
				access = smdbAccessCode(action);
				break;
			}
		}

#ifdef HAVE_REGEX_H
		/* /regex/action */
		else if (*pin == '/') {
			int code;
			regex_t re;
			char error[256];

			/* Find first unescaped slash delimiter to end pattern.
			 * A slash is permitted in the local-part of an email
			 * address and so must be backslash escaped.
			 */
			action = find_delim(pin+1, "/");
			if (*action == '\0') {
				smfLog(SMF_LOG_ERROR, TAG_FORMAT "regular expression delimiter error: \"%.50s...\"", TAG_ARGS, pin);
				continue;
			}

			*action++ = '\0';
			smfLog(SMF_LOG_DEBUG, TAG_FORMAT "regex=/%s/ action=%.6s...", TAG_ARGS, pin+1, action);
			code = regcomp(&re, pin+1, REG_EXTENDED|REG_NOSUB|REG_ICASE);
			action[-1] = '/';

			if (code != 0) {
				regerror(code, &re, error, sizeof (error));
				smfLog(
					SMF_LOG_ERROR, TAG_FORMAT "regular expression error: %s \"%.50s...\"",
					TAG_ARGS, error, pin
				);
				continue;
			}

			code = regexec(&re, hay, 0, NULL, 0);

			if (code == 0) {
				smfLog(SMF_LOG_DATABASE, TAG_FORMAT "\"%s\" matched \"%.50s...\"", TAG_ARGS, hay, pin);
				access = smdbAccessCode(action);
				regfree(&re);
				break;
			}

			if (code != REG_NOMATCH) {
				regerror(code, &re, error, sizeof (error));
				smfLog(
					SMF_LOG_ERROR, TAG_FORMAT "regular expression error: %s \"%.50s...\"",
					TAG_ARGS, error, pin
				);
			}
			regfree(&re);
		}
#endif /* HAVE_REGEX_H */
		else {
			smfLog(
				SMF_LOG_DATABASE, TAG_FORMAT "\"%s\" default action \"%.10s...\"",
				TAG_ARGS, hay, pin
			);
			access = smdbAccessCode(pin);
			action = pin;
			break;
		}
	}

	if (strcmp(action, "NEXT") == 0)
		access = SMDB_ACCESS_NOT_FOUND;
	else if (access != SMDB_ACCESS_NOT_FOUND && actionp != NULL)
		*actionp = TextDupN(action, strcspn(action, " \t"));
error0:
	smfLog(
		SMF_LOG_DEBUG,
		TAG_FORMAT "exit  smfAccessPattern(%lx, \"%s\", \"%.50s...\", %lx) rc=%c action='%s'", TAG_ARGS,
		(long) work, TextNull(hay), TextNull(pins), (long) actionp, access,
		(actionp == NULL || *actionp == NULL) ? "" : *actionp
	);

	return access;
}

static void
smdb_free_pair(char **lhs, char **rhs)
{
	if (lhs != NULL) {
		free(*lhs);
		*lhs = NULL;
	}
	if (rhs != NULL) {
		free(*rhs);
		*rhs = NULL;
	}
}

/**
 * Perform the following access.db lookups concerning IP and/or resolved
 * domain name, stopping on the first entry found:
 *
 * For an IPv4 address:
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
 *
 * For an IPv6 address:
 *
 *	tag:a:b:c:d:e:f:g
 *	tag:a:b:c:d:e:f
 *	tag:a:b:c:d:e
 *	tag:a:b:c:d
 *	tag:a:b:c
 *	tag:a:b
 *	tag:a
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * resolved, then the subsequent lookups are:
 *
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * did NOT resolve, then the subsequent lookups are:
 *
 *	tag:[ip]
 *	tag:
 *
 * When an entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned. Otherwise if no entry is
 * found, then SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * Note this lookup ordering, except the empty tag:, is based on sendmail's
 * lookups. Sendmail syntax limits the netmasks to /32, /24, /16, /8 for IPv4
 * and /128, /112. /96, ... /16 for IPv6, which are the most common cases,
 * but not so flexible as full range netmasks. The smfAccessPattern() pattern
 * list processing provides "[network/cidr]action" for finer granularity.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param client_name
 *	A C string for the SMTP client host name.
 *
 * @param client_addr
 *	A C string for the SMTP client address.
 *
 * @param lhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @param rhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, or SMDB_ACCESS_UNKNOWN.
 *
 * @see
 *	smfAccessPattern()
 */
int
smfAccessClient(smfWork *work, const char *tag, const char *client_name, const char *client_addr, char **lhs, char **rhs)
{
	int access;
	char *value = NULL;

	/*	tag:a.b.c.d
	 *	tag:a.b.c
	 *	tag:a.b
	 *	tag:a
	 */
	if ((access = smdbAccessIp(smdbAccess, tag, client_addr, lhs, &value)) != SMDB_ACCESS_NOT_FOUND)
		access = smfAccessPattern(work, client_addr, value, rhs);

	/* If the client IP resolved and matched, then the lookup order is:
	 *
	 *	tag:some.sub.domain.tld
	 *	tag:sub.domain.tld
	 *	tag:domain.tld
	 *	tag:tld
	 * 	tag:
	 *
	 * If the client IP did not resolve nor match, then the lookup order is:
	 *
	 *	tag:[ip]
	 * 	tag:
	 */
	if (access == SMDB_ACCESS_NOT_FOUND) {
		smdb_free_pair(lhs, &value);
		access = smdbAccessDomain(smdbAccess, tag, client_name, lhs, &value);

		if (access == SMDB_ACCESS_NOT_FOUND) {
			smdb_free_pair(lhs, &value);
			value = smdbGetValue(smdbAccess, tag);
			if (value != NULL && lhs != NULL)
				*lhs = strdup(tag);
		}

		if ((access = smfAccessPattern(work, client_name, value, rhs)) == SMDB_ACCESS_NOT_FOUND)
			access = smfAccessPattern(work, client_addr, value, rhs);

		if (access == SMDB_ACCESS_NOT_FOUND)
			smdb_free_pair(lhs, &value);
	}

	free(value);

	smfLog(
		SMF_LOG_DEBUG,
		TAG_FORMAT "smfAccessClient(%lx, %s, %s, %s, %lx, %lx) access=%d", TAG_ARGS,
		(long) work, TextNull(tag), TextNull(client_name), TextNull(client_addr), (long) lhs, (long) rhs, access
	);

	return access;
}

/**
 * Perform the following access.db lookups concerning IP and/or resolved
 * domain name, stopping on the first entry found:
 *
 * For an IPv4 address:
 *
 *	tag:a.b.c.d
 *	tag:a.b.c
 *	tag:a.b
 *	tag:a
 *
 *	connect:a.b.c.d
 *	connect:a.b.c
 *	connect:a.b
 *	connect:a
 *
 *	a.b.c.d
 *	a.b.c
 *	a.b
 *	a
 *
 * For an IPv6 address:
 *
 *	tag:a:b:c:d:e:f:g
 *	tag:a:b:c:d:e:f
 *	tag:a:b:c:d:e
 *	tag:a:b:c:d
 *	tag:a:b:c
 *	tag:a:b
 *	tag:a
 *
 *	connect:a:b:c:d:e:f:g
 *	connect:a:b:c:d:e:f
 *	connect:a:b:c:d:e
 *	connect:a:b:c:d
 *	connect:a:b:c
 *	connect:a:b
 *	connect:a
 *
 *	a:b:c:d:e:f:g
 *	a:b:c:d:e:f
 *	a:b:c:d:e
 *	a:b:c:d
 *	a:b:c
 *	a:b
 *	a
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * resolved, then the subsequent lookups are:
 *
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:
 *
 *	connect:some.sub.domain.tld
 *	connect:sub.domain.tld
 *	connect:domain.tld
 *	connect:tld
 *
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *
 * If the above IP address lookups fail to find an entry and the IP address
 * did NOT resolve, then the subsequent lookups are:
 *
 *	tag:[ip]
 *	tag:
 *
 *	connect:[ip]
 *
 *	[ip]
 *
 * When a tag: entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned, else on the result of the
 * the right-hand-side is returned. Otherwise if no entry is found, then
 * SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * Note this lookup ordering, except the empty tag:, is based on sendmail's
 * lookups. Sendmail syntax limits the netmasks to /32, /24, /16, /8 for IPv4
 * and /128, /112. /96, ... /16 for IPv6, which are the most common cases,
 * but not so flexible as full range netmasks. The smfAccessPattern() pattern
 * list processing provides "[network/cidr]action" for finer granularity.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param client_name
 *	A C string for the SMTP client host name.
 *
 * @param client_addr
 *	A C string for the SMTP client address.
 *
 * @param loopbackDefault
 *	An SMDB_ACCESS_* value returned for the localhost [127.0.0.1]
 *	loopback address.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, or SMDB_ACCESS_UNKNOWN.
 */
int
smfAccessHost(smfWork *work, const char *tag, const char *client_name, const char *client_addr, int loopbackDefault)
{
	int access;

	if (isReservedIP(client_addr, IS_IP_THIS_HOST|IS_IP_LOCALHOST|IS_IP_LOOPBACK))
		access = loopbackDefault;

	/* Lookup
	 *
	 *	tag:a.b.c.d
	 *	tag:a.b.c
	 *	tag:a.b
	 *	tag:a
	 *
	 * If the client IP resolved and matched, then lookup:
	 *
	 *	tag:some.sub.domain.tld
	 *	tag:sub.domain.tld
	 *	tag:domain.tld
	 *	tag:tld
	 *	tag:
	 *
	 * If the client IP did not resolve nor match, then lookups:
	 *
	 *	tag:[ip]
	 *	tag:
	 */
	else if ((access = smfAccessClient(work, tag, client_name, client_addr, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;

	/*	Connect:a.b.c.d
	 *	Connect:a.b.c
	 *	Connect:a.b
	 *	Connect:a
	 */
#ifdef ENABLE_ACCESS_TAGLESS
	/*	a.b.c.d
	 *	a.b.c
	 *	a.b
	 *	a
	 */
	else if ((access = smdbAccessIp2(smdbAccess, "connect:", client_addr, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#else
	else if ((access = smdbAccessIp(smdbAccess, "connect:", client_addr, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#endif
	/* If the client IP resolved and matched, then the lookup order is:
	 *
	 *	Connect:some.sub.domain.tld
	 *	Connect:sub.domain.tld
	 *	Connect:domain.tld
	 *	Connect:tld
	 *
	 * If the client IP did not resolve or match, then the lookup order is:
	 *
	 *	Connect:[ip]
	 */
#ifdef ENABLE_ACCESS_TAGLESS
	/*	[ip]
	 *	some.sub.domain.tld
	 *	sub.domain.tld
	 *	domain.tld
	 *	tld
	 */
	else if ((access = smdbAccessDomain2(smdbAccess, "connect:", client_name, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#else
	else if ((access = smdbAccessDomain(smdbAccess, "connect:", client_name, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#endif
	/* Unless by-pass above in the access database, then RFC 2606
	 * reserved special domains will be rejected. Take care with
	 * the .localhost or .localdomain domain if you use it.
	 */
	else if (smfOptRejectRFC2606.value && isRFC2606(client_name)) {
		smfLog(
			SMF_LOG_WARN, TAG_FORMAT "host %s [%s] from RFC2606 reserved domain",
			TAG_ARGS, client_name, client_addr
		);
		access = SMDB_ACCESS_REJECT;
	}

	/* Postfix 2.3 uses "unknown" for client_name when there is no rDNS
	 * result instead of an IP-as-domain-literal "[123.45.67.89]" which
	 * Sendmail uses.
	 */
	else if (smfOptRejectUnknownTLD.value && *client_name != '['
	&& TextInsensitiveCompare(client_name, "unknown") != 0 && !hasValidTLD(client_name)) {
		smfLog(
			SMF_LOG_WARN, TAG_FORMAT "host %s [%s] from unknown TLD",
			TAG_ARGS, client_name, client_addr
		);
		access = SMDB_ACCESS_REJECT;
	}

	if ((access = smdbAccessIsOk(access)) == SMDB_ACCESS_OK) {
		smfLog(
			SMF_LOG_INFO, TAG_FORMAT "host %s [%s] OK",
			TAG_ARGS, client_name, client_addr
		);
		work->skipConnection = 1;
	}

	return access;
}

/**
 * Perform the following access.db lookups for an auth-id, stopping on
 * the first entry found:
 *
 *	tag:auth_authen				RHS
 * 	tag:					RHS
 *
 * When an entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned. The string to search will
 * be a mail address.
 *
 * Otherwise if no entry is found, then SMDB_ACCESS_NOT_FOUND will be
 * returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param auth
 *	A C string for the {auth_authen} macro or NULL.
 *
 * @param mail
 *	A C string for the SMTP MAIL FROM: address.
 *
 * @param lhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @param rhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR.
 *
 * @see
 *	smfAccessPattern()
 */
int
smfAccessAuth(smfWork *work, const char *tag, const char *auth, const char *mail, char **lhs, char **rhs)
{
	int access;
	size_t buflen;
	const char *start, *stop;
	char *value = NULL, *buf, path[SMTP_PATH_LENGTH+1];

	if (auth == NULL)
		return SMDB_ACCESS_NOT_FOUND;

	if (findInnerPath(mail, &start, &stop) || sizeof (path) <= stop - start)
		return SMDB_ACCESS_NOT_FOUND;

	if (*start == '<')
		start++;
	if (start < stop && stop[-1] == '>')
		stop--;

	strncpy(path, start, stop-start);
	path[stop-start] = '\0';

	buflen = strlen(tag) + strlen(auth) + 1;
	if ((buf = malloc(buflen)) == NULL) {
		(void) smfReply(work, 451, "4.0.0", "internal error, out of memory");
		return SMDB_ACCESS_TEMPFAIL;
	}

	if (buflen <= snprintf(buf, buflen, "%s%s", tag, auth)) {
		(void) smfReply(work, 553, "5.1.0", "internal error, buffer overflow");
		return SMDB_ACCESS_ERROR;
	}

	value = smdbGetValue(smdbAccess, buf);

	if (value == NULL) {
		free(buf);
		value = smdbGetValue(smdbAccess, tag);
		if (value != NULL && lhs != NULL)
			*lhs = strdup(tag);
	} else if (lhs != NULL) {
		*lhs = buf;
	} else {
		free(buf);
	}

	/* NEXT is a Snert milter extension that can only be used
	 * with milter specific tags.
	 *
	 * Normally when the access.db lookup matches a milter tag,
	 * then RHS regex pattern list is processed and there are
	 * no further access.db lookups possible.
	 *
	 * The NEXT action allows the access.db lookups to resume
	 * and is effectively the opposite of SKIP.
	 *
	 * Consider the following trival example:
	 *
	 *	milter-NAME-from:com	/@com/REJECT NEXT
	 *	From:com		OK
	 *
	 * would reject mail from places like compaq.com or com.com
	 * if the pattern matches, but resume the the access.db
	 * lookups otherwise.
	 *
	 * Consider this more complex example concerning the format
	 * of aol.com mail addresses. AOL local parts are between 3
	 * and 16 characters long and can contain dots and RFC 2822
	 * atext characters except % and /.
	 *
	 * First is what might be specified if NEXT were not possible:
	 *
	 *	milter-NAME-from:aol.com  /^.{1,2}@aol.com$/REJECT /^[^@]{17,}@aol.com$/REJECT /[%\/]/REJECT
	 *	From:fred@aol.com	OK
	 *	From:john@aol.com	OK
	 *
	 * Now consider this shorter version using NEXT:
	 *
	 *	milter-NAME-from:aol.com  /^[a-zA-Z0-9!#$&'*+=?^_`{|}~.-]{3,16}@aol.com$/NEXT REJECT
	 *	From:fred@aol.com	OK
	 *	From:john@aol.com	OK
	 *
	 * The NEXT used above allowed me to specify one simple regex
	 * instead of (a complex one using alternation or) three in order
	 * to validate the format aol.com address and then proceed to
	 * lookup white listed and/or black listed addresses.
	 */
	access = smfAccessPattern(work, path, value, rhs);
	if (access == SMDB_ACCESS_NOT_FOUND && lhs != NULL)
		free(*lhs);

	free(value);

	return access;
}

/**
 * Perform the following access.db lookups for a mail address, stopping on
 * the first entry found:
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 * 	tag:
 *
 * When an entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned. If auth is not NULL, then
 * the string to search will be "auth:mail", else just "mail".
 *
 * Otherwise if no entry is found, then SMDB_ACCESS_NOT_FOUND will be
 * returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param mail
 *	A C string for the SMTP MAIL FROM: address.
 *
 * @param lhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the key found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @param rhs
 *	A pointer to C string pointer. May be NULL. If this pointer is
 *	not NULL, then pass back the pointer to an allocated C string
 *	corresponding to the value found. Its the  responsibilty of the
 *	caller to release this memory.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR.
 *
 * @see
 *	smfAccessPattern()
 */
int
smfAccessEmail(smfWork *work, const char *tag, const char *mail, char **lhs, char **rhs)
{
	int access;
	char *value;

	access = smdbAccessMail(smdbAccess, tag, mail, lhs, &value);

	if (access == SMDB_ACCESS_NOT_FOUND) {
		value = smdbGetValue(smdbAccess, tag);
		if (value != NULL && lhs != NULL)
			*lhs = strdup(tag);
	}

	/* NEXT is a Snert milter extension that can only be used
	 * with milter specific tags.
	 *
	 * Normally when the access.db lookup matches a milter tag,
	 * then RHS regex pattern list is processed and there are
	 * no further access.db lookups possible.
	 *
	 * The NEXT action allows the access.db lookups to resume
	 * and is effectively the opposite of SKIP.
	 *
	 * Consider the following trival example:
	 *
	 *	milter-NAME-from:com	/@com/REJECT NEXT
	 *	From:com		OK
	 *
	 * would reject mail from places like compaq.com or com.com
	 * if the pattern matches, but resume the the access.db
	 * lookups otherwise.
	 *
	 * Consider this more complex example concerning the format
	 * of aol.com mail addresses. AOL local parts are between 3
	 * and 16 characters long and can contain dots and RFC 2822
	 * atext characters except % and /.
	 *
	 * First is what might be specified if NEXT were not possible:
	 *
	 *	milter-NAME-from:aol.com  /^.{1,2}@aol.com$/REJECT /^[^@]{17,}@aol.com$/REJECT /[%\/]/REJECT
	 *	From:fred@aol.com	OK
	 *	From:john@aol.com	OK
	 *
	 * Now consider this shorter version using NEXT:
	 *
	 *	milter-NAME-from:aol.com  /^[a-zA-Z0-9!#$&'*+=?^_`{|}~.-]{3,16}@aol.com$/NEXT REJECT
	 *	From:fred@aol.com	OK
	 *	From:john@aol.com	OK
	 *
	 * The NEXT used above allowed me to specify one simple regex
	 * instead of (a complex one using alternation or) three in order
	 * to validate the format aol.com address and then proceed to
	 * lookup white listed and/or black listed addresses.
	 */
	access = smfAccessPattern(work, mail, value, rhs);
	if (access == SMDB_ACCESS_NOT_FOUND && lhs != NULL)
		free(*lhs);

	free(value);

	return access;
}

/**
 * Perform the following access.db lookups for mail address, stopping on
 * the first entry found:
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 * 	tag:
 *
 *	from:account@some.sub.domain.tld
 *	from:some.sub.domain.tld
 *	from:sub.domain.tld
 *	from:domain.tld
 *	from:tld
 *	from:account@
 *
 *	account@some.sub.domain.tld
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *	account@
 *
 * When a tag: entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned, else on the result of the
 * the right-hand-side is returned. Otherwise if no entry is found, then
 * SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param mail
 *	A C string for the SMTP MAIL FROM: address.
 *
 * @param dsnDefault
 *	A SMDB_ACCESS_* value to be return for the DSN (null sender).
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR for a parse error in
 *	which case the SMTP reponse will also have been set.
 */
int
smfAccessMail(smfWork *work, const char *tag, const char *mail, int dsnDefault)
{
	int access;
	ParsePath *path;
	const char *error;
	char *auth_authen;
#ifdef ENABLE_COMBO_TAGS
	char connect[80], *name, *delim, *value;
#endif

	free(work->mail);
	work->mail = NULL;

	if ((error = parsePath(mail, smfFlags, 1, &path)) != NULL) {
		smfLog(SMF_LOG_ERROR, "sender %s parse error: %s", mail, error);
		(void) smfReply(work, SMTP_ISS_TEMP(error) ? 451 : 553, NULL, error);
		return SMTP_ISS_TEMP(error) ? SMDB_ACCESS_TEMPFAIL : SMDB_ACCESS_ERROR;
	}

	auth_authen = smfi_getsymval(work->ctx, smMacro_auth_authen);

	smfLog(
		SMF_LOG_PARSE,
		TAG_FORMAT "address='%s' localleft='%s' localright='%s' domain='%s' auth='%s'",
		TAG_ARGS, path->address.string, path->localLeft.string,
		path->localRight.string, path->domain.string, TextNull(auth_authen)

	);

#ifdef ENABLE_COMBO_TAGS
	if (work->info == NULL) {
		name = delim = "";
	} else {
		/* Build the milter specific leading tag name for combo
		 * tags to avoid confusion with sendmail's own tags.
		 */
		name = work->info->package;
		delim = "-";
	}

	if (sizeof (connect) <= snprintf(connect, sizeof (connect), "%s%sconnect:", name, delim)) {
		(void) smfReply(work, 553, "5.1.0", "internal error, buffer overflow");
		return SMDB_ACCESS_ERROR;
	}
#endif
	/* The default is to white list authenticated users. */
	if (smfOptSmtpAuthOk.value && auth_authen != NULL)
		access = SMDB_ACCESS_OK;

	/* How to handle the DSN address. */
	else if (path->address.length == 0)
		access = dsnDefault;

#ifdef ENABLE_COMBO_TAGS
	else if ((access = smdbIpMail(smdbAccess, connect, work->client_addr, ":from:", path->address.string, NULL, &value)) != SMDB_ACCESS_NOT_FOUND
	     ||  (*work->client_name != '\0' && (access = smdbDomainMail(smdbAccess, connect, work->client_name, ":from:", path->address.string, NULL, &value)) != SMDB_ACCESS_NOT_FOUND)) {
		access = smfAccessPattern(work, work->client_addr, value, NULL);
		if (access == SMDB_ACCESS_NOT_FOUND) {
			access = smfAccessPattern(work, work->client_name, value, NULL);
			if (access == SMDB_ACCESS_NOT_FOUND)
				access = smfAccessPattern(work, path->address.string, value, NULL);
		}
		free(value);
	}
#endif
	/* Lookup
	 *
	 *	tag:account@some.sub.domain.tld
	 *	tag:some.sub.domain.tld
	 *	tag:sub.domain.tld
	 *	tag:domain.tld
	 *	tag:tld
	 *	tag:account@
	 *	tag:
	 */
	else if ((access = smfAccessEmail(work, tag, path->address.string, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;

	/* Lookup
	 *
	 *	from:account@some.sub.domain.tld
	 *	from:some.sub.domain.tld
	 *	from:sub.domain.tld
	 *	from:domain.tld
	 *	from:tld
	 *	from:account@
	 */
#ifdef ENABLE_ACCESS_TAGLESS
	/*	account@some.sub.domain.tld
	 *	some.sub.domain.tld
	 *	sub.domain.tld
	 *	domain.tld
	 *	tld
	 *	account@
	 */
	else if ((access = smdbAccessMail2(smdbAccess, "from:", path->address.string, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#else
	else if ((access = smdbAccessMail(smdbAccess, "from:", path->address.string, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#endif
	/* Unless by-pass above in the access database, then RFC 2606
	 * reserved special domains will be rejected. Take care with
	 * the .localhost domain if you use it.
	 */
	else if (smfOptRejectRFC2606.value && isRFC2606(path->domain.string)) {
		smfLog(
			SMF_LOG_WARN, TAG_FORMAT "sender <%s> from RFC2606 reserved domain",
			TAG_ARGS, path->address.string
		);
		access = SMDB_ACCESS_REJECT;
	}

	else if (smfOptRejectUnknownTLD.value && *path->domain.string != '[' && !hasValidTLD(path->domain.string)) {
		smfLog(
			SMF_LOG_WARN, TAG_FORMAT "sender <%s> from unknown TLD",
			TAG_ARGS, path->address.string
		);
		access = SMDB_ACCESS_REJECT;
	}

	/* This behaviour is similar to FEATURE(`delay_checks')
	 * in that MAIL white-listing can override connection
	 * black-listing. However, MAIL black-listing will
	 * not override connection white-listing.
	 *
	 * For a black-listing or unknown result, continue.
	 */
	if ((access = smdbAccessIsOk(access)) == SMDB_ACCESS_OK) {
		smfLog(
			SMF_LOG_INFO, TAG_FORMAT "sender <%s> %s", TAG_ARGS,
			path->address.string, auth_authen == NULL ? "OK" : "SMTP AUTH authenticated"
		);
		path->isWhiteListed = 1;
		work->skipMessage = 1;
	}

	else if (access == SMDB_ACCESS_REJECT) {
		/* Explicitly clear this flag. Consider the case where
		 * you white-list by connection, but want to override
		 * on a per message basis.
		 */
		work->skipMessage = 0;
	}

	work->mail = path;

	smfLog(
		SMF_LOG_PARSE,
		TAG_FORMAT "sender=<%s> access=%c skipConnection=%d skipMessage=%d",
		TAG_ARGS, path->address.string, access ? access : ' ',
		work->skipConnection, work->skipMessage
	);

	return access;
}

/**
 * Perform the following access.db lookups for mail address, stopping on
 * the first entry found:
 *
 *	tag:account@some.sub.domain.tld
 *	tag:some.sub.domain.tld
 *	tag:sub.domain.tld
 *	tag:domain.tld
 *	tag:tld
 *	tag:account@
 * 	tag:
 *
 *	spam:account@some.sub.domain.tld	FRIEND
 *	spam:some.sub.domain.tld		FRIEND
 *	spam:sub.domain.tld			FRIEND
 *	spam:domain.tld				FRIEND
 *	spam:tld				FRIEND
 *	spam:account@				FRIEND
 *
 *	from:account@some.sub.domain.tld
 *	from:some.sub.domain.tld
 *	from:sub.domain.tld
 *	from:domain.tld
 *	from:tld
 *	from:account@
 *
 *	account@some.sub.domain.tld
 *	some.sub.domain.tld
 *	sub.domain.tld
 *	domain.tld
 *	tld
 *	account@
 *
 * When a tag: entry is found, then the right-hand-side value is processed
 * as a pattern list and that result returned, else on the result of the
 * the right-hand-side is returned. Otherwise if no entry is found, then
 * SMDB_ACCESS_NOT_FOUND will be returned.
 *
 * @param work
 *	A pointer to a smfWork workspace.
 *
 * @param tag
 *	A C string tag that may be prefixed to access.db look-ups.
 *
 * @param rcpt
 *	A C string for the SMTP RCPT TO: address.
 *
 * @return
 *	One of SMDB_ACCESS_OK, SMDB_ACCESS_REJECT, SMDB_ACCESS_UNKNOWN,
 *	SMDB_ACCESS_NOT_FOUND, or SMDB_ACCESS_ERROR for a parse error in
 *	which case the SMTP reponse will also have been set.
 */
int
smfAccessRcpt(smfWork *work, const char *tag, const char *rcpt)
{
	int access;
	ParsePath *path;
	const char *error;
#ifdef ENABLE_COMBO_TAGS
	char connect[80], from[80], *name, *delim, *value;
#endif
#ifdef HAVE_POP_BEFORE_SMTP
/* Requested by Michael Elliott <elliott@rod.msen.com>. [ACH: Why is this
 * being applied during the RCPT handler instead of the MAIL handler where
 * its more relavent? Possibly to support FEATURE(`delay_checks').]
 */
	char *popauth_info;
#endif

	free(work->rcpt);
	work->rcpt = NULL;
	work->skipRecipient = 0;

	if ((error = parsePath(rcpt, smfFlags, 0, &path)) != NULL) {
		smfLog(SMF_LOG_ERROR, "recipient %s parse error: %s", rcpt, error);
		(void) smfReply(work, SMTP_ISS_TEMP(error) ? 451 : 553, NULL, error);
		return SMTP_ISS_TEMP(error) ? SMDB_ACCESS_TEMPFAIL : SMDB_ACCESS_ERROR;
	}

	smfLog(
		SMF_LOG_PARSE,
		TAG_FORMAT "address='%s' localleft='%s' localright='%s' domain='%s'",
		TAG_ARGS, path->address.string, path->localLeft.string,
		path->localRight.string, path->domain.string
	);

#ifdef ENABLE_COMBO_TAGS
	if (work->info == NULL) {
		name = delim = "";
	} else {
		name = work->info->package;
		delim = "-";
	}

	if (sizeof (from) <= snprintf(from, sizeof (from), "%s%sfrom:", name, delim)) {
		(void) smfReply(work, 553, "5.1.0", "internal error, buffer overflow");
		return SMDB_ACCESS_ERROR;
	}
	if (sizeof (connect) <= snprintf(connect, sizeof (connect), "%s%sconnect:", name, delim)) {
		(void) smfReply(work, 553, "5.1.0", "internal error, buffer overflow");
		return SMDB_ACCESS_ERROR;
	}
#endif
#ifdef HAVE_POP_BEFORE_SMTP
	if ((popauth_info = smfi_getsymval(work->ctx, smMacro_popauth_info)) != NULL) {
		smfLog(SMF_LOG_PARSE, TAG_FORMAT "{popauth_info}=%s", TAG_ARGS, popauth_info);
		access = SMDB_ACCESS_OK;
	} else
#endif
	/* Block this form of routed address:
	 *
	 *	user%other.domain.com@our.domain.com
	 *
	 * Normally Sendmail prevents the %-hack relaying form, but some
	 * local rule sets might overlook this and inadvertantly circumvent
	 * Sendmail, eg. mailertable relay rule set in cookbook.mc. This
	 * test catches these slips.
	 */
	if (smfOptRejectPercentRelay.value && strchr(path->address.string, '%') != NULL) {
		smfReply(work, 550, NULL, "routed address relaying denied");
		access = SMDB_ACCESS_REJECT;
	}
#ifdef ENABLE_COMBO_TAGS
	else if ((access = smdbIpMail(smdbAccess, connect, work->client_addr, ":to:", path->address.string, NULL, &value)) != SMDB_ACCESS_NOT_FOUND
	     ||  (*work->client_name != '\0' && (access = smdbDomainMail(smdbAccess, connect, work->client_name, ":to:", path->address.string, NULL, &value)) != SMDB_ACCESS_NOT_FOUND)) {
		access = smfAccessPattern(work, work->client_addr, value, NULL);
		if (access == SMDB_ACCESS_NOT_FOUND) {
			access = smfAccessPattern(work, work->client_name, value, NULL);
			if (access == SMDB_ACCESS_NOT_FOUND)
				access = smfAccessPattern(work, path->address.string, value, NULL);
		}
		free(value);
	}

	else if ((access = smdbMailMail(smdbAccess, from, work->mail->address.string, ":to:", path->address.string, NULL, &value)) != SMDB_ACCESS_NOT_FOUND) {
		access = smfAccessPattern(work, work->mail->address.string, value, NULL);
		if (access == SMDB_ACCESS_NOT_FOUND)
			access = smfAccessPattern(work, path->address.string, value, NULL);
		free(value);
	}
#endif
	/* Lookup
	 *
	 *	tag:account@some.sub.domain.tld
	 *	tag:some.sub.domain.tld
	 *	tag:sub.domain.tld
	 *	tag:domain.tld
	 *	tag:tld
	 *	tag:account@
	 *	tag:
	 */
	else if ((access = smfAccessEmail(work, tag, path->address.string, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;

	/* Lookup
	 *
	 *	spam:account@some.sub.domain.tld	FRIEND
	 *	spam:some.sub.domain.tld		FRIEND
	 *	spam:sub.domain.tld			FRIEND
	 *	spam:domain.tld				FRIEND
	 *	spam:tld				FRIEND
	 *	spam:account@				FRIEND
	 */
	else if (smdbAccessMail(smdbAccess, "spam:", path->address.string, NULL, NULL) == SMDB_ACCESS_FRIEND)
		access = SMDB_ACCESS_OK;

	/* Lookup
	 *
	 *	to:account@some.sub.domain.tld
	 *	to:some.sub.domain.tld
	 *	to:sub.domain.tld
	 *	to:domain.tld
	 *	to:tld
	 *	to:account@
	 */
#ifdef ENABLE_ACCESS_TAGLESS
	/*	account@some.sub.domain.tld
	 *	some.sub.domain.tld
	 *	sub.domain.tld
	 *	domain.tld
	 *	tld
	 *	account@
	 */
	else if ((access = smdbAccessMail2(smdbAccess, "to:", path->address.string, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#else
	else if ((access = smdbAccessMail(smdbAccess, "to:", path->address.string, NULL, NULL)) != SMDB_ACCESS_NOT_FOUND)
		;
#endif
	/* Unless by-pass above in the access database, then RFC 2606
	 * reserved special domains will be rejected. Take care with
	 * the .localhost domain if you use it.
	 */
	else if (smfOptRejectRFC2606.value && isRFC2606(path->domain.string)) {
		smfLog(
			SMF_LOG_WARN, TAG_FORMAT "recipient <%s> has RFC2606 reserved domain",
			TAG_ARGS, path->address.string
		);
		access = SMDB_ACCESS_REJECT;
	}

	else if (smfOptRejectUnknownTLD.value
	&& TextInsensitiveCompare(path->localLeft.string, "postmaster") != 0
	&& *path->domain.string != '[' && !hasValidTLD(path->domain.string)) {
		smfLog(
			SMF_LOG_WARN, TAG_FORMAT "recipient <%s> has unknown TLD",
			TAG_ARGS, path->address.string
		);
		access = SMDB_ACCESS_REJECT;
	}

	/* This behaviour is similar to FEATURE(`delay_checks') in
	 * that RCPT white-listing can override MAIL or connection
	 * black-listing. I think this provides for maximum possible
	 * B/W listing flexibilty. However, RCPT black-listing will
	 * not override MAIL or connection white-listing.
	 *
	 * For a black-listing or unknown result, the test proceeds.
	 */
	if ((access = smdbAccessIsOk(access)) == SMDB_ACCESS_OK) {
#ifdef HAVE_POP_BEFORE_SMTP
		if (popauth_info != NULL && work->mail != NULL)
			smfLog(
				SMF_LOG_INFO, TAG_FORMAT "recipient <%s> OK, sender <%s> POP-before-SMTP authenticated",
				TAG_ARGS, path->address.string, work->mail->address.string
			);
		else
#endif
		smfLog(
			SMF_LOG_INFO, TAG_FORMAT "recipient <%s> OK",
			TAG_ARGS, path->address.string
		);
		path->isWhiteListed = 1;
#ifdef OLD_BEHAVIOUR
/* The effect of this in most of my milters is that a single white
 * listed RCPT means that the entire message is white listed for
 * all subsequent recipients.
 */
		work->skipMessage = 1;
#else
		work->skipRecipient = 1;
#endif
	}

	else if (access == SMDB_ACCESS_REJECT) {
		/* Explicitly clear this flag. Consider the case where
		 * you white-list by connection, but want to override
		 * on a per message basis. Think about messages queued
		 * on a secondary MX that doesn't do any filtering,
		 * some of those messages might be spammy.
		 */
		work->skipMessage = 0;
	}

	work->rcpt = path;

 	smfLog(
		SMF_LOG_PARSE,
		TAG_FORMAT "recipient=<%s> access=%c skipConnection=%d skipRecipient=%d",
		TAG_ARGS, path->address.string, access ? access : ' ',
		work->skipConnection, work->skipRecipient
	);

	return access;
}

/***********************************************************************
 *** Support Routines
 ***********************************************************************/

int
smfHeaderSet(SMFICTX *ctx, char *field, char *value, int index, int present)
{
	if (present)
		return smfi_chgheader(ctx, field, index, value);

	return smfi_addheader(ctx, field, value);
}

int
smfHeaderRemove(SMFICTX *ctx, char *field)
{
	return smfi_chgheader(ctx, field, 1, NULL);
}

#ifdef DISABLED_BY_ACH_20070530
/* David F. Skoll can now say "I told you so." */
/*** REMOVAL OF THIS CODE IS IN VIOLATION OF THE TERMS OF THE SOFTWARE
 *** LICENSE AS AGREED TO BY DOWNLOADING OR INSTALLING THIS SOFTWARE.
 ***/
void *
smfLicenseControl(void *data)
{
	SMTP session;
	char stamp[40];
	struct tm local;
	time_t now = time(NULL);
	SMFICTX *ctx = (SMFICTX *) data;
	char *j = smfi_getsymval(ctx, "j");
	char *if_name = smfi_getsymval(ctx, smMacro_if_name);
	char *if_addr = smfi_getsymval(ctx, smMacro_if_addr);

	if (if_addr == NULL)
		if_addr = "127.0.0.1";
	if (if_name == NULL)
		if_name = "localhost";

#ifdef HAVE_TZSET
	tzset();
#endif
	(void) localtime_r(&now, &local);
	(void) strftime(stamp, sizeof (stamp), "%a, %d %b %Y %H:%M:%S %z", &local);

	memset(&session, 0, sizeof (session));
	(void) smtpOpen(&session, "");

	if (smtpAddRcpt(&session, "notify@milter.info") == 0) {
		(void) smtpPrintf(
			&session, "Received: from %s by %s (%s [%s]); %s\r\n",
			smfDesc->package, j, if_name, if_addr, stamp
		);
		(void) smtpPrintf(
			&session, "Subject: %s/%d.%d.%d (%s [%s])\r\n",
			smfDesc->package, smfDesc->major, smfDesc->minor, smfDesc->build,
			smfOptInterfaceName.string, smfOptInterfaceIp.string
		);
		(void) smtpPrintf(&session, "Priority: normal\r\n");
		(void) smtpPrintf(&session, "\r\n");
		(void) smtpPrintf(
			&session, "libsnert=%d.%d.%d\r\n",
			LibSnert.major, LibSnert.minor, LibSnert.build
		);

		/* In response to complaints about insufficient disclosure
		 * of "phone home" code and/or proper logging of there of.
		 */
		smfLog(SMF_LOG_INFO, "IP address and version details reported to SnertSoft");
	}

	smtpClose(&session);

	return NULL;
}
#endif

unsigned short
smfOpenProlog(SMFICTX *ctx, char *client_name, _SOCK_ADDR *raw_client_addr, char *client_addr, long length)
{
	int notify = 0;
	unsigned short cid;
	static int notified = 0;
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	if (pthread_mutex_lock(&mutex))
		syslog(LOG_ERR, "mutex lock in smfOpenProlog() failed: %s (%d)", strerror(errno), errno);

	/* smfCount can never be zero. Its reserved. */
	if (++smfCount == 0)
		smfCount = 1;

	cid = smfCount;
	notify = !notified;
	notified = 1;

	if (pthread_mutex_unlock(&mutex))
		syslog(LOG_ERR, "mutex unlock in smfOpenProlog() failed: %s (%d)", strerror(errno), errno);

	client_addr[0] = '\0';

	if (raw_client_addr != NULL) {
        	void *addr = &((struct sockaddr_in *) raw_client_addr)->sin_addr;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
		if (raw_client_addr->sa_family == AF_INET6) {
			addr = &((struct sockaddr_in6 *) raw_client_addr)->sin6_addr;
			TextCopy(client_addr, length, IPV6_TAG);
			client_addr += IPV6_TAG_LENGTH;
			length -= IPV6_TAG_LENGTH;
		}
#endif
		(void) formatIP(
			addr,
			raw_client_addr->sa_family == AF_INET ? IPV4_BYTE_LENGTH : IPV6_BYTE_LENGTH,
			1, client_addr, length
		);
	} else {
		/* Assuming localhost IP address. Postfix 2.3 when it gets
		 * a localhost connection passes client_name="localhost"
		 * and client_addr=NULL to a milter instead of passing the
		 * IP  address. The libmilter documentation states that
		 * client_addr is NULL if type (of what?) is unsupported
		 * or connection is from stdin (how? when?).
		 *
		 * Now for input from stdin, assuming localhost is fine,
		 * but what are the effects of this assumption for an
		 * unknown socket/IP type?
		 */
		TextCopy(client_addr, length, "127.0.0.1");
	}

	if (notify) {
#ifdef DISABLED_BY_ACH_20070530
/* David F. Skoll can now say "I told you so." */
		/*** REMOVAL OF THIS CODE IS IN VIOLATION OF THE TERMS OF
		 *** THE SOFTWARE LICENSE AS AGREED TO BY DOWNLOADING OR
		 *** INSTALLING THIS SOFTWARE.
		 ***
		 *** This operation can take some time, so we want do it
	 	 *** outside of the critical section above and in its own
	 	 *** thread.
		 ***/
		pthread_t thread;
		(void) pthread_create(&thread, NULL, smfLicenseControl, ctx);
		(void) pthread_detach(thread);
#endif
#ifndef HAVE_SMFI_OPENSOCKET
		/* With pre-8.13 libmilter libraries, we have to wait
		 * until after the first connection, when the sendmail
		 * to libmilter domain socket will have been created,
		 * before we can change the process owner.
		 */
		smfSetProcessOwner(smfDesc);
#endif
	}

	return cid;
}

void
smfProlog(smfWork *work, SMFICTX *ctx, char *client_name, _SOCK_ADDR *raw_client_addr)
{
	work->ctx = ctx;
	work->qid = smfNoQueue;

	/* Postfix claims to support {client_name} and {client_addr}
	 * macros, but smfi_getsymval always returns NULL, so at
	 * xxfi_connect we save the arguments given to us as part of
	 * our context for future reference.
	 */
	(void) TextCopy(work->client_name, sizeof (work->client_name), client_name);
	work->cid = smfOpenProlog(ctx, client_name, raw_client_addr, work->client_addr, sizeof (work->client_addr));
}

unsigned short
smfCloseEpilog(smfWork *work)
{
	unsigned short cid = 0;

	if (work != NULL) {
		cid = work->cid;
		free(work->mail);
		free(work->rcpt);
		work->qid = smfNoQueue;

		/* We have to do this, otherwise we get a warning. */
		(void) smfi_setpriv(work->ctx, NULL);
	}

	return cid;
}

void
smfAtExitCleanUp(void)
{
	if (smfDesc != NULL) {
		(void) pthread_mutex_destroy(&smfMutex);

		/* Avoid unlinking files of an already running instance.
		 * Its the running instance's responsibility to cleanup
		 * its files.
		 */
		if (pidLoad(smfOptPidFile.string) == getpid()) {
			/* These files should be owned by smfOptRunUser
			 * and smfOptRunGroup so that the process can
			 * remove them at this point, assuming the
			 * process owner was set to smfOptRunUser and
			 * smfOptRunGroup.
			 *
			 * See smfSetFileOwner(), smfSetProcessOwner(),
			 * and smfMainStart().
			 */
#define REMOVE_UNIX_DOMAIN_SOCKET
#ifdef REMOVE_UNIX_DOMAIN_SOCKET
			/* Unlink the named socket ourselves since it appears that
			 * libmilter doesn't do this. See Sendmail 8.13.0 change log.
			 */
			if (strncmp(smfOptMilterSocket.string, "unix:", 5) == 0)
				(void) unlink(smfOptMilterSocket.string + 5);
			else if (strncmp(smfOptMilterSocket.string, "local:", 6) == 0)
				(void) unlink(smfOptMilterSocket.string + 6);
#endif
			if (unlink(smfOptPidFile.string))
				syslog(LOG_ERR, "unlink(%s): %s (%d)", smfOptPidFile.string, strerror(errno), errno);
		}
	}
}

void
smfSignalExit(int signum)
{
	(void) signal(signum, SIG_IGN);
	syslog(LOG_ERR, "signal %d received, program exit", signum);
	exit(0);
}

int
smfSetFileOwner(smfInfo *smf, const char *file)
{
	struct group *gr;
	struct passwd *pw;
	const char *filename;

	if ((pw = getpwnam(smfOptRunUser.string)) == NULL) {
		syslog(LOG_ERR, "user \"%s\" not found", smfOptRunUser.string);
		return -1;
	}

	if ((gr = getgrnam(smfOptRunGroup.string)) == NULL) {
		syslog(LOG_ERR, "group \"%s\" not found", smfOptRunGroup.string);
		return -1;
	}

	/* Convience check for a libmilter socket type prefix so that
	 * we can set the ownership of unix domain socket.
	 */
	if (strncmp(file, "unix:", 5) == 0) {
		filename = file + 5;
	} else if (strncmp(file, "local:", 6) == 0) {
		filename = file + 6;
	} else if (strncmp(file, "inet:", 5) == 0 || strncmp(file, "inet6:", 6) == 0) {
		errno = EINVAL;
		return -1;
	} else {
		filename = file;
	}

	return chown(filename, pw->pw_uid, gr->gr_gid);
}

int
smfSetProcessOwner(smfInfo *smf)
{
	gid_t gid;
	struct group *gr;
	struct passwd *pw;

	/* If we're running as root user and/or root group, we want to
	 * change the process ownership to some benign user and group
	 * specified at compile time that has permissions to read the
	 * sendmail.cf, access and aliases databases. There are
	 * intentionally NO runtime options for this and we're trying
	 * to avoid the need for setuid.
	 */
	if ((pw = getpwnam(smfOptRunUser.string)) == NULL) {
		syslog(LOG_ERR, "user \"%s\" not found", smfOptRunUser.string);
		return -1;
	}

	if ((gr = getgrnam(smfOptRunGroup.string)) == NULL) {
		syslog(LOG_ERR, "group \"%s\" not found", smfOptRunGroup.string);
		return -1;
	}

	/* initgroups() uses getgrent() to gather the supplemental
	 * groups, which in turn destroys the static buffer used
	 * by getgrnam(), so we save the gid.
	 */
	gid = gr->gr_gid;

	/* Set the process ownership now to something else if we're root. */
	if (getuid() == 0 && pw->pw_uid != 0 && gid != 0) {
#if defined(HAVE_INITGROUPS)
		if (initgroups(smfOptRunUser.string, gid))
			syslog(LOG_WARNING, "WARNING: initgroups(\"%s\", %d) failed: %s (%d)", smfOptRunUser.string, gid, strerror(errno), errno);
#endif
		if (setgid(gid))
			syslog(LOG_WARNING, "WARNING: setgid(%d \"%s\") failed: %s (%d)", gid, smfOptRunGroup.string, strerror(errno), errno);

		if (setuid(pw->pw_uid))
			syslog(LOG_WARNING, "WARNING: setuid(%d \"%s\") failed: %s (%d)", pw->pw_uid, smfOptRunUser.string, strerror(errno), errno);
	}

	syslog(LOG_DEBUG, "process ruid=%d rgid=%d euid=%d egid=%d", getuid(), getgid(), geteuid(), getegid());

	return 0;
}

/*
 * Kill the process specified by the given pid file.
 * Return 0 on success or -1 on error.
 */
int
smfKillProcess(smfInfo *smf, int signal)
{
	return pidKill(smfOptPidFile.string, signal);
}

/*
 *
 */
int
smfStartBackgroundProcess(void)
{
	pid_t pid;

	/* Start the milter as a background process. */
	if ((pid = fork()) < 0) {
		syslog(LOG_ERR, "failed to fork: %s (%d)", strerror(errno), errno);
		exit(1);
	}

	if (pid != 0)
		exit(0);

	if (setsid() == -1) {
		syslog(LOG_ERR, "failed to become a background process: %s (%d)", strerror(errno), errno);
		return -1;
	}

	return 0;
}

void
smfOptions(smfInfo *smf, int argc, char **argv, void (*options)(int, char **))
{
	int ac;
	FILE *fp;
	char **av, *buf;

	/* Load and parse the options file, if present. */
	if ((fp = fopen(smf->cf, "r")) != NULL) {
		if ((buf = malloc(BUFSIZ)) == NULL) {
			syslog(LOG_ERR, "out of memory");
			exit(1);
		}

		if (0 < (ac = fread(buf, 1, BUFSIZ-1, fp))) {
			buf[ac] = '\0';

			/* The av array and copy of buffer are NOT
			 * freed until the program exits.
			 */
			if (!TokenSplit(buf, NULL, &av, &ac, 1))
				(*options)(ac, av);
		}

		fclose(fp);
		free(buf);
	}

	(*options)(argc, argv);

	/*** Transition from old to new option handling. ***/
	smfOptFile.initial = smf->cf;
	smfOptPidFile.initial = smf->pid;
	smfOptRunUser.initial = smf->user;
	smfOptRunGroup.initial = smf->group;
	smfOptWorkDir.initial = smf->workdir;
	smfOptMilterSocket.initial = smf->socket;
	optionInit(smfOptTable, NULL);
}

/* Most common system directories. */
static const char *system_directories[] = {
	"/",
	"/bin",
	"/etc",
	"/dev",
	"/home",
	"/lib",
	"/opt",
	"/sbin",
	"/sys",
	"/tmp",
	"/usr",
	"/usr/etc",
	"/usr/include",
	"/usr/lib",
	"/usr/local",
	"/usr/local/bin",
	"/usr/local/etc",
	"/usr/local/include",
	"/usr/local/lib",
	"/usr/local/sbin",
	"/usr/local/share",
	"/usr/local/src",
	"/usr/share",
	"/usr/src",
	"/var",
	"/var/cache",
	"/var/db",
	"/var/empty",
	"/var/lib",
	"/var/log",
	"/var/mail",
	"/var/run",
	"/var/spool",
	"/var/tmp",
	NULL
};

static int
isSystemDirectory(const char *dir)
{
	const char **p;

	for (p = system_directories; *p != NULL; p++) {
		if (strcmp(*p, dir) == 0)
			return 1;
	}

	return 0;
}

static int
getMyDetails(void)
{
	if (*smfOptInterfaceName.string == '\0') {
		if ((smfOptInterfaceName.string = malloc(DOMAIN_STRING_LENGTH)) == NULL)
			return -1;
		networkGetMyName(smfOptInterfaceName.string);
	}

	if (*smfOptInterfaceIp.string == '\0' || isReservedIP(smfOptInterfaceIp.string, IS_IP_THIS_HOST|IS_IP_LOCALHOST)) {
		if (smfOptInterfaceIp.initial != smfOptInterfaceIp.string)
			free(smfOptInterfaceIp.string);

		if ((smfOptInterfaceIp.string = malloc(IPV6_STRING_LENGTH)) == NULL)
			return -1;

		networkGetHostIp(smfOptInterfaceName.string, smfOptInterfaceIp.string);
	}

	return 0;
}

/*
 * Normally the last thing called from main(). The function return
 * values are suitable as process exit values. The function assumes
 * that the appropriate system log has already been opened.
 *
 * Its the caller's responsiblity to install an atexit() handler
 * that calls smfAtExitCleanUp(). smfMainStart() does not install
 * the handler itself, because the atexit() handlers are executed
 * in reverse order and the caller's atexit() handler may need to
 * lock the smfMutex, in case other threads are still running during
 * exit().
 *
 * @param smf
 *	A pointer to an smf information structure.
 *
 * @return
 *	Zero on success; otherwise 1 on error.
 *
 * @global-out pthread_mutex_t smfMutex;
 *
 * @side-effects umask changed
 * @side-effects QUIT handler set
 * @side-effects process ownership changed
 * @side-effects working directory changed
 * @side-effects standard file pointers closed or redirected
 */
int
smfMainStart(smfInfo *smf)
{
	FILE *fp;
	char *buffer;
	int fd, lastslash;

	smfDesc = smf;
	getMyDetails();
	smfSetFlags(NULL);

        smfLog(SMF_LOG_INFO, "interface-name=%s interface-ip=%s", smfOptInterfaceName.string, smfOptInterfaceIp.string);

	if (smfLogDetail & SMF_LOG_LIBMILTER)
		smfi_setdbg(100);

	if (signal(SIGQUIT, smfSignalExit) == SIG_ERR) {
		syslog(LOG_ERR, "failed to set SIGQUIT handler: %s (%d)", strerror(errno), errno);
		return 1;
	}

	/* Set the working directory to some place we should have the
	 * permissions to write to. Especially important if you want
	 * to get core dumps while debugging.
	 *
	 * *** Should we consider a chroot() too? ***
	 */
	if (chdir(smfOptWorkDir.string)) {
		syslog(LOG_ERR, "failed to set working directory to %s: %s (%d)", smfOptWorkDir.string, strerror(errno), errno);
		return 1;
	}

	(void) umask(S_IXUSR | S_IWGRP | S_IXGRP | S_IWOTH | S_IXOTH);

	/* Assert that the pid file's directory, typically /var/run/milter
	 * exists. On OpenBSD and possibly others, the contents of /var/run
	 * are removed when the server starts.
	 */
	lastslash = strlrcspn(smfOptPidFile.string, strlen(smfOptPidFile.string), "/");
	if ((buffer = malloc(lastslash)) == NULL) {
		syslog(LOG_ERR, "malloc() failed: %s (%d)", strerror(errno), errno);
		return 1;
	}

	TextCopy(buffer, lastslash, smfOptPidFile.string);

	/* Don't create nor change the file permissions of a system directory.
	 * This is a safe guard against careless changes of the default value
	 * for smfOptPidFile. For example, if /var/run/milter.pid was used
	 * instead, you don't want to alter the /var/run directory itself.
	 */
	if (!isSystemDirectory(buffer)) {
		if (mkpath(buffer) || smfSetFileOwner(smf, buffer) || chmod(buffer, S_IRUSR|S_IWUSR|S_IXUSR|S_IXGRP|S_IXOTH)) {
			syslog(LOG_ERR, "failed to create \"%s\": %s (%d)", buffer, strerror(errno), errno);
			free(buffer);
			return 1;
		}
	}

	free(buffer);

	if ((fd = open(smfOptPidFile.string, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0) {
		/* Is the proces denoted by the .pid file still running? */
		pid_t old_pid = pidLoad(smfOptPidFile.string);
		errno = 0;
		if (0 < old_pid && (getpgid(old_pid) != (pid_t) -1 || errno != ESRCH)) {
			syslog(LOG_ERR, "pid file \"%s\": process %d exists", smfOptPidFile.string, old_pid);
			return 1;
		}

		/* The previous instance appears to have died, overwite the .pid file. */
		if ((fd = open(smfOptPidFile.string, O_WRONLY|O_TRUNC, 0644)) < 0) {
			syslog(LOG_ERR, "pid file \"%s\": %s (%d)", smfOptPidFile.string, strerror(errno), errno);
			return 1;
		}
	}

	if ((fp = fdopen(fd, "wb")) == NULL) {
		syslog(LOG_ERR, "fdopen() failed: %s (%d)", strerror(errno), errno);
		close(fd);
		return 1;
	}

	(void) fprintf(fp, "%d\n", getpid());
	(void) fclose(fp);

	if (pthread_mutex_init(&smfMutex, NULL)) {
		syslog(LOG_ERR, "failed to initialise mutex");
		return 1;
	}

	/* Figure out libmilter runtime version. */
#ifndef HAVE_SMFI_VERSION
	for ( ; 2 <= smf->handlers.xxfi_version; smf->handlers.xxfi_version--) {
		if (smfi_register(smf->handlers) == MI_SUCCESS)
			break;
	}

	if (smf->handlers.xxfi_version < 2) {
		syslog(LOG_ERR, "smfi_register() failed");
		return 1;
	}
#endif

	if (smfi_register(smf->handlers) == MI_FAILURE) {
		syslog(LOG_ERR, "smfi_register() failed");
		return 1;
	}

	if (smfi_setconn((char *) smfOptMilterSocket.string) == MI_FAILURE) {
		syslog(LOG_ERR, "smfi_setconn(%s) failed", smfOptMilterSocket.string);
		return 1;
	}

#ifdef HAVE_SMFI_SETBACKLOG
	if (smfi_setbacklog(smfOptMilterQueue.value) == MI_FAILURE) {
		syslog(LOG_ERR, "smfi_setbacklog(%ld) failed", smfOptMilterQueue.value);
		return 1;
	}
#endif
#ifdef HAVE_SMFI_OPENSOCKET
	/* Remove, if necessary, an old unix domain socket. Open the new
	 * socket, be it a unix domain or internet type.
	 */
	if (smfi_opensocket(1) == MI_FAILURE) {
		syslog(LOG_ERR, "smfi_opensocket() failed for \"%s\": %s (%d)", smfOptMilterSocket.string, strerror(errno), errno);
		return 1;
	}
#else
	if (strncmp(smfOptMilterSocket.string, "unix:", 5) == 0)
		(void) unlink(smfOptMilterSocket.string + 5);
	else if (strncmp(smfOptMilterSocket.string, "local:", 6) == 0)
		(void) unlink(smfOptMilterSocket.string + 6);
#endif
	/* Don't need these and they just use up resources. There are
	 * two schools of throught here: leave the standard files open,
	 * but redirected to /dev/null to avoid possible errors should
	 * the milter or C library read or write on standard I/O; the
	 * other is to close them to free up the file descriptors, which
	 * may be required in an I/O intensive milter.
	 */
	switch (smf->standardIO) {
	case SMF_STDIO_CLOSE:
		fclose(stderr);
		fclose(stdout);
		fclose(stdin);
		break;
	case SMF_STDIO_IGNORE:
		(void) freopen("/dev/null", "a", stderr);
		(void) freopen("/dev/null", "a", stdout);
		(void) freopen("/dev/null", "r", stdin);
		break;
	}

#ifdef HAVE_SMFI_OPENSOCKET
	/* Set the process owner after we have created any special files
	 * or sockets in root only directories, like /var/run, /var/db, etc.
	 */
	if (getuid() == 0) {
		(void) smfSetFileOwner(smf, smfOptMilterSocket.string);
		(void) smfSetFileOwner(smf, smfOptPidFile.string);
		smfSetProcessOwner(smf);
	}
#endif

	if (smfLogDetail & SMF_LOG_DEBUG)
		rlimits();

	/* Write version information to log. */
	syslog(LOG_INFO, "%s/%d.%d.%d %s", smf->package, smf->major, smf->minor, smf->build, smf->copyright);
	syslog(LOG_INFO, "%s/%d.%d.%d %s", LibSnert.package, LibSnert.major, LibSnert.minor, LibSnert.build, LibSnert.copyright);
#ifdef HAVE_SMFI_VERSION
{
	unsigned major, minor, patch;
	(void) smfi_version(&major, &minor, &patch);
	syslog(LOG_INFO, "libmilter version %d.%d.%d", major, minor, patch);
}
#else
	syslog(LOG_INFO, "libmilter version %d", SMFI_VERSION);
#endif
#ifdef HAVE_SQLITE3_H
	syslog(LOG_INFO, "SQLite %s Public Domain by D. Richard Hipp", sqlite3_libversion());
#endif
#ifdef HAVE_DB_H
# if DB_VERSION_MAJOR > 1
	syslog(LOG_INFO, "%s", db_version(NULL, NULL, NULL));
# else
	syslog(LOG_INFO, "Built with old Berkeley DB 1.85");
# endif
#endif
	return smfi_main() == MI_FAILURE;
}

#endif /* HAVE_LIBMILTER_MFAPI_H */

