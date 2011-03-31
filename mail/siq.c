/*
 * siq.c
 *
 * Copyright 2004, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef SIQ_VERSION
#define SIQ_VERSION			1
#endif

#ifndef SIQ_UDP_PORT
#define SIQ_UDP_PORT			6262
#endif

#ifndef SIQ_TIMEOUT
#define SIQ_TIMEOUT			5000
#endif

#ifndef SIQ_ATTEMPTS
#define SIQ_ATTEMPTS			4
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef ENABLE_PDQ
# include <com/snert/lib/net/pdq.h>
#else
# include <com/snert/lib/io/Dns.h>
#endif
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/siq.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

/***********************************************************************
 *** Network value macros.
 ***********************************************************************/

#define UCHAR_BIT		((unsigned) CHAR_BIT)

/* These macros intended to retrieve network numeric data types stored
 * at odd memory addresses, which can cause some bus errors on certain
 * CPU types if the pointer is cast to a particular type.
 */
#define GET_NET_SHORT(p)	(unsigned short) (                 \
				    ((unsigned char *) p)[0] << 8  \
				  | ((unsigned char *) p)[1]       \
				)

#define GET_NET_LONG(p)		(unsigned long) (                  \
				    ((unsigned char *) p)[0] << 24 \
				  | ((unsigned char *) p)[1] << 16 \
				  | ((unsigned char *) p)[2] << 8  \
				  | ((unsigned char *) p)[3]       \
				)

/***********************************************************************
 ***
 ***********************************************************************/

int
isClientIpHeloEqual(char *client_addr, char *helo)
{
	int rc;
	PDQ_rr *list, *rr;

	rc = 0;

	if (client_addr == NULL || helo == NULL || *helo == '\0')
		goto error0;

	list = pdqFetch5A(PDQ_CLASS_IN, helo);

	for (rr = list; rr != NULL; rr = rr->next) {
		if (rr->section == PDQ_SECTION_QUERY)
			continue;
		if (strcmp(client_addr, ((PDQ_AAAA *) rr)->address.string.value) == 0) {
			rc = 1;
			break;
		}
	}

	pdqListFree(list);
error0:
	return rc;
}

int
isHeloMailSimilar(char *helo, char *domain)
{
	if (domain == NULL)
		return 0;

	return 0 < TextInsensitiveEndsWith(helo, domain);
}

const char siqErrorOpen[] = "SIQ socket open error";
const char siqErrorMemory[] = "SIQ memory allocation error";
const char siqErrorNullArgument[] = "SIQ null argument";
const char siqErrorNoServers[] = "SIQ no servers specified";
const char siqErrorDomainTooLong[] = "SIQ domain name too long";
const char siqErrorReadTimeout[] = "SIQ timeout before input from server";
const char siqErrorVersionMismatch[] = "SIQ version mismatch";
const char siqErrorIdMismatch[] = "SIQ query ID mismatch";
const char siqErrorRead[] = "SIQ read error";

static int
isPrintable(char *text, int length)
{
	while (0 < length--) {
		if (!isprint(*text))
			return 0;
		text++;
	}

	return 1;
}

