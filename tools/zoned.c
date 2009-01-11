/*
 * zoned.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * An inetd server to help maintain secondary DNS zone lists.
 *
 * BEWARE THAT THIS SERVICE IS A SECURITY RISK IF THE PORT USED BY
 * ZONED IS NOT PROPERLY PROTECTED BY A FIREWALL AND/OR HOSTS.ALLOW.
 *
 * Anthony Howe <achowe@snert.com>
 *
 * To build:
 *
 *	gcc -o zoned zoned.c -lpam
 *
 * To install:
 *
 *	mv zoned /usr/local/libexec
 *
 * Linux PAM configuration (not required for FreeBSD):
 *
 *	zoned  auth      required  pam_unix.so  try_first_pass
 *	zoned  password  required  pam_unix.so
 *
 * Recommended /etc/services entry:
 *
 *	zoned	323/tcp
 *
 * /etc/hosts.allow entry
 *
 *	zoned : 127.0.0.1 82.97.1.254
 *
 * /etc/inted.conf entry (FreeBSD filepath):
 *
 *	zoned  stream  tcp  nowait  root  /usr/local/libexec/zoned  zoned
 */

#ifndef ZONE_LIST_FILE
#define ZONE_LIST_FILE		"/var/named/named.conf"
#endif

#ifndef ZONE_FORMAT
#define ZONE_FORMAT		"zone \"%s\" {\n\ttype slave;\n\tfile \"slave/%s\";\n\tmasters { %s; };\n};\n"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

/* Simple secondary zone list management.
 *
 * > LOGIN $login $password
 * < +OK LOGIN
 * < -NO LOGIN $reason
 *
 * > ADD $domain $master
 * < +OK ADD
 * < -NO ADD $reason
 *
 * > SUB $domain
 * < +OK SUB
 * < -NO SUB $reason
 *
 * > LIST
 * $domain1 $master1
 * $domain2 $master2
 * ...
 * < +OK LIST
 * < -NO LIST $reason
 *
 * > QUIT
 * < +OK QUIT
 */

#include <pwd.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/file.h>

#if defined(__linux__) || defined(__FreeBSD__)
#define HAVE_PAM
#endif

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

static char buffer[BUFSIZ];
static char *zonelist = ZONE_LIST_FILE;

/***********************************************************************
 *	Routines
 ***********************************************************************/

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

	syslog(LOG_DEBUG, line);

	return i;
}

void
TextUpperWord(char *buffer)
{
	for ( ; isalpha(*buffer); buffer++)
		*buffer = toupper(*buffer);
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

typedef char *(*CommandFunction)(int, char **);

struct command {
	char *command;
	CommandFunction function;
};

#define NULL_STATE		((struct command *) 0)

char *cmdLogin(int, char **);
char *cmdAdd(int, char **);
char *cmdSub(int, char **);
char *cmdHelp(int, char **);
char *cmdList(int, char **);
char *cmdNoop(int, char **);
char *cmdQuit(int, char **);
char *cmdNope(int, char **);

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
	{ "HELP", cmdHelp },
	{ "LIST", cmdList },
	{ "NOOP", cmdNoop },
	{ "QUIT", cmdQuit },
	{ 0, 0 }
};

static struct command *state = state0;

char *
cmdNoop(int argn, char **args)
{
	return NULL;
}

char *
cmdNope(int argn, char **args)
{
	return "command not valid";
}

char *
cmdHelp(int argn, char **args)
{
	printf("  LOGIN username password\r\n");
	printf("  ADD domain master\r\n");
	printf("  SUB domain\r\n");
	printf("  LIST\r\n");
	printf("  QUIT\r\n");

	return NULL;
}

