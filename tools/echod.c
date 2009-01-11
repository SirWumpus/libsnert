/*
 * echod.c
 *
 * Echo Protocol (RFC 862, STD 20)
 *
 * Copyright 2001, 2008 by Anthony Howe.  All rights reserved.
 */

#ifdef __WIN32__
# define WINVER	0x0501
#endif

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/getopt.h>

typedef int (*echo_server_fn)(const char *);
static const char usage[] = "usage: echod address[:port]\n";

#ifdef __WIN32__
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>

static DWORD strerror_tls = TLS_OUT_OF_INDEXES;
static const char unknown_error[] = "(unknown error)";

char *
strerror(int error_code)
{
	char *error_string;

	if (strerror_tls == TLS_OUT_OF_INDEXES) {
		strerror_tls = TlsAlloc();
		if (strerror_tls == TLS_OUT_OF_INDEXES)
			return (char *) unknown_error;
	}

	error_string = (char *) TlsGetValue(strerror_tls);
	LocalFree(error_string);

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &error_string, 0, NULL
	);

	if (!TlsSetValue(strerror_tls, error_string)) {
		LocalFree(error_string);
		return (char *) unknown_error;
	}

	return error_string;
}

#endif /* __WIN32__ */

#ifdef GETADDRINFO_LOOP
void *
workerThread(void *data)
{
	SOCKET client;
	unsigned char ch;

	client = *(SOCKET *) data;

	while (recv(client, &ch, 1, 0) == 1) {
		if (send(client, &ch, 1, 0) != 1)
			break;
	}

	closesocket(client);

	return NULL;
}

int
echoServer(const char *host)
{
	int rc, running;
	pthread_t thread;
	socklen_t socklen;
      	SOCKET server, client;
      	struct addrinfo *ai, *result;

      	if (getaddrinfo(host, "echo", NULL, &result)) {
		printf("getaddrinfo error: %s (%d)\n", strerror(errno), errno);
		return 1;
      	}

	server = INVALID_SOCKET;

	for (ai = result; ai != NULL; ai = ai->ai_next) {
		if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
			continue;

		if ((server = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == INVALID_SOCKET) {
			printf("socket error: %s (%d)\n", strerror(errno), errno);
			continue;
		}

		if (bind(server, ai->ai_addr, ai->ai_addrlen) == SOCKET_ERROR) {
			printf("bind error: %s (%d)\n", strerror(errno), errno);
			closesocket(server);
			continue;
		}

		if (listen(server, 10)) {
			printf("listen error: %s (%d)\n", strerror(errno), errno);
			closesocket(server);
			continue;
		}

		rc = 0;

		for (running = 1; running; ) {
			socklen = ai->ai_addrlen;
			if ((client = accept(server, ai->ai_addr, &socklen)) == INVALID_SOCKET) {
				printf("accept error: %s (%d)\n", strerror(errno), errno);
				break;
			}

			if (pthread_create(&thread, NULL, workerThread, &client)) {
				printf("pthread_create error: %s (%d)\n", strerror(errno), errno);
				break;
			}

			pthread_detach(thread);
		}

		closesocket(server);
		break;
	}

	freeaddrinfo(result);

	return rc;
}
#endif

void *
workerThread(void *data)
{
	unsigned char buffer[256];
	long line_length, bytes_sent;
	Socket2 *client = (Socket2 *) data;

	while (0 < (line_length = socketReadLine2(client, buffer, sizeof (buffer), 1))) {
		bytes_sent = socketWrite(client, buffer, line_length);
		if (line_length != bytes_sent)
			break;
	}

	socketClose(client);

	return NULL;
}

int
echoServer(const char *host)
{
	int running;
	pthread_t thread;
	SocketAddress *address;
	Socket2 *server, *client;

	if ((address = socketAddressCreate(host, 7)) == NULL) {
		printf("socketAddressCreate error: %s (%d)\n", strerror(errno), errno);
		return 1;
	}

	if ((server = socketOpen(address, 1)) == NULL) {
		printf("socketOpen error: %s (%d)\n", strerror(errno), errno);
		return 1;
	}

	if (socketServer(server, 10)) {
		printf("socketServer error: %s (%d)\n", strerror(errno), errno);
		return 1;
	}

	for (running = 1; running; ) {
		if ((client = socketAccept(server)) == NULL) {
			printf("socketAccept error: %s (%d)\n", strerror(errno), errno);
			break;
		}

		if (pthread_create(&thread, NULL, workerThread, client)) {
			printf("pthread_create error: %s (%d)\n", strerror(errno), errno);
			break;
		}

		pthread_detach(thread);
	}

	socketClose(server);
	free(address);

	return 0;
}

int
main(int argc, char **argv)
{
	int ch;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			fprintf(stderr, usage);
			return 2;
		}
	}

	if (argc < optind + 1) {
		fprintf(stderr, usage);
		return 2;
	}

	if (socketInit())
		return 1;

	return echoServer(argv[optind]);
}

