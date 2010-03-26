/*
 * spanHost.c
 *
 * Copyright 2010 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/net/network.h>
#include <com/snert/lib/util/Text.h>

/*
 * RFC 2821 domain syntax excluding address-literal.
 *
 * Note that RFC 1035 section 2.3.1 indicates that domain labels
 * should begin with an alpha character and end with an alpha-
 * numeric character. However, all numeric domains do exist, such
 * as 123.com, so are permitted.
 */
int
spanDomain(const char *domain, int minDots)
{
	const char *start;
	int dots, previous, label_is_alpha;

	if (domain == NULL)
		return 0;

	dots = 0;
	previous = '.';
	label_is_alpha = 1;

	for (start = domain; *domain != '\0'; domain++) {
		switch (*domain) {
		case '.':
			/* A domain segment must end with an alpha-numeric. */
			if (!isalnum(previous))
				goto stop;

			/* Double dots are illegal. */
			if (domain[1] == '.')
				return 0;

			/* Count only internal dots, not the trailing root dot. */
			if (domain[1] != '\0') {
				label_is_alpha = 1;
				dots++;
			}
			break;
		case '-': case '_':
			/* A domain segment cannot start with a hyphen. */
			if (previous == '.')
				goto stop;
			break;
		default:
			if (!isalnum(*domain))
				goto stop;

			label_is_alpha = label_is_alpha && isalpha(*domain);
			break;
		}

		previous = *domain;
	}

	/* Top level domain must end with dot or alpha character. */
	if (0 < dots && !label_is_alpha)
		return 0;
stop:
	if (dots < minDots)
		return 0;

	return domain - start;
}


int
spanHost(const char *host, int minDots)
{
	int span;

	if (0 < (span = spanDomain(host, minDots)))
		return span;

	return spanIP(host);
}
