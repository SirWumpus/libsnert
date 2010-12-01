/*
 * tee.c
 *
 * Copyright 2010 by Anthony Howe.  All rights reserved.
 *
 * tee(1) like test tool using a thread for standard input and a
 * thread for standard output and each output file. Input passed
 * using shared buffer and message queues.
 *
 * This cumbersome design for tee(1) is intended to test message
 * queue API and "lockpick" mutex debugging functions.
 */

#ifndef BUFFER_SIZE
#define BUFFER_SIZE		4096
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_SYSLOG_H
# include <syslog.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/sys/sysexits.h>

#ifdef TEST
# include <com/snert/lib/sys/lockpick.h>
#endif


static int append_mode;

static const char usage[] =
"usage: tee [-aiv] [file ...]\n"
"\n"
"-a\t\tappend the output to files\n"
"-i\t\tignore SIGINT signal\n"
"\n"
"Standard input is copied to standard output, sending a copy of\n"
"the input to zero or more files. The output is not buffered.\n"
"\n"
"Copyright 2010 by Anthony Howe.  All rights reserved.\n"
;

static int eof;
static int exit_code;
static int output_count;
static ssize_t buffer_length;
static unsigned char buffer[BUFFER_SIZE];

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv_more;
static pthread_cond_t cv_ready;

static void *
output_file(void *data)
{
	int fd, flags;
	const char *file = data;

	flags = append_mode ? (O_WRONLY|O_APPEND) : (O_WRONLY|O_CREAT|O_TRUNC);

	if (file == NULL) {
		fd = 1;
		file = "(stdout)";
	} else if ((fd = open(file, flags, 0660)) < 0) {
		PTHREAD_MUTEX_LOCK(&mutex);
		exit_code = EX_IOERR;
		PTHREAD_MUTEX_UNLOCK(&mutex);

		fprintf(
			stderr, "tee: \"%s\" open error: %s (%d)\n",
			file, strerror(errno), errno
		);

		return NULL;
	}

#if defined(HAVE_PTHREAD_CLEANUP_PUSH)
	pthread_cleanup_push((void (*)(void *)) close, (void *) fd);
#endif
	PTHREAD_MUTEX_LOCK(&mutex);

	for (;;) {
		/* Bump the thread counter. */
		output_count++;
		pthread_cond_signal(&cv_more);

		/* Wait for more input or EOF. */
		do
			(void) pthread_cond_wait(&cv_ready, &mutex);
		while (buffer_length <= 0 && !eof);

		if (eof)
			break;

		if (write(fd, buffer, buffer_length) != buffer_length) {
			fprintf(
				stderr, "tee: \"%s\" write error: %s (%d)\n",
				file, strerror(errno), errno
			);
			exit_code = EX_IOERR;
		}
	}

	/* Notify that EOF has been seen. */
	output_count++;
	pthread_cond_signal(&cv_more);

	PTHREAD_MUTEX_UNLOCK(&mutex);

#if defined(HAVE_PTHREAD_CLEANUP_PUSH)
	pthread_cleanup_pop(1);
#else
	close(fd);
#endif
	return NULL;
}

int
main(int argc, char **argv)
{
	pthread_t thread;
	int i, argi, output_length;
	pthread_attr_t pthread_attr;

	exit_code = EXIT_SUCCESS;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-' || (argv[argi][1] == '-' && argv[argi][2] == '\0'))
			break;

		switch (argv[argi][1]) {
		case 'a':
			append_mode = 1;
			break;
		case 'i':
			signal(SIGINT, SIG_IGN);
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return EXIT_FAILURE;
		}
	}

	/* Initialise our control objects before starting any threads. */
	if (pthread_mutex_init(&mutex, NULL) != 0)
		return EX_OSERR;
	if (pthread_cond_init(&cv_more, NULL) != 0)
		return EX_OSERR;
	if (pthread_cond_init(&cv_ready, NULL) != 0)
		return EX_OSERR;
	if (pthread_attr_init(&pthread_attr) != 0)
		return EX_OSERR;
# if defined(HAVE_PTHREAD_ATTR_SETSTACKSIZE)
	(void) pthread_attr_setstacksize(&pthread_attr, 2*PTHREAD_STACK_MIN);
# endif

	/* Create a "consumer" output thread for each file and standard output. */
	output_count = 0;
	output_length = argc - argi + 1;
	for (i = argi; i <= argc; i++) {
		if (pthread_create(&thread, &pthread_attr, output_file, argv[i]) != 0) {
			fprintf(
				stderr, "tee: \"%s\" output thread error: %s (%d)\n",
				argv[i], strerror(errno), errno
			);

			PTHREAD_MUTEX_LOCK(&mutex);
			exit_code = EX_OSERR;
			PTHREAD_MUTEX_UNLOCK(&mutex);
			output_length--;
		}
		pthread_detach(thread);
	}

	/* Start the "producer" thread. */
	PTHREAD_MUTEX_LOCK(&mutex);

	while (!eof) {
		/* Wait for all output threads to request more input. */
		while (output_count < output_length)
			(void) pthread_cond_wait(&cv_more, &mutex);

		buffer_length = read(0, buffer, sizeof (buffer));
		eof = buffer_length <= 0;

		/* Reset the output counter. */
		output_count = 0;

		/* Notify all output threads that input or EOF is ready. */
		pthread_cond_broadcast(&cv_ready);
	}

	/* Wait for all output threads to terminate. */
	while (output_count < output_length)
		(void) pthread_cond_wait(&cv_more, &mutex);

	PTHREAD_MUTEX_UNLOCK(&mutex);

	/* Clean up. */
	(void) pthread_attr_destroy(&pthread_attr);
	(void) pthread_cond_destroy(&cv_ready);
	(void) pthread_cond_destroy(&cv_more);
	(void) pthread_mutex_destroy(&mutex);

	return exit_code;
}
