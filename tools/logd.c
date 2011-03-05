/*
 * logd.c
 *
 * Copyright 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/getopt.h>

static const char usage[] = "usage: logd [-dqvw] [address[:port]]\n";

typedef struct {
	Queue unused;
	Queue message;
	Socket2 *server;
} LogServer;

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/sys/Time.h>
#include <com/snert/lib/sys/process.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>

#define _NAME			"server"
#define PID_FILE		"/var/run/" _NAME ".pid"
#define SYSLOG_PORT		514

int debug;
int server_quit;
int daemon_mode = 1;
char *windows_service;
char *interface_address = "127.0.0.1:" QUOTE(SYSLOG_PORT);
ServerSignals signals;

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

oid
serverOptions(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "dqvw:")) != -1) {
		switch (ch) {
		case 'd':
			daemon_mode = 0;
			break;

		case 'q':
			server_quit++;
			break;

		case 'v':
			debug++;
			break;

		case 'w':
			if (strcmp(optarg, "add") == 0 || strcmp(optarg, "remove") == 0) {
				windows_service = optarg;
				break;
			}
			/*@fallthrough@*/

		default:
			fprintf(stderr, usage);
			exit(EX_USAGE);
		}
	}
}

int
serverMain(void)
{
	int rc, signal;
	Server *echo, *daytime;

	rc = EXIT_FAILURE;

	if ((echo = serverCreate("[::0]:" QUOTE(ECHO_PORT) "; 0.0.0.0:" QUOTE(ECHO_PORT), ECHO_PORT)) == NULL)
		goto error0;

	echo->debug.level = debug;
	echo->hook.session_accept = reportAccept;
	echo->hook.session_process = echoProcess;
	serverSetStackSize(echo, SERVER_STACK_SIZE);

	if ((daytime = serverCreate("[::0]:" QUOTE(DAYTIME_PORT) "; 0.0.0.0:" QUOTE(DAYTIME_PORT), DAYTIME_PORT)) == NULL)
		goto error1;

	daytime->debug.level = debug;
	daytime->hook.session_accept = reportAccept;
	daytime->hook.session_process = daytimeProcess;
	serverSetStackSize(daytime, SERVER_STACK_SIZE);

	if (serverSignalsInit(&signals, _NAME))
		goto error2;

#if defined(__OpenBSD__) || defined(__FreeBSD__)
	(void) processDumpCore(2);
#endif
	if (processDropPrivilages("nobody", "nobody", "/tmp", 0))
		goto error3;
#if defined(__linux__)
	(void) processDumpCore(1);
#endif
	if (serverStart(echo) || serverStart(daytime))
		goto error3;

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);

	syslog(LOG_INFO, "signal %d, stopping sessions, cn=%u", signal, echo->connections + daytime->connections);
	serverStop(daytime, signal == SIGQUIT);
	serverStop(echo, signal == SIGQUIT);
	syslog(LOG_INFO, "signal %d, terminating process", signal);

	rc = EXIT_SUCCESS;
error3:
	serverSignalsFini(&signals);
error2:
	serverFree(daytime);
error1:
	serverFree(echo);
error0:
	return rc;
}

# ifdef __unix__
/***********************************************************************
 *** Unix Daemon
 ***********************************************************************/

# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif

#include <com/snert/lib/sys/pid.h>

void
atExitCleanUp(void)
{
	(void) unlink(PID_FILE);
	closelog();
}

int
main(int argc, char **argv)
{
	serverOptions(argc, argv);
	LogSetProgramName(_NAME);

	switch (server_quit) {
	case 1:
		/* Slow quit	-q */
		exit(pidKill(PID_FILE, SIGQUIT) != 0);

	case 2:
		/* Quit now	-q -q */
		exit(pidKill(PID_FILE, SIGTERM) != 0);

	default:
		/* Restart	-q -q -q
		 * Restart-If	-q -q -q -q
		 */
		if (pidKill(PID_FILE, SIGTERM) && 3 < server_quit) {
			fprintf(stderr, "no previous instance running: %s (%d)\n", strerror(errno), errno);
			return EXIT_FAILURE;
		}

		sleep(2);
	}

	if (daemon_mode) {
		pid_t ppid;
		int pid_fd;

		openlog(_NAME, LOG_PID|LOG_NDELAY, LOG_USER);

		if ((ppid = fork()) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_OSERR;
		}

		if (ppid != 0)
			return EXIT_SUCCESS;

		if (setsid() == -1) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_OSERR;
		}

		if (atexit(atExitCleanUp)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if (pidSave(PID_FILE)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if ((pid_fd = pidLock(PID_FILE)) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}
	} else {
		LogOpen("(standard error)");
	}

	return serverMain();
}
# endif /* __unix__ */