char *
cmdQuit(int argn, char **args)
{
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
cmdLogin(int argn, char **args)
{
	char *msg;
	struct passwd *pw;
#ifdef HAVE_PAM
	pam_handle_t *pamh;
	struct pam_conv pamc;
#endif
	if (argn != 3) {
		msg = "syntax error";
		goto error0;
	}

#ifdef HAVE_PAM
	pamc.conv = pam_conversation;
	pamc.appdata_ptr = args[2];

	if (pam_start("zoned", args[1], &pamc, &pamh) != PAM_SUCCESS) {
		msg = "PAM failed to start";
		goto error0;
	}

	if (pam_authenticate(pamh, PAM_SILENT) != PAM_SUCCESS) {
		msg = "invalid username and/or password";
		state = NULL_STATE;
		goto error1;
	}
#endif
	if ((pw = getpwnam(args[1])) == NULL) {
		msg = "invalid username and/or password";
		state = NULL_STATE;
		goto error0;
	}

#ifndef HAVE_PAM
	if (!checkPassword(args[2], pw->pw_passwd)) {
		msg = "invalid username and/or password";
		state = NULL_STATE;
		goto error0;
	}
#endif

	if (getuid() == 0) {
		(void) setgid(pw->pw_gid);
		(void) setuid(pw->pw_uid);
	}

	syslog(LOG_NOTICE, "LOGIN %s uid=%d gid=%d", args[1], getuid(), getgid());
	state = state1;
	msg = NULL;
#ifdef HAVE_PAM
error1:
	pam_end(pamh, 0);
#endif
error0:
	return msg;
}

long
readComments(FILE *fp, FILE *copy)
{
	int ch, state = 0;
	long offset = ftell(fp);

	for (state = 1; state != 0; ) {
		if ((ch = fgetc(fp)) == EOF)
			return -1;

		switch (state) {
		case 1:
			if (isspace(ch)) {
				/* Leading whitespace. */
			} else if (ch == '#') {
				/* Start of shell style comment. */
				state = '#';
			} else if (ch == '/') {
				/* Start of C/C++ style comment. */
				state = '/';
			} else {
				/* Not a comment or leading whitespace, leave. */
				ungetc(ch, fp);
				state = 0;
				continue;
			}
			break;
		case '#':
			/* Read shell style comment until newline. */
			if (ch == '\n')
				state = 1;
			break;
		case '/':
			if (ch == '/') {
				/* Read C++ style comment until newline. */
				state = '#';
			} else if (ch == '*') {
				/* Start of C style comment block. */
				state = 2;
			} else {
				/* Backup 2 characters, and leave. */
				fseek(fp, -2, SEEK_CUR);
				if (copy != NULL)
					fseek(copy, -1, SEEK_CUR);
				state = 0;
				continue;
			}
			break;
		case 2:
			/* Read C style comment block until star-slash. */
			if (ch == '*')
				state = 3;
			break;
		case 3:
			if (ch == '/') {
				/* End of C comment block, star-slash. */
				state = 1;
			} else {
				/* Still in C comment block. */
				state = 2;
			}
			break;
		}

		if (copy != NULL)
			fputc(ch, copy);
	}

	return ftell(fp) - offset;
}

/*
 * Read into buffer[] the next statement block, removing the comments.
 * Its assumed that the buffer is large enough to hold a large statement
 * block.
 *
 * The copy file pointer is used to save a copy of everything read into
 * a temporary file used for inplace edits.
 */
long
readStatement(FILE *fp, FILE *copy, long *start, long *stop)
{
	char *buf;
	int ch, braces;

	buf = buffer;

	/* Skip leading whitespace and comments. */
	if (readComments(fp, copy) == -1)
		return -1;

	/* Remember start of statement. */
	if (start != NULL)
		*start = ftell(fp);

	/* Find end of statement. */
	for (braces = 0; 0 <= braces; ) {
		if (readComments(fp, copy) == -1)
			return -1;

		if ((ch = fgetc(fp)) == EOF) {
			if (buffer < buf)
				break;
			return -1;
		}

		if (copy != NULL)
			fputc(ch, copy);

		if (sizeof (buffer)-1 <= buf - buffer)
			return -1;

		*buf++ = ch;

		switch (ch) {
		case '{':
			/* Count nested braces. */
			braces++;
			break;
		case '}':
			braces--;
			break;
		case ';':
			/* Have we found the semi-colon for the end of statement? */
			if (braces == 0) {
				/* Skip trailing whitespace. */
				while ((ch = fgetc(fp)) != EOF && isspace(ch)) {
					if (copy != NULL)
						fputc(ch, copy);
				}

				ungetc(ch, fp);

				/* End loop. */
				braces = -1;
			}
		}
	}

	*buf ='\0';

	if (stop != NULL)
		*stop = ftell(fp);

	return buf - buffer;
}

char *
cmdAdd(int argn, char **args)
{
	int i;
	FILE *fp;
	char *msg;
	long length;

	if (argn != 3) {
		msg = "syntax error";
		goto error0;
	}

	if ((fp = fopen(zonelist, "a+")) == NULL) {
		msg = "cannot open zone list";
		goto error0;
	}

	flock(fileno(fp), LOCK_EX);

	fseek(fp, 0, SEEK_SET);
	length = strlen(args[1]);

	while (readStatement(fp, NULL, NULL, NULL) != -1) {
		/* Did we find a `zone' statement? */
		if (strncasecmp(buffer, "zone", 4) != 0)
			continue;

		/* Yep, check for existing domain. */
		i = 4 + strspn(buffer+4, " \"");
		if (strncasecmp(buffer+i, args[1], length) == 0) {
			msg = "domain already exists";
			goto error1;
		}
	}

	/* Domain doesn't exist, append it to list. */
	fprintf(fp, ZONE_FORMAT, args[1], args[1], args[2]);

	msg = NULL;
error1:
	fclose(fp);
error0:
	return msg;
}

char *
cmdSub(int argn, char **args)
{
	char *msg;
	FILE *fp, *tmp;
	int found, ch, i;
	long start, stop, start2, stop2, length;

	if (argn != 2) {
		msg = "syntax error";
		goto error0;
	}

	if ((fp = fopen(zonelist, "r+")) == NULL) {
		msg = "cannot open zone list";
		goto error0;
	}

	flock(fileno(fp), LOCK_EX);

	if ((tmp = tmpfile()) == NULL) {
		msg = "failed to create temporary file";
		goto error1;
	}

	length = strlen(args[1]);

	/* Find start and finish of domain zone declaration
	 * and copy everything into the temporary file.
	 */
	for (found = 0; ; ) {
		if (readStatement(fp, tmp, &start, &stop) == -1)
			break;

		if (!found) {
			if (strncasecmp(buffer, "zone", 4) != 0)
				continue;

			i = 4 + strspn(buffer+4, " \"");
			if (strncasecmp(buffer+i, args[1], length) == 0) {
				start2 = start;
				stop2 = stop;
				found = 1;
			}
		}
	}

	if (!found) {
		msg = "domain does not exist";
		goto error2;
	}

	/* Now modify the original file in place reading the
	 * tail of the temporary file back over top of the
	 * zone statement to be deleted.
	 */
	fseek(fp, start2, SEEK_SET);
	fseek(tmp, stop2, SEEK_SET);

	while ((ch = fgetc(tmp)) != EOF)
		fputc(ch, fp);

	/* Finally truncate the original file. */
	fflush(fp);
	ftruncate(fileno(fp), ftell(fp));

	msg = NULL;
error2:
	fclose(tmp);
error1:
	fclose(fp);
error0:
	return msg;
}

char *
cmdList(int argn, char **args)
{
	FILE *fp;
	int i, j, plength;
	char *msg, *ip, *next, *prefix;

	if (argn == 2) {
		prefix = args[1];
		plength = strlen(prefix);
	} else {
		prefix = NULL;
	}

	syslog(LOG_NOTICE, "LIST %s", prefix == NULL ? "" : prefix);

	if ((fp = fopen(zonelist, "r")) == NULL) {
		msg = "cannot open zone list";
		goto error0;
	}

	flock(fileno(fp), LOCK_SH);

	while (readStatement(fp, NULL, NULL, NULL) != -1) {
		/* Did we find a `zone' statement? */
		if (strncasecmp(buffer, "zone", 4) != 0)
			continue;

		/* Find index of domain token and terminate string. */
		i = 4 + strspn(buffer+4, " \t\"");
		j = i + strcspn(buffer+i, " \t\"");
		buffer[j] = '\0';

		/* Find `master' statement. */
		if ((next = strstr(buffer+j+1, "master")) == NULL)
			continue;

		/* Find index of master ip list and terminate string. */
		j = 6 + strcspn(next+6, "{");
		if (next[j] != '{')
			continue;

		j += strspn(next+j, "{ \t\r\n");

		if (prefix != NULL && strncasecmp(buffer+i, prefix, plength) != 0)
			continue;

		/* Write domain and master ip strings to client. */
		printf("%s,", buffer+i);
		for (ip = next+j; *ip != '}'; ) {
			j = strcspn(ip, ";");
			ip[j] = '\0';
			printf(ip);

			ip += j + 1;
			ip += strspn(ip, " \t\r\n");
			if (*ip != '}')
				printf("; ");
		}
		printf("\r\n");
	}

	msg = NULL;
	fclose(fp);
error0:
	return msg;
}

/***********************************************************************
 *	Server
 ***********************************************************************/

int
server(void)
{
	int i, argn;
	struct command *s;
	char *msg, *arg, *args[4];

	(void) setvbuf(stdout, NULL, _IOLBF, 0);

	for (state = state0; state != NULL_STATE; ) {
		if (TextInputLine(stdin, buffer, sizeof (buffer)) < 0) {
			syslog(LOG_WARNING, "premature EOF");
			break;
		}

		/* Split the line into space separated arguments, max 3. */
		arg = buffer;
		for (argn = 0; (args[argn] = TextToken(arg, &arg, ", \t", 0)) != NULL && argn < 3; argn++)
			;

		/* No command has more than 3 arguments. */
		if (3 < argn) {
			printf("-NO %s syntax error\r\n", args[0]);
		} else {

			TextUpperWord(args[0]);

			for (s = state; s->command != (char *) 0; ++s) {
				if (strcmp(args[0], s->command) == 0) {
					msg = (*s->function)(argn, args);
					if (msg == NULL)
						printf("+OK %s\r\n", s->command);
					else
						printf("-NO %s %s\r\n", s->command, msg);
					break;
				}
			}

			if (s->command == (char *) 0)
				printf("-NO %s unknown command\r\n", args[0]);
		}

		/* Release arguments. */
		for (i = 0; i < argn; i++)
			free(args[i]);
	}

	return 0;
}

int
main(int argc, char **argv)
{
	while(getopt(argc, argv, "f:") != -1) {
		switch (optopt) {
		case 'f':
			zonelist = optarg;
			break;
		default:
			fprintf(stderr, "usage: zoned [-f zone-list]\n");
			return 2;
		}
	}

	openlog("zoned", LOG_PID, LOG_DAEMON);
	server();
	closelog();

	return 0;
}

