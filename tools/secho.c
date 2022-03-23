/*
 * secho.c
 *
 * Secure Echo Test Client
 *
 * Copyright 2011, 2013 by Anthony Howe.  All rights reserved.
 */

#define _NAME			"secho"

#ifndef ECHO_HOST
#define ECHO_HOST		"127.0.0.1"
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
#if defined(__sun__) && !defined(_POSIX_PTHREAD_SEMANTICS)
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
#include <com/snert/lib/util/getopt.h>

#define SMTP_PORT		25
#define SMTPS_PORT		465
#define POP_PORT		110
#define POPS_PORT		995
#define IMAP_PORT		143
#define IMAPS_PORT		993
#define HTTPS_PORT		443

#ifndef CERT_DIR
#define CERT_DIR		NULL
#endif

#ifndef CA_CHAIN
#define CA_CHAIN		NULL
#endif

typedef int (*is_eol_fn)(const unsigned char *, long);

static int debug;
static int running;
static int read_first;
static is_eol_fn echo_eol;
static long echo_port = ECHO_PORT;
static char *echo_host = ECHO_HOST;
static long socket_timeout = SOCKET_TIMEOUT;

static char *ca_chain = CA_CHAIN;
static char *cert_dir = CERT_DIR;
static char *key_crt_pem = NULL;
static char *key_pass = NULL;

static char line[INPUT_LINE_SIZE+1];
static unsigned char data[INPUT_LINE_SIZE * 10];

static const char log_io[] = "socket error %s(%d): %s (%d)";
static const char log_internal[] = "%s(%d): %s (%d)";
#define __F_L__			   __FILE__, __LINE__

static char options[] = "rvc:C:d:k:K:h:p:t:";
static char usage[] =
"usage: " _NAME " [-vr][-c ca_pem][-C ca_dir][-d dh_pem][-k key_crt_pem]\n"
"             [-K key_pass][-h host][-p port][-t seconds]\n"
"\n"
"-c ca_pem\tCertificate Authority root certificate chain file\n"
"-C ca_dir\t\tCertificate Authority root certificate directory\n"
"-h host\thost and optional port to contact; default " ECHO_HOST "\n"
"-k key_crt_pem\tprivate key and certificate chain file\n"
"-K key_pass\tpassword for private key; default no password\n"
"-p port\tport to connect to; default " QUOTE(ECHO_PORT) "\n"
"-r\t\tread from server first\n"
"-t seconds\tsocket timeout in seconds; default " QUOTE(SOCKET_TIMEOUT) "\n"
"-v\t\tverbose debug messages to standard error\n"
"\n"
"Understands SMTP (25), POP (110), and IMAP (143) usage of STARTTLS and STLS.\n"
"Also understands SSL connections for HTTPS (443), SMTPS (465), POPS (995),\n"
"and IMAPS (993)\n"
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
echo_is_eol(const unsigned char *buffer, long length)
{
	/* Empty buffer? */
	if (length < 1)
		return 0;

	/* Buffer end with an incomplete line. */
	return buffer[length-1] == '\n';
}

int
echo_pop_eol(const unsigned char *buffer, long length)
{
	int span;

	if (!echo_is_eol(buffer, length))
		return 0;

	/* Find start of last line. */
	span = strlrspn((char *)buffer, length, "\r\n");
	span = strlrcspn((char *)buffer, span, "\r\n");

	/* Buffer ends with a dot line? */
	if (buffer[span] == '.' && iscntrl(buffer[span+1]))
		return 1;

	return span == 0;
}

int
echo_imap_eol(const unsigned char *buffer, long length)
{
	int span;

	if (!echo_is_eol(buffer, length))
		return 0;

	/* Find start of last line. */
	span = strlrspn((char *)buffer, length, "\r\n");
	span = strlrcspn((char *)buffer, span, "\r\n");

	if (TextMatch((char *)buffer+span, "*OK *", length-span, 0))
		return 1;
	if (TextMatch((char *)buffer+span, "*NO *", length-span, 0))
		return 1;
	if (TextMatch((char *)buffer+span, "*BAD *", length-span, 0))
		return 1;

	return 0;
}

int
echo_smtp_eol(const unsigned char *buffer, long length)
{
	int span;

	if (!echo_is_eol(buffer, length))
		return 0;

	/* Find start of last line. */
	span = strlrspn((char *)buffer, length, "\r\n");
	span = strlrcspn((char *)buffer, span, "\r\n");

	/* Last line starts with SMTP reply code and space. */
	return isdigit(buffer[span]) && buffer[span+3] == ' ';
}

