/**
 * Socket.h
 *
 * Copyright 2001, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_io_Socket_h__
#define __com_snert_lib_io_Socket_h__	1

#ifndef AtExitFunctionType
#define AtExitFunctionType	1
typedef void (*AtExitFunction)(void);
#endif

#include <sys/types.h>

/*
 * Provide IPPROTO_* definitions.
 */
#if defined(__WIN32__)

# if defined(__VISUALC__)
#  define _WINSOCK2API_
# endif

# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>

# define ETIMEDOUT	WSAETIMEDOUT

# define SHUT_RD	SD_RECEIVE
# define SHUT_WR	SD_SEND
# define SHUT_RDWR	SD_BOTH

/*
# ifndef socklen_t
#  define socklen_t int
# endif
*/
#else /* not __WIN32__ */

# include <sys/socket.h>

# ifdef HAVE_SYS_UN_H
#  include <sys/un.h>
# endif
# ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
# endif
# ifdef HAVE_NETINET_IN6_H
#  include <netinet/in6.h>
# endif
# ifdef USE_IP_TOS
#  include <netinet/ip.h>
# endif
# ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
# endif

# ifndef SHUT_RDWR
#  define SHUT_RD	0
#  define SHUT_WR	1
#  define SHUT_RDWR	2
# endif

#endif /* __WIN32__ */

typedef void *Socket;

typedef struct inet_address {
	int domain;
	socklen_t length;
	union {
		struct sockaddr_in in;
#if defined(__WIN32__)
		unsigned long longValue;
#elif defined(HAVE_STRUCT_SOCKADDR_UN)
		struct sockaddr_un un;
#endif
	} address;

	/* Methods */
	void (*destroy)(struct inet_address *);
	char *(*getAddress)(struct inet_address *);
	int (*getPort)(struct inet_address *);
} InetAddress;

#ifdef __cplusplus
extern "C" {
#endif

extern int SocketInit(void);
extern Socket SocketOpen(InetAddress *addr, int type);
extern Socket SocketOpenUdp(const char *host, int port);
extern Socket SocketOpenTcpClient(const char *host, int port);
extern Socket SocketOpenTcpClientWait(const char *host, int port, long ms);
extern Socket SocketOpenTcpServer(const char *host, int port, int nqueue);
extern Socket SocketFromFd(int);
extern Socket SocketAccept(Socket);
extern void SocketClose(Socket);

extern char *SocketGetAddress(Socket);
extern char *SocketGetSourceAddress(Socket);

extern int SocketGetFd(Socket);
extern int SocketGetPort(Socket);
extern int SocketGetError(Socket);
extern int SocketSetBroadcast(Socket, int);
extern int SocketSetKeepAlive(Socket, int);
extern int SocketSetLinger(Socket, int);
extern int SocketSetReuseAddr(Socket, int);
extern int SocketSetTcpNoDelay(Socket, int);
extern void SocketClearError(Socket);

/* These two functions are not strictly portable and may
 * go away at some point in the future.
 */
extern void SocketSetReadFlags(Socket sock, int flags);
extern void SocketSetWriteFlags(Socket sock, int flags);

/* How to shutdown the socket on close. */
extern void SocketSetShutdown(Socket sock, int shutdown);

/* Shutdown the socket now. Used with some protocols (spamc) to signal EOF. */
extern void SocketShutdownNow(Socket sock, int shutdown);

extern int SocketSetNonBlocking(Socket sock, int nonblocking);

extern int SocketWaitForInput(Socket sock, long ms);
extern int SocketWaitForOutput(Socket sock, long ms);
extern long SocketRead(Socket, void *buffer, long length);
extern long SocketReadLine(Socket sock, char *line, long length);

extern int SocketPrint(Socket sock, char *line);
extern int SocketPrintLine(Socket sock, char *line);
extern long SocketWriteIp(Socket, void *buffer, long length, InetAddress *addr);
extern long SocketWrite(Socket, void *buffer, long length);

extern int SocketAddressToIP(const char *address, struct in_addr *ip);

#define SOCKET_DEBUG_ALL		(~0)
#define SOCKET_DEBUG_FD			1
#define SOCKET_DEBUG_OPEN_CLOSE		2
#define SOCKET_DEBUG_READ_WRITE		4
#define SOCKET_DEBUG_GET_SET		8

extern void SocketSetDebugMask(int flag);

#ifndef HAVE_INET_NTOP
# define NOT_THREAD_SAFE_INET_NTOP	1
extern const char *inet_ntop(int af, const void *in_addr, char *buffer, socklen_t size);
#endif

/*
 * Front-end for inet_ntop() or inet_ntoa().
 */
extern int ConvertIpToString(const void *ip, char *buffer, long size);

/*
 * Create an internet address structure, which can be a:
 *
 *  a)	hostname and port
 *  b)	ip address and port
 *  c)	unix domain socket path
 *
 * Return a structure pointer or null on error.
 */
extern InetAddress *InetAddressCreate(const char *host, int port);

/*
 * Initialise an internet address structure, which can be a:
 *
 *  a)	ip address and port
 *
 * Return 0 for success, otherwise -1 on error.
 */
extern int InetAddressInitIP(InetAddress *addr, const char *host, int port);

/*
 * Initialise an internet address structure, which can be a:
 *
 *  a)	hostname and port
 *  b)	ip address and port
 *  c)	unix domain socket path
 *
 * Return 0 for success, otherwise -1 on error.
 */
extern int InetAddressInitHost(InetAddress *addr, const char *host, int port);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_Socket_h__ */
