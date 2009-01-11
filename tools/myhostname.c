/*
 * gcc  -g -mno-cygwin myhostname.c -lws2_32
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __WIN32__
#include <windows.h>
#include <winsock2.h>
#endif

int
main(int argc, char **argv)
{
	char hostname[256];

#ifdef __WIN32__
	WORD version;
	WSADATA wsaData;

	version = MAKEWORD(2, 2);
	if (WSAStartup(version, &wsaData) != 0) {
		fprintf(stderr, "WSAStartup() failed: %s (%d)\n", strerror(errno), errno);
		return 71;
	}

	if (atexit((void (*)(void)) WSACleanup)) {
		fprintf(stderr, "atexit(WSACleanup) failed: %s (%d)\n", strerror(errno), errno);
		return 70;
	}

	if (HIBYTE( wsaData.wVersion ) < 2 || LOBYTE( wsaData.wVersion ) < 2) {
		fprintf(stderr, "WinSock API must be version 2.2 or better.");
		return 70;
	}

#endif
	if (gethostname(hostname, sizeof (hostname)))
		fprintf(stderr, "gethostname error: %s (%d)\n", strerror(errno), errno);

	printf("%s\n", hostname);

	return 0;
}