const char *
siqGetScoreA(SIQ *siq, char *ip, char *helo, char *domain, char **servers)
{
	size_t domainLength;
	const char *comment;
	Socket2 *socket = NULL;
	SocketAddress *address = NULL;
	unsigned short id, responseID;
	long length, timeout, actualTimeout;
	unsigned char query[512], response[512];
	int attempt, maxAttempts, textOffset, nservers;

	comment = NULL;
	siq->score = RESPONSE_ERROR;

	if (siq == NULL) {
		comment = siqErrorNullArgument;
		goto error0;
	}

	if (servers == NULL) {
		comment = siqErrorNoServers;
		goto error0;
	}

	for (nservers = 0; servers[nservers] != NULL; nservers++)
		;

	if (255 < (domainLength = strlen(domain))) {
		comment = siqErrorDomainTooLong;
		goto error0;
	}

	memset(query, 0, sizeof (query));
	(void) parseIPv6(ip, query + QUERY_IP);

	query[QUERY_VERSION] = SIQ_VERSION;

	/* These bits are NOT part of the official protocol at this time. */
	siq->hl = isClientIpHeloEqual(ip, helo);
	siq->ml = isHeloMailSimilar(helo, domain);
	query[QUERY_FLAGS] = (siq->hl << 7) | (siq->ml << 6);

	id = (unsigned short) rand();
	query[QUERY_ID] = (id & 0xff00) >> 8;
	query[QUERY_ID+1] = id & 0x00ff;

	query[QUERY_QD_LENGTH] = (unsigned char) domainLength;
	query[QUERY_EXTRA_LENGTH] = 0;

	(void) memcpy(query + QUERY_QD, domain, query[QUERY_QD_LENGTH]);
	query[QUERY_QD + query[QUERY_QD_LENGTH]] = '\0';

	timeout = SIQ_TIMEOUT;
	maxAttempts = SIQ_ATTEMPTS * nservers;

	for (attempt = 0; attempt < maxAttempts; ++attempt) {
		if ((address = socketAddressCreate(servers[attempt % nservers], SIQ_UDP_PORT)) == NULL) {
			comment = siqErrorMemory;
			goto error0;
		}

		if ((socket = socketOpen(address, 0)) == NULL) {
			comment = siqErrorOpen;
			goto error1;
		}
#ifdef __unix__
		(void) fcntl(socketGetFd(socket), F_SETFD, FD_CLOEXEC);
#endif
		length = socketWriteTo(socket, query, 20 + domainLength + 2, address);

#if defined(__FreeBSD__)
/* sendto() is not defined to return EINVAL under FreeBSD, but under Linux yes.
 * See known issue http://www.freebsd.org/cgi/query-pr.cgi?pr=26506
 */
		if (errno == EINVAL)
			goto nextServer;
#endif
		if (length != 20 + domainLength + 2)
			goto nextServer;

		/* The base timeout value is divided by the number of DNS
		 * servers avaliable in order to limit the actual amount
		 * of time spent looking for an answer. Maintain a mimimum
		 * timeout value.
		 */
		if ((actualTimeout = timeout / nservers) < SIQ_TIMEOUT)
			actualTimeout= SIQ_TIMEOUT;

		if (socketHasInput(socket, actualTimeout))
			break;
nextServer:
		/* Cycle through list of DNS servers. */
		if (attempt % nservers == nservers - 1) {
			/* Double the server timeout value this cycle. */
			timeout += timeout;
		}

		socketClose(socket);
		socket = NULL;

		free(address);
		address = NULL;
	}

	if (maxAttempts <= attempt) {
		comment = siqErrorReadTimeout;
		goto error0;
	}

	memset(response, 0, sizeof (response));
#ifdef LOOK_FOR_MORE
	length = 0;
	do {
		length += socketReadFrom(socket, response+length, sizeof (response)-length, NULL);
	} while (socketHasInput(socket, SIQ_TIMEOUT));
#else
	if ((length = socketReadFrom(socket, response, sizeof (response), NULL)) <= 0) {
		comment = siqErrorRead;
		goto error2;
	}
#endif

	responseID = GET_NET_SHORT(response + RESPONSE_ID);

	/* Correct version? */
	if (response[RESPONSE_VERSION] != SIQ_VERSION) {
		comment = siqErrorVersionMismatch;
		goto error2;
	}

	/* Matching ID? */
	if (id != responseID) {
		comment = siqErrorIdMismatch;
		goto error2;
	}

	if (response[RESPONSE_TTL] == 0 && isPrintable((char *) response + RESPONSE_TEXT_01, response[RESPONSE_TEXT_LENGTH])) {
		/* Draft 01
		 *
		 * The TTL is a signed 32-bit number, but its assumed that
		 * most TTL values will be smallish, ie in the range of 0 to
		 * 0x00FFFFFF (16777215 seconds or 194 days) and negative
		 * values were undefined. So if the TTL high most byte is
		 * zero and it looks like we have a printable text value
		 * then assume draft 01.
		 */
		siq->ttl = (unsigned short) GET_NET_LONG(response + RESPONSE_TTL);
		siq->confidence = (char) response[RESPONSE_CONFIDENCE_01];
		siq->extra_length = response[RESPONSE_EXTRA_LENGTH_01];
		textOffset = RESPONSE_TEXT_01;
	} else if (isPrintable((char *) response + RESPONSE_TEXT_00, response[RESPONSE_TEXT_LENGTH])) {
		/* Draft 00
		 *
		 * There were fewer fields in draft 00 and the text started
		 * where the TTL is now, so if we have a printable text value
		 * assume draft 00.
		 */
		textOffset = RESPONSE_TEXT_00;
		siq->extra_length = 0;
		siq->confidence = -1;
		siq->ttl = 0;
	} else {
/*	       if (isPrintable((char *) response + RESPONSE_TEXT, response[RESPONSE_TEXT_LENGTH])) { */
		/* Draft 02
		 *
		 * The TTL is now an unsigned 16-bit value, 0 to 0xFFFF,
		 * allowing for only less than a whole day of TTL seconds.
		 */
		siq->ttl = (unsigned short) GET_NET_SHORT(response + RESPONSE_TTL);
		siq->confidence = (char) response[RESPONSE_CONFIDENCE];
		siq->extra_length = response[RESPONSE_EXTRA_LENGTH];
		textOffset = RESPONSE_TEXT;
	}

	siq->score = (char) response[RESPONSE_SCORE];
	siq->score_ip = (char) response[RESPONSE_IP_SCORE];
	siq->score_rel = (char) response[RESPONSE_REL_SCORE];
	siq->score_domain = (char) response[RESPONSE_DOMAIN_SCORE];
	siq->text_length = response[RESPONSE_TEXT_LENGTH];
	siq->expires = time(NULL) + siq->ttl;

	memcpy(siq->extra, &response[textOffset + siq->text_length], siq->extra_length);

	/* The response text may be followed by extra data,
	 * therefore we cannot null terminate the string
	 * within the packet itself until here.
	 */
	response[textOffset + siq->text_length] = '\0';
	TextCopy(siq->text, sizeof (siq->text), (char *) response + textOffset);
error2:
	socketClose(socket);
error1:
	free(address);
error0:
	return comment;
}

