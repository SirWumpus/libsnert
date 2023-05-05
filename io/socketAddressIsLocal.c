/*
 * socketAddressIsLocal.c
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef HAVE_IFADDRS_H
# include <ifaddrs.h>
#endif

int
socketAddressIsLocal(SocketAddress *addr)
{
#ifdef HAVE_IFADDRS_H
	int rc, saved_port;
	struct ifaddrs *if_list, *if_entry;

	if_list = NULL;
	if (addr == NULL || getifaddrs(&if_list) != 0)
		return 0;

	saved_port = socketAddressGetPort(addr);
	socketAddressSetPort(addr, 0);
	rc = 0;

	for (if_entry = if_list; if_entry != NULL; if_entry = if_entry->ifa_next) {
		if (socketAddressEqual(addr, (SocketAddress *) if_entry->ifa_addr)) {
			rc = 1;
			break;
		}
	}

	socketAddressSetPort(addr, saved_port);
	freeifaddrs(if_list);

	return rc;
#else
	errno = ENOSYS;
	return 0;
#endif /* HAVE_IFADDRS_H */
}

#ifdef TEST
# ifdef HAVE_IFADDRS_H

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	SocketAddress *addr;
	char **argp, *is_local;

	for (argp = argv+1; *argp != NULL; argp++) {
		if ((addr = socketAddressNew(*argp, 0)) != NULL) {
			is_local = socketAddressIsLocal(addr) ? "local" : "not local";
			printf("%s is %s\n", *argp, is_local);
			free(addr);
		}
	}

	return EXIT_SUCCESS;
}

# else
#  error "This OS does not support getifaddrs() and freeifaddrs()."
# endif
#endif

