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

#ifndef KEY_CRT_PEM
#define KEY_CRT_PEM		CERT_DIR "/" _NAME ".pem"
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

static char *ca_chain = CA_CHAIN;
static char *cert_dir = CERT_DIR;
static char *key_crt_pem = KEY_CRT_PEM;
static char *key_pass = NULL;
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
"\t\tdefault " CA_CHAIN "\n"
"-C dir\t\tCertificate Authority root certificate directory;\n"
"\t\tdefault " CERT_DIR "\n"
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
#endif

void
signal_exit(int signum)
{
	running = 0;
}

int
echo_server(SOCKET fd, SocketAddress *addr)
{
	long length;
	char host[DOMAIN_STRING_LENGTH], ip[SOCKET_ADDRESS_STRING_SIZE];

	(void) socketAddressGetName(addr, host, sizeof (host));
	(void) socketAddressGetString(addr, SOCKET_ADDRESS_AS_IPV4, ip, sizeof (ip));

	syslog(LOG_INFO, "start client %s [%s]", host, ip);

	for (running = 1; running; ) {
		if (!socket3_has_input(fd, socket_timeout)) {
			syslog(LOG_ERR, "timeout error: %s (%d)", strerror(errno), errno);
			return -1;
		}
		if ((length = socket3_read(fd, data, sizeof (data)-1, NULL)) < 0) {
			syslog(LOG_ERR, "read error: %s (%d)", strerror(errno), errno);
			return -1;
		}
		if (length == 0) {
			syslog(LOG_INFO, "end client %s [%s]", host, ip);
			break;
		}

		if (0 < debug) {
			data[length] = '\0';
			syslog(LOG_DEBUG, "%ld:%s", length, data);
		}

		if (socket3_write(fd, data, length, NULL) != length) {
			syslog(LOG_ERR, "write error: %s (%d)", strerror(errno), errno);
			return -1;
		}

		if (0 < TextInsensitiveStartsWith((char *)data, "STLS")
		||  0 < TextInsensitiveStartsWith((char *)data, "STARTTLS")) {
			if (socket3_is_tls(fd)) {
				syslog(LOG_WARNING, "TLS already started");
				continue;
			}

			syslog(LOG_INFO, "starting TLS...");

			if (socket3_start_tls(fd, 1, socket_timeout)) {
				syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
				return -1;
			}

			syslog(LOG_INFO, "TLS started");
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
	int argi, rc = EXIT_FAILURE;
	SocketAddress *address, from;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'c':
			ca_chain = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
			break;
		case 'C':
			cert_dir = argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2];
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
			socket3_set_debug(debug-1);
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
	if (socket3_init_tls()) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error0;
	}
	if (socket3_set_ca_certs(cert_dir, ca_chain)) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error1;
	}
	if (socket3_set_cert_key_chain(key_crt_pem, key_pass)) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error1;
	}

	if ((address = socketAddressCreate(echo_host, echo_port)) == NULL) {
		syslog(LOG_ERR, "failed to find host %s:%ld", echo_host, echo_port);
		goto error1;
	}

	if ((echo = socket3_server(address, 1, socket_timeout)) < 0) {
		syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
		goto error2;
	}

	syslog(LOG_INFO, "listening on host=%s port=%d", echo_host, socketAddressGetPort(address));

	(void) fileSetCloseOnExec(echo, 1);
	(void) socket3_set_linger(echo, 0);

	for (running = 1; running; ) {
		if ((client = socket3_accept(echo, &from)) < 0) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			continue;
		}

		(void) echo_server(client, &from);
		socket3_close(client);
	}

	rc = EXIT_SUCCESS;

	socket3_close(echo);
error2:
	free(address);
error1:
	socket3_fini();
error0:
	return rc;
}
