/*
 * filed.c
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 *
 * An inetd server for modifying simple key/value files.
 *
 * BEWARE THAT THIS SERVICE IS A SECURITY RISK IF THE PORT USED BY
 * FILED IS NOT PROPERLY PROTECTED BY A FIREWALL AND/OR HOSTS.ALLOW.
 *
 * Anthony Howe <achowe@snert.com>
 *
 * To build for Linux:
 *
 *	gcc -O2 -o filed filed.c -lpam
 *
 * To install:
 *
 *	mv filed /usr/local/libexec
 *
 * Linux PAM configuration (not required for FreeBSD):
 *
 *	filed  auth      required  pam_unix.so  try_first_pass
 *	filed  password  required  pam_unix.so
 *
 * Recommended /etc/services entry:
 *
 *	filed	322/tcp		# LibSnert file update daemon.
 *
 * /etc/hosts.allow entry
 *
 *	filed : 127.0.0.1 82.97.1.254
 *
 * /etc/inted.conf entry (FreeBSD filepath):
 *
 *	filed  stream  tcp  nowait.200  root  /usr/local/libexec/filed  filed
 *
 * After modifying /etc/inted.conf
 *
 *	killall -HUP inetd
 */

#define NDEBUG

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

/* Simple key/value list management.
 *
 * > LOGIN $login $password
 * < +OK LOGIN
 * < -NO LOGIN $reason
 *
 * > FILE $filepath
 * < +OK FILE
 * < -NO FILE $reason
 *
 * > ADD $key [$value]
 * < +OK ADD
 * < -NO ADD $reason
 *
 * > SUB $key
 * < +OK SUB
 * < -NO SUB $reason
 *
 * > LIST [$prefix]
 * $key1 $value1
 * $key2 $value2
 * ...
 * < +OK LIST
 * < -NO LIST $reason
 *
 * > QUIT
 * < +OK QUIT
 */

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

#if defined(__linux__) || defined(__FreeBSD__)
#define HAVE_PAM
#endif

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

static char *thisFile;
static int onlyThisFile;
static char buffer[BUFSIZ];
static char tmp[] = "/tmp";

/***********************************************************************
 *	Routines
 ***********************************************************************/

void
TextUpperWord(char *buffer)
{
	for ( ; isalpha(*buffer); buffer++)
		*buffer = toupper(*buffer);
}

long
TextInputLine(FILE *fp, char *line, long size)
{
	long i;

	for (i = 0, --size; i < size; ++i) {
		line[i] = (char) fgetc(fp);

		if (feof(fp) || ferror(fp))
			return -1;

		if (line[i] == '\n') {
			line[i] = '\0';
			if (0 < i && line[i-1] == '\r')
				line[--i] = '\0';
			break;
		}
	}

	line[i] = '\0';

#ifndef NDEBUG
	syslog(LOG_DEBUG, line);
#endif
	return i;
}

/**
 * <p>
 * Given the character following a backslash, return the
 * the character's ASCII escape value.
 * </p>
 * <pre>
 *   bell            \a	0x07
 *   backspace       \b	0x08
 *   escape          \e	0x1b
 *   formfeed        \f	0x0c
 *   linefeed        \n	0x0a
 *   return          \r	0x0d
 *   space           \s	0x20
 *   tab             \t	0x09
 *   vertical-tab    \v	0x0b
 * </pre>
 *
 * @param ch
 *	A character that followed a backslash.
 *
 * @return
 *	The ASCII value of the escape character or the character itself.
 */
int
TextBackslash(char ch)
{
	switch (ch) {
	case 'a': return '\007';
	case 'b': return '\010';
	case 'e': return '\033';
	case 'f': return '\014';
	case 'n': return '\012';
	case 'r': return '\015';
	case 's': return '\040';
	case 't': return '\011';
	case 'v': return '\013';
	}

	return ch;
}

/**
 * <p>
 * Parse the string for the next token. A token consists of characters
 * not found in the set of delimiters. It may contain backslash-escape
 * sequences, which shall be converted into literals or special ASCII
 * characters. It may contain single or double quoted strings, in which
 * case the quotes shall be removed, though any backslash escape
 * sequences within the quotes are left as is.
 * </p>
 *
 * @param string
 *	A quoted string.
 *
 * @param stop
 *	A pointer to a C string pointer. The point were the scan stops
 *	is passed back through this argument. This pointer can be NULL.
 *
 * @param delims
 *	A set of delimiter characters.
 *
 * @param returnEmptyToken
 *	If false then a run of one or more delimeters is treated as a
 *	single delimeter separating tokens. Otherwise each delimeter
 *	separates a token that may be empty.
 *
 *	string		true		false
 *	-------------------------------------------
 *	[a,b,c]		[a] [b] [c]	[a] [b] [c]
 *	[a,,c]		[a] [] [c]	[a] [c]
 *	[a,,]		[a] [] [] 	[a]
 *	[,,]		[] [] []	(null)
 *	[]		[]		(null)
 *
 * @return
 *	An allocated token.
 *
 * @see #TextBackslash(char)
 */
