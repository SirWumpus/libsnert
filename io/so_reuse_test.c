/*
 * From http://rextester.com/BUAFK86204
 *
 * See https://stackoverflow.com/questions/14388706/how-do-so-reuseaddr-and-so-reuseport-differ/14388707
 */

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#if defined(__APPLE__) || defined(__bsd__)
#	define HAVE_SA_LEN
#endif

static const uint16_t kTestPort = 23999;

typedef enum {
	SocModeNone = 0,
	SocModeListen,
	SocModeMulticast
} SocketMode;

typedef enum {
	SocReNone = 0,
	SocReAddr1,
	SocReAddr2,
	SocReAddrBoth,
#ifndef SO_REUSEPORT
	SocReLast,
#endif
	SocRePort1,
	SocRePort2,
	SocRePortBoth
#ifdef SO_REUSEPORT
	,
	SocReLast
#endif
} SocketReuse;

static int socketA = -1;
static int socketB = -1;

static
void cleanUp ( ) {
	if (socketA >= 0) close(socketA);
	if (socketB >= 0) close(socketB);
	socketA = -1;
	socketB = -1;
}

static
bool setupUDP ( ) {
	socketA = socket(PF_INET, SOCK_DGRAM, 0);
	socketB = socket(PF_INET, SOCK_DGRAM, 0);
	if (socketA < 0 || socketB < 0) cleanUp();
	return (socketA >= 0);
}

static
bool setupTCP ( ) {
	socketA = socket(PF_INET, SOCK_STREAM, 0);
	socketB = socket(PF_INET, SOCK_STREAM, 0);
	if (socketA < 0 || socketB < 0) cleanUp();
	return (socketA >= 0);
}

static
bool enableSockOpt ( int socket, int option, bool enable ) {
	int yes = (enable ? 1 : 0);
	int err = setsockopt(socket, SOL_SOCKET, option,  &yes, sizeof(yes));
	return !err;
}

static
bool enableReuseAddr ( int socket, bool enable ) {
	return enableSockOpt(socket, SO_REUSEADDR, enable);
}

static
bool enableReusePort ( int socket, bool enable ) {
#ifdef SO_REUSEPORT
	return enableSockOpt(socket, SO_REUSEPORT, enable);
#else
	return true;
#endif
}

static
struct sockaddr_in makeSockaddr (
	const char * localAddr, uint16_t localPort
) {
	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
	addr.sin_len = sizeof(addr);
#endif
	addr.sin_port = htons((unsigned short)localPort);
	int ok = inet_pton(AF_INET, localAddr, &addr.sin_addr);
	if (!ok) addr.sin_family = AF_UNSPEC;
	return addr;
}

static
bool bindSocket ( int socket, const char * localAddr, uint16_t localPort ) {
	struct sockaddr_in addr = makeSockaddr(localAddr, localPort);
	if (addr.sin_family == AF_UNSPEC) return false;

	int err = bind(socket, (struct sockaddr *)&addr, sizeof(addr));
	return !err;
}

static
bool makeListenSocket ( int socket ) {
	int err = listen(socket, 1);
	return !err;
}


static
bool test (
	SocketMode mode, bool useTCP, SocketReuse reuse,
	const char * localAddress1, const char * localAddress2
) {
	if (useTCP) {
		// TCP cannot be multicast!
		if (mode == SocModeMulticast) { errno = EINVAL; return false; }
		if (!setupTCP()) return false;
	} else {
		// UDP cannot be listen!
		if (mode == SocModeListen) { errno = EINVAL; return false; }
		if (!setupUDP()) return false;
	}

	bool reuseAddr1 = (reuse == SocReAddr1 || reuse == SocReAddrBoth);
	bool reuseAddr2 = (reuse == SocReAddr2 || reuse == SocReAddrBoth);
	bool reusePort1 = (reuse == SocRePort1 || reuse == SocRePortBoth);
	bool reusePort2 = (reuse == SocRePort2 || reuse == SocRePortBoth);

	if (!enableReuseAddr(socketA, reuseAddr1)
		|| !enableReuseAddr(socketB, reuseAddr2)
		|| !enableReusePort(socketA, reusePort1)
		|| !enableReusePort(socketB, reusePort2)
	) {
		cleanUp();
		return false;
	}

	if (!bindSocket(socketA, localAddress1, kTestPort)) {
		cleanUp();
		return false;
	}

	if (mode == SocModeListen) {
		if (!makeListenSocket(socketA)) { cleanUp(); return false; }
	}

	char * modeName = NULL;
	switch (mode) {
		case SocModeNone:      modeName = "(none)   "; break;
		case SocModeListen:    modeName = "Listen   "; break;
		case SocModeMulticast: modeName = "Multicast"; break;
		default: cleanUp(); errno = EINVAL; return false;
	}

	char * reuseName = NULL;
	switch (reuse) {
		case SocReNone:     reuseName = "(none)   "; break;
		case SocReAddr1:    reuseName = "Addr(1)  "; break;
		case SocReAddr2:    reuseName = "Addr(2)  "; break;
		case SocReAddrBoth: reuseName = "Addr(1&2)"; break;
		case SocRePort1:    reuseName = "Port(1)  "; break;
		case SocRePort2:    reuseName = "Port(2)  "; break;
		case SocRePortBoth: reuseName = "Port(1&2)"; break;
		case SocReLast: cleanUp(); errno = EINVAL; return false;
	}

	// INET_ADDRSTRLEN includes terminating \0 in count!
	int padding1 = (int)(INET_ADDRSTRLEN - strlen(localAddress1) - 1);
	int padding2 = (int)(INET_ADDRSTRLEN - strlen(localAddress2) - 1);

	int err = bindSocket(socketB, localAddress2, kTestPort);
	int errNo = (err ? 0 : errno);

	printf(
		"%s  "
		"%s    "
		"%s  "
		"%s%.*s  "
		"%s%.*s  "
		"->  %s%s%s%s\n",
		modeName,
		useTCP  ? "TCP" : "UDP",
		reuseName,
		localAddress1, padding1, "                ",
		localAddress2, padding2, "                ",
		errNo == 0 ? "OK" : "Error!",
		errNo == 0 ? "" : " (",
		errNo == 0 ? "" : strerror(errno),
		errNo == 0 ? "" : ")"
	);
	cleanUp();
	return true;
}

