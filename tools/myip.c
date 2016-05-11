/*
 * myip.c 
 *
 * Simple inetd service reports IPv4 or IPv6 address and port number.
 * If the server port is 80 or any port between 8000..8999, then treat
 * it as an HTTP request.
 *
 * gcc -O -o myip myip.c
 */

#ifdef __NetBSD__
#define HAVE_NETINET6_IN6_H
#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_IN6_H
# include <netinet/in6.h>
#endif
#ifdef HAVE_NETINET6_IN6_H
# include <netinet6/in6.h>
#endif

union socket_address {
	struct sockaddr sa;
	struct sockaddr_in in;
#ifdef AF_INET6
	struct sockaddr_in6 in6;
#endif
};

int
get_socket_info(union socket_address *addr, char *buf, size_t size, int *port)
{
	switch (addr->sa.sa_family) {
	case AF_INET:
		*port = (unsigned) ntohs(addr->in.sin_port);
		if (inet_ntop(AF_INET, &addr->in.sin_addr, buf, size) == NULL) {
			warn("inet_ntop");
			return -1;
		}
		break;
		
#ifdef AF_INET6
	case AF_INET6:
		*port = (unsigned) ntohs(addr->in6.sin6_port);
		if (inet_ntop(AF_INET6, &addr->in6.sin6_addr, buf, size) == NULL) {
			warn("inet_ntop");
			return -1;
		}
		break;
#endif
	default:
		warnx("unknown socket family");
		return -1;
	}
	
	return 0;
}	

int
main(int argc, char **argv)
{
	unsigned port;
	socklen_t len;
	union socket_address addr;
	char ip[INET6_ADDRSTRLEN];
	int ch, is_http, show_port;
	struct linger setlinger = { 0, 0 };

	show_port = 0;
	while ((ch = getopt(argc, argv, "p")) != -1) {
		switch (ch) {
		case 'p':
			show_port = 1;
			break;
		default:
			(void) fprintf(stderr, "usage: myip [-p]\n");
			return EXIT_FAILURE;
		}
	}

	len = sizeof (addr);
	
	if (getsockname(STDIN_FILENO, (struct sockaddr *)&addr, &len) != 0) 
		err(EXIT_FAILURE, "getsockname");
	
	if (get_socket_info(&addr, ip, sizeof (ip), &port) != 0)
		return EXIT_FAILURE;

	/* HTTP port 80 or any 8000 range port, which covers 8008 and 8080,
	 * making it easy to find a convenient number if the usual ones are
	 * occupied.
	 */
	is_http = port == 80 || (8000 <= port && port < 9000);
			
	if (getpeername(STDIN_FILENO, (struct sockaddr *)&addr, &len) != 0) 
		err(EXIT_FAILURE, "getpeername");

	if (setsockopt(STDIN_FILENO, SOL_SOCKET, SO_LINGER, (char *) &setlinger, sizeof (setlinger)) != 0)
		err(EXIT_FAILURE, "setsockopt SO_LINGER");

	if (get_socket_info(&addr, ip, sizeof (ip), &port) != 0)
		return EXIT_FAILURE;

	if (is_http) {
		int outlen;
		if (show_port)
			outlen = snprintf(NULL, 0, "%s %u\r\n", ip, port);
		else
			outlen = snprintf(NULL, 0, "%s\r\n", ip);		
		(void) printf("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n", outlen);
	}
	if (show_port)
		(void) printf("%s %u\r\n", ip, port);
	else
		(void) printf("%s\r\n", ip);
	
	return EXIT_SUCCESS;
}