char *
TextToken(const char *string, char **stop, const char *delims, int returnEmptyToken)
{
	char *token, *t;
	const char *s;
	int quote = 0, escape = 0, length;

	if (string == (char *) 0) {
		if (stop != (char **) 0)
			*stop = (char *) 0;
		return (char *) 0;
	}

	/* Skip leading delimiters? */
	if (!returnEmptyToken) {
		/* Find start of next token. */
		string += strspn(string, delims);

		if (*string == '\0') {
			if (stop != (char **) 0)
				*stop = (char *) 0;
			return (char *) 0;
		}
	}

	/* Find end of token. */
	for (s = string; *s != '\0'; ++s) {
		if (escape) {
			escape = 0;
			continue;
		}

		switch (*s) {
		case '"': case '\'':
			quote = *s == quote ? 0 : *s;
			continue;
		case '\\':
			escape = 1;
			if (quote == 0) continue;
			continue;
		}

		if (quote == 0 && strchr(delims, *s) != (char *) 0)
			break;
	}

	token = malloc((s - string) + 1);
	if (token == (char *) 0)
		return (char *) 0;

	/* Copy token, removing quotes and backslashes. */
	for (t = token; string < s; ++string) {
		if (escape) {
			if (quote == 0)
				*t++ = (char) TextBackslash(*string);
			else
				*t++ = *string;
			escape = 0;
			continue;
		}

		switch (*string) {
		case '"': case '\'':
			quote = *string == quote ? 0 : *string;
			continue;
		case '\\':
			escape = 1;
			if (quote == 0) continue;
			break;
		}

		if (quote == 0 && strchr(delims, *string) != (char *) 0)
			break;

		*t++ = *string;
	}
	*t = '\0';

	if (*s == '\0') {
		/* Token found and end of string reached.
		 * Next iteration should return no token.
		 */
		s = (char *) 0;
	} else {
		length = strspn(s, delims);
		if (returnEmptyToken) {
			/* Consume only a single delimter. */
			s += length <= 0 ? 0 : 1;
		} else {
			/* Consume one or more delimeters. */
			s += length;
		}
	}

	if (stop != (char **) 0)
		*stop = (char *) s;

	return token;
}

/***********************************************************************
 *	Commands
 ***********************************************************************/

typedef char *(*CommandFunction)(char *);

struct command {
	char *command;
	CommandFunction function;
};

#define NULL_STATE		((struct command *) 0)

char *cmdLogin(char *);
char *cmdAdd(char *);
char *cmdSub(char *);
char *cmdFile(char *);
char *cmdHelp(char *);
char *cmdList(char *);
char *cmdNoop(char *);
char *cmdQuit(char *);
char *cmdNope(char *);

static struct command state0[] = {
	{ "LOGIN", cmdLogin },
	{ "HELP", cmdHelp },
	{ "NOOP", cmdNoop },
	{ "QUIT", cmdQuit },
	{ 0, 0 }
};

static struct command state1[] = {
	{ "LOGIN", cmdNope },
	{ "ADD", cmdAdd },
	{ "SUB", cmdSub },
	{ "FILE", cmdFile },
	{ "HELP", cmdHelp },
	{ "LIST", cmdList },
	{ "NOOP", cmdNoop },
	{ "QUIT", cmdQuit },
	{ 0, 0 }
};

static struct command *state = state0;

char *
cmdNoop(char *raw)
{
	return NULL;
}

char *
cmdNope(char *raw)
{
	return "command not valid";
}

char *
cmdHelp(char *raw)
{
	printf("  LOGIN username password\r\n");
	printf("  FILE filepath\r\n");
	printf("  ADD key value...\r\n");
	printf("  SUB key\r\n");
	printf("  LIST [prefix]\r\n");
	printf("  QUIT\r\n");

	return NULL;
}

char *
cmdQuit(char *raw)
{
	syslog(LOG_NOTICE, "QUIT");

	state = NULL_STATE;

	return NULL;
}

