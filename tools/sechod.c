/*
 * sechod.c
 *
 * Secure Echo Test Server
 *
 * Copyright 2011 by Anthony Howe.  All rights reserved.
 */

#define _NAME			"sechod"

#ifndef ECHO_HOST
#define ECHO_HOST		"0.0.0.0"
#endif

#ifndef ECHO_PORT
#define ECHO_PORT		7
#endif

#ifndef SOCKET_TIMEOUT
#define SOCKET_TIMEOUT		30
#endif

#ifndef INPUT_LINE_SIZE
#define INPUT_LINE_SIZE		128
#endif

#ifndef SSL_DIR
#define SSL_DIR			"/etc/openssl"
#endif

#ifndef CA_PEM_DIR
#define CA_PEM_DIR		SSL_DIR "/certs"
#endif

#ifndef CA_PEM_CHAIN
#define CA_PEM_CHAIN		CA_PEM_DIR "/roots.pem"
#endif

#ifndef KEY_CRT_PEM
#define KEY_CRT_PEM		CA_PEM_DIR "/" _NAME ".pem"
#endif

#ifndef KEY_PASS
#define KEY_PASS		NULL
#endif

#ifndef DH_PEM
#define DH_PEM			CA_PEM_DIR "/dh.pem"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/
#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#ifdef HAVE_OPENSSL_SSL_H
# include <openssl/ssl.h>
# include <openssl/bio.h>
# include <openssl/err.h>
#endif

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/Text.h>

static int debug;
static int running;
static long echo_port = ECHO_PORT;
static char *echo_host = ECHO_HOST;
static long socket_timeout = SOCKET_TIMEOUT;

static char *ca_pem_chain = CA_PEM_CHAIN;
static char *ca_pem_dir = CA_PEM_DIR;
static char *key_crt_pem = KEY_CRT_PEM;
static char *key_pass = KEY_PASS;
static char *dh_pem = NULL;

static char line[INPUT_LINE_SIZE+1];
static unsigned char data[INPUT_LINE_SIZE * 10];
static Buf buffer = { NULL, data, sizeof (data), 0, 0 };

static const char log_io[] = "socket error %s(%d): %s (%d)";
static const char log_internal[] = "%s(%d): %s (%d)";
#define __F_L__			   __FILE__, __LINE__

static char usage[] =
"usage: " _NAME " [-v][-c ca_pem][-C ca_dir][-d dh_pem][-k key_crt_pem][-K key_pass]\n"
"             [-h host[:port]][-p port][-t seconds]\n"
"\n"
"-c ca_pem\tCertificate Authority root certificate chain file;\n"
"\t\tdefault " CA_PEM_CHAIN "\n"
"-C dir\t\tCertificate Authority root certificate directory;\n"
"\t\tdefault " CA_PEM_DIR "\n"
"-d dh_pem\tDiffie-Hellman parameter file\n"
"-h host[:port]\tECHO host and optional port to contact; default " ECHO_HOST "\n"
"-k key_crt_pem\tprivate key and certificate chain file;\n"
"\t\tdefault " KEY_CRT_PEM "\n"
"-K key_pass\tpassword for private key; default no password\n"
"-p port\t\tECHO port to connect to; default " QUOTE(ECHO_PORT) "\n"
"-t seconds\tsocket timeout in seconds; default " QUOTE(SOCKET_TIMEOUT) "\n"
"-v\t\tverbose debug messages to standard error\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

#undef syslog

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

void
signal_exit(int signum)
{
	running = 0;
}

int
echo_server(SOCKET fd)
{
	long length;

	for (running = 1; running; ) {
		if (socket3_has_input(fd, socket_timeout) != 0) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			break;
		}

		if ((length = socket3_read(fd, data, sizeof (data)-1, NULL)) < 0) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			break;
		}

		if (length == 0)
			break;

		if (0 < debug) {
			data[length] = '\0';
			syslog(LOG_DEBUG, "%ld:%s", length, data);
		}

		if (data[0] == '.' && isalpha(data[1]))
			return 1;

		if (socket3_write(fd, data, length, NULL) != length) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			break;
		}
	}

	return 0;
}

int
main(argc, argv)
int argc;
char **argv;
{
	char *arg, *stop;
	SOCKET echo, client;
	SocketAddress *address;
	int argi, rc = EXIT_FAILURE;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'c':
			ca_pem_chain = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'C':
			ca_pem_dir = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'd':
			dh_pem = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'k':
			key_crt_pem = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'K':
			key_pass = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'h':
			echo_host = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'p':
			arg = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if ((echo_port = strtol(arg, &stop, 10)) <= 0 || *stop != '\0') {
				fprintf(stderr, "invalid ECHO port number\n%s", usage);
				return EX_USAGE;
			}
			break;
		case 't':
			arg = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			if ((socket_timeout = strtol(arg, &stop, 10)) <= 0 || *stop != '\0') {
				fprintf(stderr, "invalid socket timeout value\n%s", usage);
				return EX_USAGE;
			}
			break;
		case 'v':
			debug++;
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return EX_USAGE;
		}
	}

	if (0 < debug) {
		LogSetProgramName(_NAME);
		LogOpen("(standard error)");

		if (1 < debug)
			socket3_set_debug(debug);
	}

	socket_timeout *= 1000;

	/* Prevent SIGPIPE from terminating the process. */
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}

#ifdef OFF
	/* Catch termination signals for a clean exit. */
	if (signal(SIGINT, signal_exit) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}
	if (signal(SIGQUIT, signal_exit) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}
	if (signal(SIGTERM, signal_exit) == SIG_ERR) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}
#endif
	if (socket3_init_tls(ca_pem_dir, ca_pem_chain, key_crt_pem, key_pass, dh_pem)) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}

	syslog(LOG_INFO, "connecting to host=%s port=%ld", echo_host, echo_port);

	if ((address = socketAddressCreate(echo_host, echo_port)) == NULL) {
		syslog(LOG_ERR, "failed to find host %s:%ld", echo_host, echo_port);
		goto error1;
	}

	if ((echo = socket3_server(address, 1, socket_timeout)) < 0) {
		syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
		goto error2;
	}

	(void) fileSetCloseOnExec(echo, 1);
	(void) socket3_set_linger(echo, 0);

	for (running = 1; running; ) {
		if ((client = socket3_accept(echo, NULL)) < 0) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			continue;
		}

		while (echo_server(client)) {
			if (0 < TextInsensitiveStartsWith((char *)data, ".starttls")) {
				syslog(LOG_INFO, "starting TLS...");

				if (socket3_start_tls(client, 1, socket_timeout)) {
					syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
					break;
				}

				syslog(LOG_INFO, "TLS started");
			}
		}

		socket3_close(client);
	}

	rc = EXIT_SUCCESS;
error3:
	socket3_close(echo);
error2:
	free(address);
error1:
	socket3_fini();
error0:
	return rc;
}
