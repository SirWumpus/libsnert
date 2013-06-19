/*
 * isRFC2606.c
 *
 * Copyright 2002, 2013 by Anthony Howe. All rights reserved.
 */


/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/net/network.h>

/**
 * @param path
 *	An email address or domain name string.
 *
 * @param flags
 *	Flag bit mask of reserved domains to restrict.
 *
 * @return
 *	True if the domain portion matches a reserved domain. 
 *
 * @see
 *	RFC 2606, 6761, 6762
 */
int
isReservedTLD(const char *path, unsigned long flags)
{
	char *p, *dot;

	if (path == NULL) {
		errno = EFAULT;
		return 0;
	}

	if ((dot = strrchr(path, '.')) == NULL)
		return 0;

	if ((flags & IS_TLD_TEST) && TextInsensitiveCompare(dot, ".test") == 0)
		return 1;

	if ((flags & IS_TLD_EXAMPLE) && TextInsensitiveCompare(dot, ".example") == 0)
		return 1;

	if ((flags & IS_TLD_INVALID) && TextInsensitiveCompare(dot, ".invalid") == 0)
		return 1;

	if ((flags & IS_TLD_LOCALHOST) && TextInsensitiveCompare(dot, ".localhost") == 0)
		return 1;

	/* This is NOT an RFC 2606 reserved domain, but is in common
	 * usage, because of Debian and Redhat doing their own shite.
	 */
	if ((flags & IS_TLD_LOCALDOMAIN) && TextInsensitiveCompare(dot, ".localdomain") == 0)
		return 1;

	/* This is NOT an RFC 2606 reserved domain, but is in common
	 * usage, because of broken Microsoft MCSE recommendations
	 * concerning Active Directory. See RFC 6762.
	 */
	if ((flags & IS_TLD_LOCAL) && TextInsensitiveCompare(dot, ".local") == 0)
		return 1;

	/* This is NOT an RFC 2606 reserved domain, but is in common
	 * usage.
	 */
	if ((flags & IS_TLD_LAN) && TextInsensitiveCompare(dot, ".lan") == 0)
		return 1;

	p = dot+1 - (sizeof ("example.")-1);
	if ((flags & IS_TLD_EXAMPLE)
	&& sizeof ("example.")-1 <= dot-path+1
	&& 0 < TextInsensitiveStartsWith(p, "example.")) {
		if (path == p || (path < p && p[-1] != '-' && !isalnum(p[-1])))
			return 1;
	}

	return 0;
}

/**
 * @param path
 *	An email address or domain name string.
 *
 * @return
 *	True if the domain portion matches the RFC 2606 reserved domains.
 */
int
isRFC2606(const char *path)
{
	return isReservedTLD(path, IS_TLD_ANY_RESERVED);
}