#ifdef HAVE_PAM
int
pam_conversation(int num_msg, const struct pam_message **msg, struct pam_response **response, void *appdata_ptr)
{
	int i;
	struct pam_response *r;

	if (num_msg <= 0)
		return PAM_CONV_ERR;

	if ((r = malloc(sizeof(struct pam_response) * num_msg)) == NULL)
		return PAM_CONV_ERR;

	for (i = 0; i < num_msg; i++) {
		if (msg[i]->msg_style == PAM_ERROR_MSG) {
			printf("-NO LOGIN %s\r\n", msg[i]->msg);

			/* If there is an error, we don't want to be sending
			 * in anything more, so set pop_state to invalid
			 */
			state = NULL_STATE;
		}

		r[i].resp = NULL;
		r[i].resp_retcode = 0;

		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
			if (state == state0)
				r[i].resp = strdup(appdata_ptr);
		}
	}

	*response = r;

	return PAM_SUCCESS;
}
#else
int
checkPassword(char *key, char *pw_passwd)
{
	char salt[3];

	if (*pw_passwd == '$')
		return strcmp(pw_passwd, crypt(key, pw_passwd)) == 0;

	salt[0] = pw_passwd[0];
	salt[1] = pw_passwd[1];
	salt[2] = '\0';

	return strcmp(pw_passwd, crypt(key, salt)) == 0;
}
#endif


char *
cmdLogin(char *raw)
{
	struct passwd *pw;
#ifdef HAVE_PAM
	pam_handle_t *pamh;
	struct pam_conv pamc;
#endif
	char *msg, *username, *password;

	username = TextToken(raw, &raw, " ", 0);
	if (username == NULL) {
		msg = "username parse error";
		goto error0;
	}

	password = TextToken(raw, NULL, " ", 0);
	if (password == NULL) {
		msg = "password parse error";
		goto error1;
	}

#ifdef HAVE_PAM
	pamc.conv = pam_conversation;
	pamc.appdata_ptr = password;

	if (pam_start("filed", username, &pamc, &pamh) != PAM_SUCCESS) {
		msg = "PAM failed to start";
		goto error2;
	}

	if (pam_authenticate(pamh, PAM_SILENT) != PAM_SUCCESS) {
		msg = "invalid username and/or password";
		state = NULL_STATE;
		goto error3;
	}
#endif
	if ((pw = getpwnam(username)) == NULL) {
		msg = "invalid username and/or password";
		state = NULL_STATE;
		goto error3;
	}

#ifndef HAVE_PAM
	if (!checkPassword(password, pw->pw_passwd)) {
		msg = "invalid username and/or password";
		state = NULL_STATE;
		goto error3;
	}
#endif

	if (getuid() == 0) {
		(void) setgid(pw->pw_gid);
		(void) setuid(pw->pw_uid);
	}

	syslog(LOG_NOTICE, "LOGIN %s uid=%d gid=%d", username, getuid(), getgid());
	state = state1;
	msg = NULL;
error3:
#ifdef HAVE_PAM
	pam_end(pamh, 0);
error2:
#endif
	free(password);
error1:
	free(username);
error0:
	return msg;
}

char *
cmdAdd(char *value)
{
	int i;
	FILE *fp;
	long length;
	char *msg, *key;

	if ((key = TextToken(value, &value, " \t", 0)) == NULL) {
		msg = "syntax error";
		goto error0;
	}

	/* Duplicate value which is current setting in the `buffer',
	 * because we're going to overwrite the buffer below.
	 */
        if (value != NULL && (value = strdup(value)) == NULL) {
                msg = "memory allocation error";
                goto error1;
        }

	syslog(LOG_NOTICE, "%s: ADD %s %s", thisFile, key, value == NULL ? "" : value);

	if ((fp = fopen(thisFile, "a+")) == NULL) {
		msg = "cannot open file";
		goto error2;
	}

	flock(fileno(fp), LOCK_EX);
	fseek(fp, 0, SEEK_SET);
	length = strlen(key);

	while (0 <= TextInputLine(fp, buffer, sizeof (buffer))) {
		if (isspace(buffer[length]) && strncasecmp(buffer, key, length) == 0) {
			msg = "key already exists";
			goto error3;
		}
	}

	/* Key doesn't exist, append it to file. */
	if (value == NULL) {
		/* Allows for adding keys that are really whole lines. */
		fprintf(fp, "%s\n", key);
	} else {
		fprintf(fp, "%s\t%s\n", key, value);
	}

	msg = NULL;
error3:
	fclose(fp);
error2:
	free(value);
error1:
	free(key);
error0:
	return msg;
}