long
echo_read(SOCKET fd)
{
	long length, offset;

	length = 0;
	do {
		if (!socket3_has_input(fd, socket_timeout)) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			return -1;
		}
		errno = 0;
		if ((offset = socket3_read(fd, data, sizeof (data), NULL)) < 0) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			return -1;
		}
		if (0 < offset) {
			(void) fwrite(data, 1, offset, stdout);
			(void) fflush(stdout);
		}
		length += offset;
	} while (0 < offset && !(*echo_eol)(data, offset));

	return length;
}

int
echo_client(SOCKET fd)
{
	long length;
	int wait_for_dot = 0;

	*line = '\0';

	switch (echo_port) {
	case POP_PORT:
		echo_eol = echo_pop_eol;
		break;
	case IMAP_PORT:
		echo_eol = echo_imap_eol;
		break;
	case SMTP_PORT:
		echo_eol = echo_smtp_eol;
		break;
	default:
		echo_eol = echo_is_eol;
		break;
	}

	switch (echo_port) {
	case POPS_PORT:
	case IMAPS_PORT:
	case SMTPS_PORT:
	case HTTPS_PORT:
		syslog(LOG_INFO, "starting TLS...");
		if (socket3_start_tls(fd, SOCKET3_CLIENT_TLS, socket_timeout)) {
			syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
			return -1;
		}
		syslog(LOG_INFO, "TLS started");
		if (echo_port == HTTPS_PORT)
			break;
		/*@fallthrough@*/

	case POP_PORT:
	case IMAP_PORT:
	case SMTP_PORT:
		read_first = 1;
	}

	if (read_first) {
		running = 1;
		goto welcome_banner;
	}

	for (running = 1; running; ) {
		do {
			if (fgets(line, sizeof (line), stdin) == NULL)
				break;
			length = (long) strlen(line);

			if (line[0] == '.' && (line[1] == '\n' || line[1] == '\r'))
				wait_for_dot = 0;

			if (socket3_write(fd, (unsigned char *)line, length, NULL) != length) {
				syslog(LOG_ERR, "write error: %s (%d)", strerror(errno), errno);
				return -1;
			}
		} while (wait_for_dot);
		if (TextMatch(line, "*STARTTLS*", -1, 1) || 0 < TextInsensitiveStartsWith(line, "STLS")) {
			if (socket3_is_tls(fd)) {
				syslog(LOG_WARNING, "TLS already started");
				continue;
			}

			syslog(LOG_INFO, "starting TLS...");
			if (socket3_start_tls(fd, SOCKET3_CLIENT_TLS, socket_timeout)) {
				syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
				return -1;
			}
			syslog(LOG_INFO, "TLS started");
		}
welcome_banner:
		if (echo_read(fd) <= 0) {
			syslog(LOG_ERR, "read error: %s (%d)", strerror(errno), errno);
			break;
		}

		if (0 < TextInsensitiveStartsWith(line, "QUIT") || TextMatch(line, "*LOGOUT*", -1, 1))
			break;
		else if (0 < TextInsensitiveStartsWith(line, "DATA"))
			wait_for_dot = 1;
	}

	return 0;
}

int
main(argc, argv)
int argc;
char **argv;
{
	SOCKET echo;
	int ch, rc = EXIT_FAILURE;

	while ((ch = getopt(argc, argv, options)) != -1) {
		switch (ch) {
		case 'c':
			ca_chain = optarg;
			break;
		case 'C':
			cert_dir = optarg;
			break;
		case 'k':
			key_crt_pem = optarg;
			break;
		case 'K':
			key_pass = optarg;
			break;
		case 'h':
			echo_host = optarg;
			break;
		case 'p':
			echo_port = strtol(optarg, NULL, 10);
			break;
		case'r':
			read_first = 1;
			break;
		case 't':
			socket_timeout = strtol(optarg, NULL, 10);
			break;
		case 'v':
			debug++;
			break;
		default:
			fprintf(stderr, usage);
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
	if (key_crt_pem != NULL && socket3_set_cert_key_chain(key_crt_pem, key_pass)) {
		syslog(LOG_ERR, log_internal, __F_L__, strerror(errno), errno);
		goto error1;
	}

	syslog(LOG_INFO, "connecting to host=%s port=%ld", echo_host, echo_port);

	if ((echo = socket3_connect(echo_host, echo_port, socket_timeout)) < 0) {
		syslog(LOG_ERR, log_io, __F_L__, strerror(errno), errno);
		goto error1;
	}

	(void) fileSetCloseOnExec(echo, 1);
	(void) socket3_set_linger(echo, 0);
	(void) socket3_set_nonblocking(echo, 1);

	if (echo_client(echo))
		goto error2;

	rc = EXIT_SUCCESS;
error2:
	socket3_close(echo);
error1:
	socket3_fini();
error0:
	return rc;
}
