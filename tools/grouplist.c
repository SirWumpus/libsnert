/*
 * grouplist.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 *
 * To build:
 *
 *	gcc -o grouplist grouplist.c
 *
 * In sendmail.mc (note the tabs in the R lines):
 *
 *	LOCAL_CONFIG
 *	Kgroup program /usr/local/bin/grouplist
 *
 *	LOCAL_RULESETS
 *	SLocal_check_rcpt        
 *	R$*                             $: $1 $| $>CanonAddr $1
 *	R$* $| $+ <@ $=w .> $*          $: $1 $| group: $( group $2 $: NOGROUP $)
 *	R$* $| $+ <@ $j .> $*           $: $1 $| group: $( group $2 $: NOGROUP $)
 *	R$* $| group: NOGROUP           $: $1        
 *	R$* $| group: $+                $#local $: postmaster
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>

char usage[] =
"usage: grouplist ... group\n"
"\n"
"group\t\tthe group id or name to list\n"
"\n"
"grouplist/1.0 Copyright 2004 by Anthony Howe.  All rights reserved.\n"
;

int
main(int argc, char **argv)
{
	struct group *gr;
	struct passwd *pw;
	char *stop, *fmt, **mem;

	if (argc < 2) {
		fprintf(stderr, usage);
		return EX_USAGE;
	}

	if ((gr = getgrnam(argv[argc-1])) == NULL) {
		gid_t group_id = (gid_t) strtol(argv[argc-1], &stop, 10);
		if (*stop != '\0')
			return EX_NOUSER;
		
		if ((gr = getgrgid(group_id)) == NULL)
			return EX_NOUSER;
	}

	/* Primary group members. */
	fmt = "%s";
	setpwent();
	while ((pw = getpwent()) != NULL) {
		if (pw->pw_gid == gr->gr_gid) {
			fprintf(stdout, fmt, pw->pw_name);
			fmt = ",%s";
		}
	}
	endpwent();

	/* Secondary group members. */
	for (mem = gr->gr_mem; *mem != NULL; mem++) {
		fprintf(stdout, fmt, *mem);
		fmt = ",%s";
	}
	
	fputs("\n", stdout);
	
	return 0;
}