char *
cmdSub(char *raw)
{
	long length;
	int found, ch;
	FILE *fp, *tmp;
	char *msg, *key;

	if ((key = TextToken(raw, &raw, " \t", 0)) == NULL) {
		msg = "syntax error";
		goto error0;
	}

	syslog(LOG_NOTICE, "%s: SUB %s", thisFile, key);

	if ((fp = fopen(thisFile, "r+")) == NULL) {
		msg = "cannot open file";
		goto error1;
	}

	flock(fileno(fp), LOCK_EX);

	if ((tmp = tmpfile()) == NULL) {
		msg = "failed to create temporary file";
		goto error2;
	}

	length = strlen(key);

	/* Copy the source file, looking for /^key[ \t]/ or /^key$/ lines, which are skipped. */
	for (found = 0; 0 <= TextInputLine(fp, buffer, sizeof (buffer)); ) {
		if ((buffer[length] == '\0' || isspace(buffer[length])) && strncasecmp(buffer, key, length) == 0) {
			found = 1;
			continue;
		}

		fputs(buffer, tmp);
		fputs("\n", tmp);
	}

	if (!found) {
		msg = "key does not exist";
		goto error3;
	}

	/* Now copy the temporary file back over top of the original */
	rewind(fp);
	rewind(tmp);

	while ((ch = fgetc(tmp)) != EOF)
		fputc(ch, fp);

	/* Finally truncate the original file. */
	fflush(fp);
	ftruncate(fileno(fp), ftell(fp));

	msg = NULL;
error3:
	fclose(tmp);
error2:
	fclose(fp);
error1:
	free(key);
error0:
	return msg;
}

char *
cmdList(char *raw)
{
	FILE *fp;
	int i, j, plength;
	char *msg, *ip, *next, *prefix;

	if ((prefix = TextToken(raw, &raw, " ", 0)) != NULL)
		plength = strlen(prefix);

	syslog(LOG_NOTICE, "%s: LIST %s", thisFile, prefix == NULL ? "" : prefix);

	if ((fp = fopen(thisFile, "r")) == NULL) {
		syslog(LOG_ERR, "%s: %s (%d)", thisFile, strerror(errno), errno);
		msg = "cannot open file";
		goto error0;
	}

	flock(fileno(fp), LOCK_SH);

	while (0 <= TextInputLine(fp, buffer, sizeof (buffer))) {
		if (*buffer == '\0')
			continue;

		if (prefix == NULL || strncasecmp(buffer + (*buffer == '.'), prefix, plength) == 0) {
			fputs(buffer, stdout);
			fputs("\r\n", stdout);
		}
	}

	msg = NULL;

	fclose(fp);
error0:
	free(prefix);

	return msg;
}

char *
cmdFile(char *raw)
{
	char *msg;
	struct stat sb;

	if (onlyThisFile)
		return "command not valid";

	if (thisFile != NULL)
		free(thisFile);

	if ((thisFile = TextToken(raw, &raw, " ", 0)) == NULL) {
		msg = "syntax error";
		goto error0;
	}

	if (stat(thisFile, &sb)) {
		syslog(LOG_ERR, "%s: %s (%d)\n", thisFile, strerror(errno), errno);
		msg = "does not exist or cannot be accessed";
	} else if (S_ISDIR(sb.st_mode)) {
		msg = "is a directory";
	} else {
		msg = NULL;
	}
error0:
	return msg;
}

/***********************************************************************
 *	Server
 ***********************************************************************/

int
server(void)
{
	int i;
	struct command *s;
	char *msg, *cmd, *remainder;

	(void) setvbuf(stdout, NULL, _IOLBF, 0);

	for (state = state0; state != NULL_STATE; ) {
		if (TextInputLine(stdin, buffer, sizeof (buffer)) < 0) {
			syslog(LOG_WARNING, "premature EOF, connection broken");
			break;
		}

		if ((cmd = TextToken(buffer, &remainder, ", \t", 0)) == NULL) {
			printf("-NO missing command\r\n");
			continue;
		}

		TextUpperWord(cmd);

		for (s = state; s->command != (char *) 0; ++s) {
			if (strcmp(cmd, s->command) == 0) {
				msg = (*s->function)(remainder);
				if (msg == NULL) {
					printf("+OK %s\r\n", cmd);
				} else {
					syslog(LOG_ERR, "%s: %s %s", thisFile, cmd, msg);
					printf("-NO %s %s\r\n", cmd, msg);
				}
				break;
			}
		}

		if (s->command == (char *) 0)
			printf("-NO %s unknown command\r\n", cmd);

		free(cmd);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	while(getopt(argc, argv, "f:") != -1) {
		switch (optopt) {
		case 'f':
			onlyThisFile = 1;
			thisFile = optarg;
			break;
		default:
			fprintf(stderr, "usage: filed [-f filepath]\n");
			return 2;
		}
	}

	openlog("filed", LOG_PID, LOG_DAEMON);
	syslog(LOG_NOTICE, "started...");
	server();
	closelog();

	return 0;
}

