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
spanDomain(const unsigned char *domain, int minDots)
{
	const unsigned char *start;
	int dots, previous, label_is_alpha;

	if (domain == NULL)
		return 0;

	dots = 0;
	previous = '.';
	label_is_alpha = 1;

	for (start = domain; *domain != '\0'; domain++) {
		switch (*domain) {
		case '.':
#ifdef RFC_1035_STRICT
/* RFC 1035 disallows a trailing hypen in domain labels, but some
 * spam samples have demostrated its use and acceptance by MUAs.
 *
 * http://www.creditcard.com.---------phpsessionoscommerce23452.st-partners.ru/priv/cc/verification.html
 */
			/* A domain segment must end with an alpha-numeric. */
			if (!isalnum(previous))
				return 0;
#endif
			/* Double dots are illegal. */
			if (domain[1] == '.')
				return 0;

			/* Count only internal dots, not the trailing root dot. */
			if (domain[1] != '\0') {
				label_is_alpha = 1;
				dots++;
			}
			break;
		case '-':
#ifdef RFC_1035_STRICT
/* RFC 1035 disallows a leading hypen in domain labels, but some
 * spam samples have demostrated its use and acceptance by MUAs.
 *
 * http://www.creditcard.com.---------phpsessionoscommerce23452.st-partners.ru/priv/cc/verification.html
 */
			/* A domain segment cannot start with a hyphen. */
			if (previous == '.')
				return 0;
#endif
			break;
		default:
			if (!isalnum(*domain))
				goto stop;

#ifndef RFC_1035_STRICT
		/* RFC 1035 section 2.3.1. Preferred name syntax grammar
		 * only allows for alphanumeric, hyphen, and dot in domain
		 * names. However, the DNS system does not disallow other
		 * characters from actually being used in certain record
		 * types and in fact the SPF RFC 4408 susggests utility
		 * labels like "_spf".
		 */
		case '_':
#endif
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
spanHost(const unsigned char *host, int minDots)
{
	int span;

	if (0 < (span = spanIP(host)))
		return span;

	return spanDomain(host, minDots);
}