const char *
siqGetScoreV(SIQ *siq, char *client_addr, char *helo, char *mail_domain, va_list args)
{
	int i;
	char *s, *servers[11];

	for (i = 0; i < 11 && (s = va_arg(args, char *)) != NULL; i++)
		servers[i] = s;
	servers[i] = NULL;

	return siqGetScoreA(siq, client_addr, helo, mail_domain, servers);
}

const char *
siqGetScore(SIQ *siq, char *client_addr, char *helo, char *mail_domain, ...)
{
	va_list args;
	const char *error;

	va_start(args, mail_domain);
	error = siqGetScoreV(siq, client_addr, helo, mail_domain, args);
	va_end(args);

	return error;
}

#ifdef TEST
/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/util/getopt.h>

static char usage[] =
"usage: siq [-cdirstT][-D domain][-H helo][-I ip] siq-server ...\n"
"\n"
"-c\t\tshow confidence value\n"
"-d\t\tshow domain score\n"
"-D domain\tthe domain portion of the MAIL FROM:\n"
"-H helo\t\tthe SMTP EHLO/HELO argument\n"
"-i\t\tshow IP score\n"
"-I ip\t\tthe SMTP client connection IP\n"
"-r\t\tshow IP/domain relationship score\n"
"-s\t\tshow overall score\n"
"-t\t\tshow response text\n"
"-T\t\tshow time-to-live of the response\n"
"\n"
"siq-server\tone or more SIQ server hostnames or IP addresses to query\n"
"\n"
"siq/1.0 Copyright 2005, 2006 by Anthony Howe. All rights reserved.\n"
;

#define SHOW_SCORE		1
#define SHOW_IP_SCORE		2
#define SHOW_REL_SCORE		4
#define SHOW_DOMAIN_SCORE	8
#define SHOW_CONFIDENCE		16
#define SHOW_TTL		32
#define SHOW_TEXT		64

int
main(int argc, char **argv)
{
	int ch;
	SIQ siq;
	char *ip = "";
	char *helo = "";
	char *domain = "";
	const char *error;
	int showAll = -1, flags = 0;

	while ((ch = getopt(argc, argv, "cdirstTD:H:I:")) != -1) {
		switch (ch) {
		case 'c':
			showAll = 0;
			flags |= SHOW_CONFIDENCE;
			break;
		case 'd':
			showAll = 0;
			flags |= SHOW_DOMAIN_SCORE;
			break;
		case 'i':
			showAll = 0;
			flags |= SHOW_IP_SCORE;
			break;
		case 'r':
			showAll = 0;
			flags |= SHOW_REL_SCORE;
			break;
		case 's':
			showAll = 0;
			flags |= SHOW_SCORE;
			break;
		case 't':
			showAll = 0;
			flags |= SHOW_TEXT;
			break;
		case 'T':
			showAll = 0;
			flags |= SHOW_TTL;
			break;
		case 'D':
			domain = optarg;
			break;
		case 'H':
			helo = optarg;
			break;
		case 'I':
			ip = optarg;
			break;
		case 'h':
		default:
			(void) fprintf(stderr, usage);
			return 2;
		}
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, usage);
		return 2;
	}

	if (*ip == '\0' && *domain == '\0') {
		fprintf(stderr, "At least -D and/or -I must be specified.\n");
		return 2;
	}

	if ((error = siqGetScoreA(&siq, ip, helo, domain, argv+optind)) != NULL) {
		fprintf(stderr, "%s\n", error);
		return 1;
	}

	if (showAll)
		flags = ~0;

	if (flags & SHOW_SCORE)
		printf("\t%d", siq.score);
	if (flags & SHOW_IP_SCORE)
		printf("\t%d", siq.score_ip);
	if (flags & SHOW_DOMAIN_SCORE)
		printf("\t%d", siq.score_domain);
	if (flags & SHOW_REL_SCORE)
		printf("\t%d", siq.score_rel);
	if (flags & SHOW_CONFIDENCE)
		printf("\t%d", siq.confidence);
	if (flags & SHOW_TTL)
		printf("\t%u", siq.ttl);
	if (flags & SHOW_TEXT)
		printf("\t%s", siq.text);

	printf("\n");

	return siq.score < 0;
}
#endif /* TEST */
