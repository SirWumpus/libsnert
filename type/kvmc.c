/*
 * kvmc.c
 *
 * Key-Value Map
 *
 * Copyright 2006 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/type/kvm.h>
#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

static char usage[] =
"usage: kvmc [-rv][-h host[,port]][-t timeout] table [command [arguments...]]\n"
"\n"
"-h host,port\tthe socket-map host and optional port number.\n"
"-r\t\tthe socket map is read-only.\n"
"-t timeout\tsocket timeout in seconds, default 60\n"
"-v\t\tverbose logging to the user log\n"
"\n"
"The following is a summary commands and their arguments:\n"
"\n"
" key\t\tfetch using original sendmail socket map get\n"
" GET key\tget using socket map extended protocol\n"
" PUT key value\tput using socket map extended protocol\n"
" REMOVE key\tremove using socket map extended protocol\n"
"\n"
"If no command is given on the command line argument, then read then from\n"
"command and arguments from standard input until end of file.\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

/* " LIST\t\tlist all using socket map extended protocol\n" */


int read_only;
long timeout = SOCKET_CONNECT_TIMEOUT;
char socketmap[256] = "socketmap" KVM_DELIM_S "127.0.0.1," KVM_PORT_S;
char buffer[BUFSIZ];

#if ! defined(__MINGW32__)
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
#endif

int
process(kvm *map, int argc, char **argv)
{
	kvm_data key, value;
	int rc = EXIT_FAILURE;

	key.data = (unsigned char *) argv[(1 < argc)];
	key.size = (unsigned long) strlen((char *) key.data);

	if (TextInsensitiveCompare("GET", argv[0]) == 0) {
		switch (map->get(map, &key, &value)) {
		case KVM_ERROR:
			syslog(LOG_ERR, "GET '%s' failed", key.data);
			/*@fallthrough@*/
		case KVM_NOT_FOUND:
			rc = EXIT_FAILURE;
			break;
		case KVM_OK:
			printf("%s\n", value.data);
			rc = EXIT_SUCCESS;
			free(value.data);
			break;
		}
	} else if (TextInsensitiveCompare("PUT", argv[0]) == 0) {
		if (argc < 3) {
			(void) fprintf(stderr, usage);
			exit(64);
		}

		value.data = (unsigned char *) argv[2];
		value.size = (unsigned long) strlen((char *) value.data);

		if (map->put(map, &key, &value) == KVM_ERROR) {
			syslog(LOG_ERR, "PUT '%s' '%s' failed", key.data, value.data);
			rc = EXIT_FAILURE;
		} else {
			rc = EXIT_SUCCESS;
		}
	} else if (TextInsensitiveCompare("REMOVE", argv[0]) == 0) {
		if ((rc = map->remove(map, &key)) == KVM_ERROR) {
			syslog(LOG_ERR, "REMOVE '%s' failed", key.data);
			rc = EXIT_FAILURE;
		} else {
			rc = EXIT_SUCCESS;
		}
	} else {
		switch (map->fetch(map, &key, &value)) {
		case KVM_ERROR:
			syslog(LOG_ERR, "GET '%s' failed", key.data);
			/*@fallthrough@*/
		case KVM_NOT_FOUND:
			rc = EXIT_FAILURE;
			break;
		case KVM_OK:
			printf("%s\n", value.data);
			rc = EXIT_SUCCESS;
			free(value.data);
			break;
		}
	}

	fflush(stdout);

	return rc;
}

int
main(int argc, char **argv)
{
	kvm *map;
	int ch, rc;

#ifdef USE_SYSLOG
	openlog("kvmc", LOG_PID, LOG_USER);
#else
	LogSetProgramName("kvmc");
	LogOpen("(standard error)");
	LogSetLevel(LOG_INFO);
#endif
	while ((ch = getopt(argc, argv, "rh:t:v")) != -1) {
		switch (ch) {
		case 'h':
			(void) TextCopy(
				socketmap + sizeof ("socketmap" KVM_DELIM_S)-1,
				sizeof (socketmap) - sizeof ("socketmap" KVM_DELIM_S) - 1,
				optarg
			);
			break;
		case 'r':
			read_only = KVM_MODE_READ_ONLY;
			break;
		case 't':
			timeout = strtol(optarg, NULL, 10) * 1000;
			break;
		case 'v':
			LogSetProgramName("kvmc");
			LogOpen("(standard error)");
			LogSetLevel(LOG_DEBUG);
			socketSetDebug(1);
			kvmDebug(1);
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

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() %s (%d)", strerror(errno), errno);
		exit(71);
	}

	if ((map = kvmOpen(argv[optind], socketmap, read_only)) == NULL) {
		syslog(LOG_ERR, "kvmOpen(\"%s\", \"%s\", %x) failed: %s (%d)", argv[optind], socketmap, read_only, strerror(errno), errno);
		exit(71);
	}

	socketSetTimeout(map->_kvm, timeout);

	rc = EXIT_SUCCESS;

	if (++optind < argc) {
		rc = process(map, argc-optind, argv+optind);
	} else {
		while (TextInputLine(stdin, buffer, sizeof (buffer))) {
			Vector args;

			if (*buffer == '.')
				break;

			args = TextSplit(buffer, NULL, 0);
			if (args != NULL) {
				if (process(map, VectorLength(args), (char **) VectorBase(args)))
					rc = EXIT_FAILURE;
				VectorDestroy(args);
			}
		}
	}

	map->close(map);

	return rc;
}

/***********************************************************************
 *** END
 ***********************************************************************/