static
void testAndFailOnCriticalError (
	SocketMode mode, bool useTCP, SocketReuse reuse,
	const char * localAddress1, const char * localAddress2
) {
	bool ok = test(
		mode, useTCP, reuse,
		localAddress1, localAddress2
	);
	if (!ok) {
		fprintf(stderr, "Critical error setting up test! (%s)\n",
			strerror(errno)
		);
		exit(EXIT_FAILURE);
	}
}


static
char * copyPrimaryAddress ( ) {
	int so = socket(PF_INET, SOCK_DGRAM, 0);
	if (so < 0) return NULL;

	struct sockaddr_in addr = makeSockaddr("8.8.8.8", 443);
	if (addr.sin_family == AF_UNSPEC) { close(so); return false; }

	int err = connect(so, (struct sockaddr *)&addr, sizeof(addr));
	if (err) { close(so); return NULL; }

	socklen_t len = sizeof(addr);
	err = getsockname(so, (struct sockaddr *)&addr, &len);
	close(so);
	if (err) return NULL;

	// INET_ADDRSTRLEN includes terminating \0 in count!
	char buffer[INET_ADDRSTRLEN] = { 0 };
	const char * res = inet_ntop(
		AF_INET, &addr.sin_addr, buffer, sizeof(buffer)
	);
	if (!res) return NULL;

	// Apparently `strdup()` is not as potrable as one might expect
	size_t resultLength = strlen(res) + 1;
	char * result = malloc(resultLength);
	if (!result) return NULL;

	memcpy(result, res, resultLength);
	return result;
}


int main (
	int argc, const char * argv[]
) {
	const char *const localAddress = "127.0.0.1";
	const char *const wildcardAddress = "0.0.0.0";
	const char *const multicastAddress = "224.1.2.3";

#ifndef SO_REUSEPORT
	printf("WARNING: SO_REUESPORT is not available! "
		"Tests requiring it will just be skipped.\n"
	);
#endif

	printf("Test port is %"PRIu16"...\n", kTestPort);

	char * primaryAddress = copyPrimaryAddress();
	if (!primaryAddress) {
		fprintf(stderr, "Cannot obtain primary interface address!\n");
		return EXIT_FAILURE;
	}
	printf("Primary address: %s...\n", primaryAddress);

	if (strcmp(primaryAddress, localAddress) == 0) {
		fprintf(stderr, "Local address must not be primary address!");
		return EXIT_FAILURE;
	}

	const char *const sourceAddresses[] = {
		wildcardAddress, localAddress, primaryAddress
	};
	size_t addressCount = sizeof(sourceAddresses) / sizeof(sourceAddresses[0]);

	printf(
		"MODE       PROTO  REUSE      "
		"ADDRESS1         ADDRESS2         -> RESULT\n"
	);

	// Test every combinations but multicast
	SocketMode mode;
	for (mode = SocModeNone; mode < SocModeMulticast; mode++) {

		for (int proto = 0; proto < 2; proto++) {
			bool useTCP = (proto == 0);
			// UDP cannot be listen
			if (!useTCP && mode == SocModeListen) continue;

			SocketReuse reuse;
			for (reuse = SocReNone; reuse < SocReLast; reuse++) {

				size_t addr1;
				for (addr1 = 0; addr1 < addressCount; addr1++) {

					size_t addr2;
					for (addr2 = 0; addr2 < addressCount; addr2++) {

						// UDP cannot be listen!
						if (!useTCP && mode == SocModeListen) continue;

						testAndFailOnCriticalError(
							mode, useTCP, reuse,
							sourceAddresses[addr1], sourceAddresses[addr2]
						);
					}
				}
			}
		}
	}

	// Test all multicast combinations
	SocketReuse reuse;
	for (reuse = SocReNone; reuse < SocReLast; reuse++) {

		size_t addr1;
		for (addr1 = 0; addr1 < addressCount; addr1++) {

			size_t addr2;
			for (addr2 = 0; addr2 < addressCount; addr2++) {

				testAndFailOnCriticalError(
					SocModeMulticast, false, reuse,
					multicastAddress, multicastAddress
				);
			}
		}
	}

	return EXIT_SUCCESS;
}
