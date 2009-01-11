/*
 * mailgroup.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * To build:
 *
 *	gcc -O2 -o mailgroup mailgroup.c
 *
 * To install as setuid root:
 *
 *	mv mailgroup /usr/local/sbin
 *	chown root /usr/local/sbin/mailgroup
 *	chmod 4500 /usr/local/sbin/mailgroup
 *
 * Add to /etc/mail/aliases and rebuild aliases.db:
 *
 *	groupname: "|/usr/local/sbin/mailgroup groupname"
 * 
 * Add to /etc/mail/sendmail.mc and rebuild sendmail.cf:
 *
 *	define(`LOCAL_SHELL_FLAGS', `eu9S') 
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <errno.h>
#include <sys/file.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>

char usage[] =
"usage: mailgroup group <message\n"
"\n"
"group\t\tdeliver message to the given group id or name\n"
"message\t\tthe message to deliver is read from standard input\n"
"\n"
"mailgroup/1.0 Copyright 2004 by Anthony Howe.  All rights reserved.\n"
;

static char return_path[256];
static char *sendmail = NULL;
static char *which_sendmail[] = {
	"/usr/libexec/sendmail/sendmail",
	"/usr/libexec/sendmail",
	"/usr/local/sbin/sendmail",
	"/usr/sbin/sendmail",
	NULL
};

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

	return i;
}

deliver(FILE *tmp, char *name)
{
	char redirect[512];

	rewind(tmp);
	
	snprintf(redirect, sizeof (redirect), "%s -f'%s' %s", sendmail, return_path, name);

	if (dup2(fileno(tmp), 0)) {
		fprintf(stderr, "failed to dup to zero: %s (%d)\n", strerror(errno), errno);
		return -1;
	}

	if (system(redirect) != 0) {
		fprintf(stderr, "system(\"%s\") failed: %s (%d)\n", redirect, strerror(errno), errno);
		return -1;
	}

	return 0;	
}

int
main(int argc, char **argv)
{
	FILE *tmp;
	int argi, rc;
	struct group *gr;
	struct passwd *pw;
	char *stop, **mem;
	static char buffer[BUFSIZ];

	if (getuid() != 0)
		fprintf(stderr, "process uid=%d gid=%d\n", getuid(), getgid());
	
	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-')
			break;
			
		switch (argv[argi][1]) {
		case 's':
			sendmail = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;		
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return EX_USAGE;
		}
	}

	if (argc < argi + 1) {
		fprintf(stderr, usage);
		return EX_USAGE;
	}

	if (sendmail == NULL) {
		char **array;
		struct stat sb;

		for (array = which_sendmail; *array != NULL; array++) {
			if (stat(*array, &sb) == 0) {
				sendmail = *array;
				break;
			}
		}
		
		if (sendmail == NULL) {
			fprintf(stderr, "failed to find sendmail\n");
			fprintf(stderr, usage);
			return EX_USAGE;
		}
	}

#ifdef ENFORCE_LOWER_CASE_GROUP
	snprintf(buffer, sizeof (buffer), argv[argi]);
	for (stop = buffer; *stop != '\0'; stop++)
		*stop = tolower(*stop);
#endif
	
	if ((gr = getgrnam(argv[argi])) == NULL) {
		gid_t group_id = (gid_t) strtol(argv[argi], &stop, 10);
		if (*stop != '\0') {
			fprintf(stderr, "invalid group id\n");
			fprintf(stderr, usage);
			return EX_NOUSER;
		}
		
		if ((gr = getgrgid(group_id)) == NULL) {
			fprintf(stderr, "unknown group id\n");
			fprintf(stderr, usage);
			return EX_NOUSER;
		}
	}

	(void) umask(0177);

	/* Save message into a temporary file. */
	if ((tmp = tmpfile()) == NULL) {
		fprintf(stderr, "failed to create temporary file: %s (%d)\n", strerror(errno), errno);
		return EX_IOERR;
	}

#ifdef NOPE	
	fputs("Resent-From: <postmaster>\n", tmp);
#endif
	/* Strip mailbox From line added by sendmail. */
	if (0 < TextInputLine(stdin, buffer, sizeof (buffer))) {
		if (sscanf(buffer, "From %255s ", return_path) != 1) {
			fprintf(stderr, "failed to parse From line, buffer='%s'\n", buffer);
			return EX_IOERR;
		}		
	}

	/* Save the message. */			
	while (0 <= TextInputLine(stdin, buffer, sizeof (buffer))) {	
		fputs(buffer, tmp);
		fputs("\r\n", tmp);
	}
	
	/* Lookup members of group and attempt local delivery to each one.
	 * In case of a delievery error, continue to attempt delivery to
	 * remaining members.
	 */
	rc = EX_OK;
	setpwent();
	while ((pw = getpwent()) != NULL) {
		if (pw->pw_gid == gr->gr_gid && deliver(tmp, pw->pw_name)) {
			fprintf(stderr, "failed to deliver message to local group \"%s\" member \"%s\"\n", gr->gr_name, pw->pw_name);
			rc = 1;
		}
	}
	endpwent();
	
	/* Secondary group members. */
	for (mem = gr->gr_mem; *mem != NULL; mem++) {
		if (deliver(tmp, *mem)) {
			fprintf(stderr, "failed to deliver message to local group \"%s\" member \"%s\"\n", gr->gr_name, *mem);
			rc = 1;
		}
	}

	(void) fclose(tmp);

	return rc;
}


