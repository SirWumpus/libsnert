/*
 * Dns.c
 *
 * RFC 1035 (DNS), 1886 (IPv6), 2821 (SMTP), 2874 (IPv6), 3596 (IPv6)
 *
 * Copyright 2002, 2006 by Anthony Howe. All rights reserved.
 */

#undef NDEBUG

#ifndef DNS_DEFAULT_TIMEOUT
#define DNS_DEFAULT_TIMEOUT	5000
#endif

#ifndef DNS_DEFAULT_ROUNDS
#define DNS_DEFAULT_ROUNDS	4
#endif

#ifndef RESOLV_CONF
#define RESOLV_CONF		"/etc/resolv.conf"
#endif

#ifndef ETC_HOSTS
# ifdef __WIN32__
#  define ETC_HOSTS		"/WINDOWS/system32/drivers/etc/hosts"
# else
#  define ETC_HOSTS		"/etc/hosts"
# endif
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#if defined(__unix__) && ! defined(__CYGWIN__)
# include <syslog.h>
#else
# include <com/snert/lib/io/Log.h>
#endif

#include <com/snert/lib/io/Dns.h>
#include <com/snert/lib/io/posix.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>
#include <com/snert/lib/util/Text.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define DNS_PORT		53
#define UDP_PACKET_SIZE		512

#define CLASS_IN		1
#define CLASS_CS		2
#define CLASS_CH		3
#define CLASS_HS		4

#define OP_QUERY		0x0000
#define OP_IQUERY		0x0800
#define OP_STATUS		0x1000

#define BITS_QR			0x8000	/* Query = 0, response = 1 */
#define BITS_OP			0x7800	/* op-code */
#define BITS_AA			0x0400	/* Response is authoritative. */
#define BITS_TC			0x0200	/* Message was truncated. */
#define BITS_RD			0x0100	/* Recursive query desired. */
#define BITS_RA			0x0080	/* Recursion available from server. */
#define BITS_Z			0x0070	/* Reserved - always zero. */
#define BITS_AU			0x0020	/* Answer authenticaed */
#define BITS_RCODE		0x000f	/* Response code */

#define SHIFT_QR		15
#define SHIFT_OP		11
#define SHIFT_AA		10
#define SHIFT_TC		9
#define SHIFT_RD		8
#define SHIFT_RA		7
#define SHIFT_Z			4
#define SHIFT_RCODE		0

#define LABEL_LENGTH		63
#define DOMAIN_LENGTH		255

/***********************************************************************
 *** Internal types.
 ***********************************************************************/

struct header {
	unsigned short id;
	unsigned short bits;
	unsigned short qdcount;
	unsigned short ancount;
	unsigned short nscount;
	unsigned short arcount;
};

struct packet {
	size_t length;
	struct header header;
	unsigned char data[UDP_PACKET_SIZE - sizeof (struct header)];
};

union socket_address {
	struct sockaddr sa;
	struct sockaddr_in in;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	struct sockaddr_in6 in6;
#endif
};

struct dns {
	int depth;
	int rcode;
	int rounds;
	int socket;
	int nservers;
	int type_addr1;
	int type_addr2;
	long timeout;
	Vector circular;
	const char *error;
	struct packet packet;
	unsigned short counter;
	union socket_address client;
	union socket_address *server;
};

struct host {
	char host[DOMAIN_LENGTH+1];
	unsigned char ip[IPV6_BYTE_LENGTH];
};

static int debug = 0;
static Vector etc_hosts;
static Vector nameServers;

const char DnsErrorNameLength[] = "DNS name length exceeds 255 octets";
const char DnsErrorLabelLength[] = "DNS name segment exceeds 63 octets";
const char DnsErrorSocket[] = "DNS socket error";
const char DnsErrorRead[] = "DNS socket read error";
const char DnsErrorWrite[] = "DNS socket write error";
const char DnsErrorNoAnswer[] = "DNS no response from server";
const char DnsErrorIdMismatch[] = "DNS request & response ID mismatch";
const char DnsErrorFormat[] = "DNS request format error";
const char DnsErrorServer[] = "DNS server failure";
const char DnsErrorNotFound[] = "DNS name not found";
const char DnsErrorNotImplemented[] = "DNS operation not implemented";
const char DnsErrorTruncated[] = "DNS UDP response truncated, TCP support not implemented";
const char DnsErrorRefused[] = "DNS request refused";
const char DnsErrorUnknown[] = "DNS server reported an unknown error";
const char DnsErrorCircular[] = "DNS circular reference";
const char DnsErrorInternal[] = "DNS internal error";
const char DnsErrorMemory[] = "DNS out of memory";
const char DnsErrorNullArgument[] = "DNS null name argument";
const char DnsErrorIpParse[] = "DNS invalid lookup by IP address";
const char DnsErrorUnsupportedType[] = "DNS unsupported type";
const char DnsErrorUndefined[] = "DNS undefined";

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

#define NET_SHORT_BYTE_LENGTH	2
#define NET_LONG_BYTE_LENGTH	4

#ifdef NOT_USED
static unsigned char *
getNetShort(struct packet *pkt, unsigned char *p, unsigned short *value)
{
	if ((unsigned char *) &pkt->header + pkt->length <= p + NET_SHORT_BYTE_LENGTH) {
		syslog(LOG_ERR, "getNetShort() out of bounds!");
		errno = EFAULT;
		return NULL;
	}

	*value = GET_NET_SHORT(p);

	return p + NET_SHORT_BYTE_LENGTH;
}

static unsigned char *
getNetLong(struct packet *pkt, unsigned char *p, unsigned short *value)
{
	if ((unsigned char *) &pkt->header + pkt->length <= p + NET_LONG_BYTE_LENGTH) {
		syslog(LOG_ERR, "getNetLong() out of bounds!");
		errno = EFAULT;
		return NULL;
	}

	*value = GET_NET_LONG(p);

	return p + NET_LONG_BYTE_LENGTH;
}
#endif

/***********************************************************************
 *** Query handling.
 ***********************************************************************/

static void
DnsSetError(Dns dns, int rcode, const char *error)
{
	if (dns == NULL) {
		errno = EFAULT;
		return;
	}

	if (2 < debug)
		syslog(LOG_DEBUG, "DnsSetError(%lx, %d, '%s')", (long) dns, rcode, error);

	((struct dns *) dns)->rcode = rcode;
	((struct dns *) dns)->error = error;
}

/*@null@*/
static const char *
DnsBuildQuery(struct dns *dns, const char *name, int type, int opcode)
{
	int needRoot;
	size_t length;
	struct packet *q;
	unsigned char *s, *t, *label;

	if (2 < debug)
		syslog(LOG_DEBUG, "DnsBuildQuery(%lx, \"%s\", %d, %d)", (long) dns, name, type, opcode);

	q = &dns->packet;
	length = strlen(name);
	needRoot = 0 < length && name[length-1] != '.' && name[length] == '\0';

	if (DOMAIN_LENGTH < length)
		return DnsErrorNameLength;

	/* The header length, length of the name with root segment,
	 * and space for the type and class fields.
	 */
	q->length = sizeof (struct header) + 1 + length + needRoot + 2 * NET_SHORT_BYTE_LENGTH;

	/* Fill in the header fields that are not zero. */
	q->header.id = htons(++dns->counter);

#ifdef NO_RECURSION_NOT_YET_SUPPORTED
	/* Without recursion I would have to perform alot of queries: first query
	 * local DNS, which may return only the NS, then query authorative NS, which
	 * may or may not support recursion and theory more queries for other info.
	 */
	q->header.bits = htons((unsigned short)((opcode & BITS_OP)));
#else
	/* ASSUMPTION: the DNS servers support recursion. */
	q->header.bits = htons((unsigned short)((opcode & BITS_OP) | BITS_RD));
#endif

	q->header.qdcount = htons(1);
	q->header.ancount = 0;
	q->header.nscount = 0;
	q->header.arcount = 0;

	/* Copy labels into question. */
	label = q->data;
	t = label + 1;
	for (s = (unsigned char *) name; *s != '\0' ; ++s, ++t) {
		if (*s == '.') {
			/* A label cannot exceed 63 octets in length. */
			if (LABEL_LENGTH < t - label - 1)
				return DnsErrorLabelLength;

			/* Set label length. */
			*label = (unsigned char)(t - label - 1);
			label = t;
		} else {
			*t = *s;
		}
	}

	/* A label cannot exceed 63 octets in length. */
	if (LABEL_LENGTH < t - label - 1)
		return DnsErrorLabelLength;

	/* Set last label length. */
	*label = (unsigned char)(t - label - 1);
	label = t;

	if (needRoot == 1) {
		/* Set root label length. */
		*label++ = 0;
	}

	/* Complete question, assume the class is the Internet. */
	*label++ = 0;
	*label++ = (unsigned char) type;
	*label++ = 0;
	*label = CLASS_IN;

	return NULL;
}

static int
DnsSendQuery(struct dns *dns)
{
	SOCKET fd = -1;
	ssize_t length;
	int round, sa_len;
	long timeout, actualTimeout;
	union socket_address *thisServer, *lastServer;

	if (2 < debug)
		syslog(LOG_DEBUG, "DnsSendQuery(dns=%lx)", (long) dns);

	timeout = dns->timeout;
	thisServer = dns->server;
	lastServer = &dns->server[dns->nservers];
	dns->client.sa.sa_family = dns->server[0].sa.sa_family;

	/* Using the defaults and assuming a single DNS server, then the
	 * exponential backoff algorithm will result in a max. delay of
	 * 75 seconds (5+10+20+40) to find an answer before giving up.
	 *
	 * With more than one server, the timeout each round is divided
	 * by the number of servers available. So for example:
	 *
	 *	1 server : 5+     10+    20+    40+      = 75 seconds
	 *	2 servers: 5+5+   5+5+   10+10+ 20+20    = 80 seconds
	 *	3 servers: 5+5+5+ 5+5+5+ 6+6+6+ 13+13+13 = 87 seconds
	 */
	for (round = 0; round < dns->rounds; ++round, closesocket(fd)) {
		/* Note that we open and close the socket each iteration
		 * to accomodate a FreeBSD 4 bug that connects the UDP
		 * socket to the server.
		 */
		if ((fd = socket(thisServer->sa.sa_family, SOCK_DGRAM, 0)) < 0) {
			DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorSocket);
			return -1;
		}
#ifdef __unix__
		(void) fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
		sa_len = sizeof (thisServer->in);
#ifdef HAVE_STRUCT_SOCKADDR_IN6
		if (thisServer->sa.sa_family == AF_INET6)
			sa_len = sizeof (thisServer->in6);
#endif
		length = sendto(fd, (void *) &dns->packet.header, dns->packet.length, 0, &thisServer->sa, sa_len);

		/* sendto() is not defined to return EINVAL under FreeBSD, but under
		 * Linux yes. See known issue http://www.freebsd.org/cgi/query-pr.cgi?pr=26506
		 */
		if (length < 0 && errno == EINVAL)
			continue;

		if (length != dns->packet.length) {
			DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorWrite);
			closesocket(fd);
			return -1;
		}

		/* The base timeout value is divided by the number of DNS
		 * servers avaliable in order to limit the actual amount
		 * of time spent looking for an answer. Maintain a mimimum
		 * timeout value.
		 */
		if ((actualTimeout = timeout/dns->nservers) < DNS_DEFAULT_TIMEOUT)
			actualTimeout= DNS_DEFAULT_TIMEOUT;

		if (socketTimeoutIO(fd, actualTimeout, 1))
			break;

		/* Cycle through list of DNS servers. */
		if (++thisServer == lastServer) {
			/* Reset next round to the first server. */
			thisServer = dns->server;

			/* Double the server timeout value for next round. */
			timeout += timeout;
		}
	}

	if (dns->rounds <= round) {
		DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorNoAnswer);
		return -1;
	}

	dns->packet.length = recvfrom(fd, (void *) &dns->packet.header, UDP_PACKET_SIZE, 0, NULL, NULL);
	closesocket(fd);

	if (dns->packet.length < 0) {
		DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorRead);
		return -1;
	}

	dns->packet.header.id = ntohs(dns->packet.header.id);
	dns->packet.header.bits = ntohs(dns->packet.header.bits);
	dns->packet.header.qdcount = ntohs(dns->packet.header.qdcount);
	dns->packet.header.ancount = ntohs(dns->packet.header.ancount);
	dns->packet.header.nscount = ntohs(dns->packet.header.nscount);
	dns->packet.header.arcount = ntohs(dns->packet.header.arcount);

	if (dns->packet.header.id != dns->counter) {
		DnsSetError(dns, DNS_RCODE_FORMAT, DnsErrorIdMismatch);
		return -1;
	}

	/* TODO: check if the message was truncated and redo the request
	 * over TCP as recommend by RFC 974.
	 */
	if (dns->packet.header.bits & BITS_TC) {
		DnsSetError(dns, DNS_RCODE_NOT_IMPLEMENTED, DnsErrorTruncated);
		return -1;
	}

	switch (dns->packet.header.bits & BITS_RCODE) {
	case DNS_RCODE_OK:
		DnsSetError(dns, DNS_RCODE_OK, "");
		break;
	case DNS_RCODE_FORMAT:
		DnsSetError(dns, DNS_RCODE_FORMAT, DnsErrorFormat);
		return -1;
	case DNS_RCODE_SERVER:
		DnsSetError(dns, DNS_RCODE_SERVER, DnsErrorServer);
		return -1;
	case DNS_RCODE_UNDEFINED:
		DnsSetError(dns, DNS_RCODE_UNDEFINED, DnsErrorNotFound);
		return -1;
	case DNS_RCODE_NOT_IMPLEMENTED:
		DnsSetError(dns, DNS_RCODE_NOT_IMPLEMENTED, DnsErrorNotImplemented);
		return -1;
	case DNS_RCODE_REFUSED:
		DnsSetError(dns, DNS_RCODE_REFUSED, DnsErrorRefused);
		return -1;
	default:
		DnsSetError(dns, DNS_RCODE_SERVER, DnsErrorUnknown);
		return -1;
	}

	return 0;
}

/***********************************************************************
 *** Response Record Name Routines
 ***********************************************************************/

/*
 * Return the string length of the resource record name refered to
 * by ptr within the message. The length includes for name label
 * (dot) delimiters and the final root label.
 *
 * An error here would tend to indicate corrupt or hacked packets.
 */
static long
nameLength(struct packet *packet, unsigned char *ptr)
{
	long length;
	unsigned short offset;
	unsigned char *packet_end;

	packet_end = (unsigned char *) &packet->header + packet->length;

	if (ptr < (unsigned char *) &packet->header) {
		syslog(LOG_ERR, "nameLength() below bounds!!!");
		return -1;
	}

	for (length = 0; ptr < packet_end && *ptr != 0; ) {
		if ((*ptr & 0xc0) == 0xc0) {
			offset = GET_NET_SHORT(ptr) & 0x3fff;
			ptr = (unsigned char *) &packet->header + offset;
			continue;
		}

		length += *ptr + 1;
		ptr += *ptr + 1;
	}

	/* Special case where the label is only the root segment (dot). */
	if (length == 0)
		length = 1;

	if (packet_end <= ptr) {
		syslog(LOG_ERR, "nameLength() out of bounds!!!");
		return -1;
	}

	if (DOMAIN_LENGTH < length) {
		syslog(LOG_ERR, "nameLength() domain name too long!!!");
		return -1;
	}

	return length;
}

/*
 * Copy the string of the resource record name refered to by ptr
 * within the message into a buffer of length bytes. The name
 * labels are separated by dots and the final root label is added.
 * The terminating null character is appended.
 *
 * An error here would indicate process memory or stack corruption.
 */
static int
nameCopy(struct packet *packet, unsigned char *ptr, /*@out@*/ unsigned char *buf, long length)
{
	unsigned short offset;
	unsigned char *packet_end;

	packet_end = (unsigned char *) &packet->header + packet->length;

	if (ptr < (unsigned char *) &packet->header) {
		syslog(LOG_ERR, "nameCopy() below bounds!!!");
		return -1;
	}

	while (ptr < packet_end && *ptr != 0) {
		if ((*ptr & 0xc0) == 0xc0) {
			offset = GET_NET_SHORT(ptr) & 0x3fff;
			ptr = (unsigned char *) &packet->header + offset;
			continue;
		}

		/* Do we still have room in the buffer for the next label. */
		if (length <= *ptr) {
			syslog(LOG_ERR, "nameCopy() buffer overflow!!!");
			return -1;
		}

		(void) strncpy((char *) buf, (char *)(ptr+1), (size_t) *ptr);
		buf += *ptr;
		*buf++ = '.';

		length -= *ptr + 1;
		ptr += *ptr + 1;
	}

	if (packet_end <= ptr) {
		syslog(LOG_ERR, "nameCopy() out of bounds!!!");
		return -1;
	}

	/* Special case where the root label is the only segment (dot). */
	if (length == 2 && *ptr == 0) {
		*buf++ = '.';
		length--;
	}

	if (length != 1) {
		syslog(LOG_ERR, "nameCopy() buffer underflow!!!");
		return -1;
	}

	*buf = '\0';

	return 0;
}

/*
 * Return a pointer to the next field within a resource record that
 * follows after the name field refered to by ptr.
 */
static unsigned char *
nameSkip(struct packet *packet, unsigned char *ptr)
{
	unsigned char *packet_end = (unsigned char *) &packet->header + packet->length;

	for ( ; ptr < packet_end && *ptr != 0; ptr += *ptr + 1) {
		if ((*ptr & 0xc0) == 0xc0) {
			/* Skip 0xC0 byte and next one. */
			ptr++;
			break;
		}
	}

	/* Move past root label length or 2nd half of compression offset. */
	ptr++;

	return ptr;
}

/*
 * Return an allocated C string for the resource record name refered
 * to by the ptr within the message. The pointer to the next unread
 * byte following the name field is passed back..
 */
/*@null@*/
static char *
nameGet(struct packet *packet, unsigned char *ptr, unsigned char **stop)
{
	long length;
	unsigned char *buf;

	if ((length = nameLength(packet, ptr)) < 0)
		return NULL;

	if ((buf = malloc(length + 1)) == NULL)
		return NULL;

	if (nameCopy(packet, ptr, buf, length + 1) < 0) {
		free(buf);
		return NULL;
	}

	if (stop != NULL)
		*stop = nameSkip(packet, ptr);

	return (char *) buf;
}

/***********************************************************************
 *** TXT Record
 ***********************************************************************/

DnsTXT *
DnsTxtClone(DnsTXT *txt)
{
	DnsTXT *copy;

	if ((copy = malloc(sizeof (*txt) + txt->length + 1)) != NULL) {
		copy->data = (unsigned char *) &copy[1];
		memcpy(copy->data, txt->data, txt->length);
		copy->data[txt->length] = '\0';
		copy->length = txt->length;
	}

	return copy;
}

/* The TXT resource consists of one or more binary strings,
 * where each string is prefixed by a length octet followed
 * by the string content upto rdlength bytes.
 */
static DnsTXT *
DnsTxtCreate(unsigned char *rdata, unsigned rdlength)
{
	DnsTXT *txt;
	unsigned char *stop, *buf;

	if (rdata == NULL)
		return NULL;

	if ((txt = malloc(sizeof (*txt) + rdlength + 1)) == NULL)
		return NULL;

	txt->data = buf = (unsigned char *) &txt[1];
	txt->length = 0;

	for (stop = rdata + rdlength; rdata < stop; rdata += *rdata + 1) {
		/* Make sure the lengths of the string segments
		 * do not exceed the length of the TXT record.
		 */
		if (rdlength <= txt->length + *rdata) {
			free(txt);
			return NULL;
		}

		memcpy(buf, rdata+1, *rdata);
		txt->length += *rdata;
		buf += *rdata;
	}

	txt->data[txt->length] = '\0';

	return txt;
}

int
DnsTxtPrint(FILE *fp, DnsTXT *txt)
{
	unsigned char *data, *stop;

	stop = txt->data + txt->length;
	for (data = txt->data; data < stop; data++) {
		if (isprint(*data)) {
			if (*data == '\\' || *data == '"' || *data == '\'') {
				if (fputc('\\', fp) == EOF)
					return -1;
			}
			if (fputc(*data, fp) == EOF)
				return -1;
		} else if (fprintf(fp, "\\%.3o", *data) < 0) {
			return -1;
		}
	}

	return 0;
}

/***********************************************************************
 *** Response parsing.
 ***********************************************************************/

void
DnsEntryDestroy(void *entry)
{
	DnsEntry *e = (DnsEntry *) entry;

	if (e != NULL) {
		if (e->type == DNS_TYPE_SOA && e->value != NULL) {
			free(((DnsSOA *) e->value)->mname);
			free(((DnsSOA *) e->value)->rname);
		}
		free(e->address);
		free(e->value);
		free(e->name);
		free(e);
	}
}

static int
DnsCopyAddress(DnsEntry *target, DnsEntry *source)
{
	if (source == NULL || source->address == NULL)
		return -1;

	if (target->address == NULL && (target->address = malloc(IPV6_BYTE_LENGTH + IPV6_STRING_LENGTH)) == NULL)
		return -1;

	target->address_length = source->address_length;
	target->address_string = (char *) target->address + IPV6_BYTE_LENGTH;
	memcpy(target->address, source->address, IPV6_BYTE_LENGTH + IPV6_STRING_LENGTH);

	return 0;
}

static int
DnsSetAddress(DnsEntry *target, unsigned char *source, int length)
{
	int offset;

	if (target->address == NULL && (target->address = malloc(IPV6_BYTE_LENGTH + IPV6_STRING_LENGTH)) == NULL)
		return -1;

	switch (length) {
	case IPV4_BYTE_LENGTH:
		offset = IPV6_OFFSET_IPV4;
		break;
	case IPV6_BYTE_LENGTH:
		offset = 0;
		break;
	default:
		syslog(LOG_ERR, "invalid address length, %d, in DnsSetAddress()", length);
		return -1;
	}

	target->address_length = length;
	target->address_string = (char *) target->address + IPV6_BYTE_LENGTH;

	memset(target->address, 0, IPV6_BYTE_LENGTH);
	memcpy(target->address+offset, source, length);
	formatIP(source, length, 1, target->address_string, IPV6_STRING_LENGTH);

	return 0;
}

DnsEntry *
DnsEntryClone(DnsEntry *entry)
{
	DnsSOA *soa;
	size_t length;
	DnsEntry *clone;

	if ((clone = calloc(1, sizeof (*clone))) == NULL)
		goto error0;

	clone->ttl = entry->ttl;
	clone->type = entry->type;
	clone->preference = entry->preference;

	if ((clone->name = strdup(entry->name)) == NULL)
		goto error1;

	switch (entry->type) {
	case DNS_TYPE_SOA:
		if ((clone->value = malloc(sizeof (*soa))) == NULL)
			goto error1;

		soa = (DnsSOA *) clone->value;
		soa->mname = soa->rname = NULL;
		memcpy(soa, entry->value, sizeof (*soa));

		if ((soa->mname = strdup(((DnsSOA *) entry->value)->mname)) == NULL)
			goto error1;
		if ((soa->rname = strdup(((DnsSOA *) entry->value)->rname)) == NULL)
			goto error1;

		return clone;
	case DNS_TYPE_TXT:
		if ((clone->value = DnsTxtClone(entry->value)) == NULL)
			goto error1;
		return clone;
	default:
		length = strlen(entry->value);
		break;
	}

	if ((clone->value = malloc(length)) == NULL)
		goto error1;
	memcpy(clone->value, entry->value, length);

	if (entry->address != NULL || DnsCopyAddress(clone, entry))
		goto error1;

	return clone;
error1:
	DnsEntryDestroy(clone);
error0:
	return NULL;
}

/*
 * @param type
 *	The DNS record type, other than SOA.
 *
 * @param name
 *
 * @param value
 *
 * @return
 *	A poitner to an allocated DnsEntry structure.
 *	Use free() to release this structure.
 */
static DnsEntry *
DnsEntryCreate(int type, const char *name, const char *value)
{
	DnsEntry *entry;

	if (*name == '\0' || *value == '\0')
		goto error0;

	if (type == DNS_TYPE_SOA)
		/* Not enough info to build this. */
		goto error0;

	if ((entry = calloc(1, sizeof (*entry))) == NULL)
		goto error0;

	if ((entry->name = strdup(name)) == NULL)
		goto error1;

	if ((entry->value = strdup(value)) == NULL)
		goto error2;

	entry->type = (unsigned short) type;

	return entry;
error2:
	free(entry->name);
error1:
	free(entry);
error0:
	return NULL;
}

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param ptr
 *	Start of an unparsed record.
 *
 * @param stop
 *	A pointer to a pointer to the next unparsed record.
 *
 * @return
 *	A poitner to an allocated DnsEntry structure.
 *	Use free() to release this structure.
 */
static DnsEntry *
DnsEntryCreate2(struct dns *dns, unsigned char *ptr, unsigned char **stop)
{
	DnsSOA *soa;
	DnsEntry *entry;
	unsigned short length;

	if (3 < debug)
		syslog(LOG_DEBUG, "enter DnsEntryCreate2(%lx, %lx, %lx)", (long) dns, (long) ptr, (long) stop);

	if ((entry = calloc(1, sizeof (*entry))) == NULL)
		goto error0;

	if ((entry->name = nameGet(&dns->packet, ptr, &ptr)) == NULL)
		goto error1;

	entry->type = GET_NET_SHORT(ptr);
	ptr += NET_SHORT_BYTE_LENGTH;

	/* Assume the record class is IN. Skip */
	ptr += NET_SHORT_BYTE_LENGTH;

	entry->ttl = GET_NET_LONG(ptr);
	ptr += NET_LONG_BYTE_LENGTH;

	length = GET_NET_SHORT(ptr);
	ptr += NET_SHORT_BYTE_LENGTH;

	/* Its assumed that all RR return something of non-zero
	 * length. A TXT record of zero length is assumed to
	 * never occur and of no utility.
	 */
	if (length == 0) {
		syslog(LOG_ERR, "DNS resource record zero length!");
		goto error1;
	}

	/* If the RR would exceed the length of the response
	 * packet then we either a corrupt or hacked packet.
	 */
	if ((unsigned char *) &dns->packet.header + dns->packet.length < ptr + length) {
		syslog(LOG_ERR, "DNS resource record out of bounds!");
		goto error1;
	}

	switch (entry->type) {
	case DNS_TYPE_A:
	case DNS_TYPE_AAAA:
		if (DnsSetAddress(entry, ptr, length))
			goto error1;

		if ((entry->value = strdup(entry->address_string)) == NULL)
			goto error1;

		ptr += length;
		break;

	case DNS_TYPE_SOA:
		if ((entry->value = malloc(sizeof (*soa))) == NULL)
			goto error1;

		soa = (DnsSOA *) entry->value;
		if ((soa->mname = nameGet(&dns->packet, ptr, &ptr)) == NULL)
			goto error1;

		if ((soa->rname = nameGet(&dns->packet, ptr, &ptr)) == NULL) {
			free(soa->mname);
			goto error1;
		}

		soa->serial = GET_NET_LONG(ptr);
		ptr += NET_LONG_BYTE_LENGTH;

		soa->refresh = GET_NET_LONG(ptr);
		ptr += NET_LONG_BYTE_LENGTH;

		soa->retry = GET_NET_LONG(ptr);
		ptr += NET_LONG_BYTE_LENGTH;

		soa->expire = GET_NET_LONG(ptr);
		ptr += NET_LONG_BYTE_LENGTH;

		soa->minimum = GET_NET_LONG(ptr);
		ptr += NET_LONG_BYTE_LENGTH;
		break;

	case DNS_TYPE_MX:
		entry->preference = GET_NET_SHORT(ptr);
		ptr += NET_SHORT_BYTE_LENGTH;
		/*@fallthrough@*/

	case DNS_TYPE_NS:
	case DNS_TYPE_PTR:
		if ((entry->value = nameGet(&dns->packet, ptr, &ptr)) == NULL)
			goto error1;
		break;

	case DNS_TYPE_CNAME:
		if ((entry->value = nameGet(&dns->packet, ptr, &ptr)) == NULL)
			goto error1;

		/* Disallow circular references. */
		if (TextInsensitiveCompare(entry->name, entry->value) == 0) {
			DnsSetError(dns, DNS_RCODE_UNDEFINED, DnsErrorCircular);
			goto error1;
		}
		break;

	case DNS_TYPE_TXT:
		if ((entry->value = DnsTxtCreate(ptr, length)) == NULL)
			goto error1;
		ptr += length;
		break;

	default:
		goto error1;
	}

	if (stop != NULL)
		*stop = ptr;

	if (0) {
error1:
		DnsEntryDestroy(entry);
error0:
		entry = NULL;
	}

	if (3 < debug)
		syslog(
			LOG_DEBUG, "exit  DnsEntryCreate2(%lx, %lx, %lx) entry=%lx {%s %s}",
			(long) &dns->packet, (long) ptr, (long) stop, (long) entry,
			entry == NULL ? "?" : DnsTypeName(entry->type),
			entry == NULL ? "?" : entry->name
		);

	return entry;
}

/*
 * Parse the DNS response packet into a Vector of DnsEntry
 * objects (resource records) for easier manipulation.
 */
static Vector
DnsGetRecordList(struct dns *dns)
{
	int i, j;
	Vector list;
	DnsEntry *entry;
	struct packet *q;
	unsigned char *ptr;

	q = &dns->packet;

	if ((list = VectorCreate(10)) == NULL)
		goto error0;

	VectorSetDestroyEntry(list, DnsEntryDestroy);

	/* Skip question section. */
	ptr = q->data;
	for (i = 0; i < q->header.qdcount; i++) {
		ptr = nameSkip(q, ptr);
		ptr += 2 * NET_SHORT_BYTE_LENGTH;
	}

	/* Add all the returned resource-records to the list. */
	j = q->header.ancount + q->header.nscount + q->header.arcount;
	for (i = 0; i < j; i++) {
		if ((entry = DnsEntryCreate2(dns, ptr, &ptr)) == NULL) {
			VectorDestroy(list);
			list = NULL;
			goto error0;
		}

		if (VectorAdd(list, entry)) {
			DnsEntryDestroy(entry);
			VectorDestroy(list);
			list = NULL;
			goto error0;
		}
	}
error0:
	if (2 < debug)
		syslog(
			LOG_DEBUG, "DnsGetRecordList(%lx) list=%lx qd=%d an=%d ns=%d ar=%d",
			(long) dns, (long) list, q->header.qdcount, q->header.ancount,
			q->header.nscount, q->header.arcount
		);

	return list;
}

static int
DnsIsCircularReference(struct dns *dns, char *name)
{
	long i;
	char *cname;

	/* Check for circular references. If none are found
	 * add the name to the list.
	 */
	for (i = 0; i < VectorLength(dns->circular); i++) {
		if ((cname = VectorGet(dns->circular, i)) == NULL)
			continue;

		if (TextInsensitiveCompare(name, cname) == 0) {
			DnsSetError(dns, DNS_RCODE_UNDEFINED, DnsErrorCircular);
			return 1;
		}
	}

	if (VectorAdd(dns->circular, cname = strdup(name))) {
		DnsSetError(dns, DNS_RCODE_UNDEFINED, DnsErrorInternal);
		free(cname);
		return -1;
	}

	return 0;
}

/*
 * THIS IS A RESURSIVE FUNCTION.
 */
static DnsEntry *
DnsResolve(struct dns *dns, char *name, Vector list, long index)
{
	DnsEntry *entry, *other;

	if ((entry = VectorGet(list, index)) == NULL)
		/* No entry in this response vector. */
		return NULL;

	if (TextInsensitiveCompare(name, entry->name) != 0)
		return DnsResolve(dns, name, list, index+1);

 	if (entry->address != NULL)
		/* We've already have an address for the entry. */
		return entry;

	switch (entry->type) {
	case DNS_TYPE_A:
	case DNS_TYPE_AAAA:
		/* We found it finally. */
		return entry;

	case DNS_TYPE_CNAME:
		/* Dive dive dive. Recurse over the array. */
		if (DnsIsCircularReference(dns, entry->value))
			return NULL;

		if ((other = DnsResolve(dns, entry->value, list, index+1)) != NULL) {
			if (!DnsCopyAddress(entry, other))
				return entry;
		}
	}

	return NULL;
}

static int
DnsResolveAnswers(struct dns *dns, Vector list)
{
	char *host;
	Vector list2;
	int an, ancount, error;
	DnsEntry *other, *entry;

	for (an = 0, ancount = dns->packet.header.ancount; an < ancount; an++) {
		if ((entry = VectorGet(list, an)) == NULL)
			continue;

		switch (entry->type) {
		case DNS_TYPE_SOA:
			host = ((DnsSOA *) entry->value)->mname;
			if (DnsIsCircularReference(dns, host))
				return -1;

			/* Check the response from the additional section.
			 * However, we start in the name server section,
			 * which will be skipped.
			 */
			if ((other = DnsResolve(dns, host, list, ancount)) != NULL) {
				if (DnsCopyAddress(entry, other))
					return -1;
				continue;
			}
			break;
		case DNS_TYPE_MX:
		case DNS_TYPE_NS:
		case DNS_TYPE_PTR:
			host = entry->value;

			/* Check the response from the additional section. */
			if ((other = DnsResolve(dns, host, list, ancount)) != NULL) {
				if (DnsCopyAddress(entry, other))
					return -1;
				continue;
			}
			break;

		case DNS_TYPE_CNAME:
			host = entry->value;

			/* If the lookup was for a CNAME, then a chain of
			 * records appears in the answer section instead
			 * of the additional section.
			 */
			if ((other = DnsResolve(dns, host, list, an+1)) != NULL) {
				if (DnsCopyAddress(entry, other))
					return -1;
				continue;
			}
			break;
		default:
			continue;
		}

		/* Any errors, in particular circular references and
		 * out of memory should stop further processing.
		 */
		if (DnsGetReturnCode(dns) != DNS_RCODE_OK)
			return -1;

		if ((list2 = DnsGet(dns, dns->type_addr1, 1, host)) == NULL) {
			if ((list2 = DnsGet(dns, dns->type_addr2, 1, host)) == NULL) {
				DnsSetError(dns, DNS_RCODE_OK, "");
				entry->address = NULL;
				continue;
			}
		}

		error = DnsCopyAddress(entry, VectorGet(list2, 0));
		VectorDestroy(list2);
		if (error) {
			/* Reset the error state and discard the answer
			 * record when there is no A or AAAA record found
			 * so that we can at least return a partially
			 * useful answer.
			 *
			 * This also allows for RFC 2317 reverse DNS
			 * delegation (ie. CNAME -> PTR -> A) results.
			 */
			DnsSetError(dns, DNS_RCODE_OK, "");
			VectorRemove(list, an--);
			ancount--;
		}
	}

	/* When we do recursive lookups, we keep only the answer
	 * records, for which all the necessary IP addresses have
	 * been determined and we discard the extra information
	 * like the NS section and the AR section. If you really
	 * want those things, then don't do a recursive lookup.
	 */
	VectorRemoveSome(list, ancount, VectorLength(list) - ancount);

	return 0;
}

static Vector
DnsReadResolvConf(const char *cf)
{
	FILE *fp;
	char *line, *token;
	Vector servers, tokens;

	servers = NULL;

	if (cf == NULL) {
#ifdef __WIN32__
		IP_ADDR_STRING *ip;
		ULONG netinfo_size;
		FIXED_INFO *netinfo;

		if ((servers = VectorCreate(1)) == NULL)
			return NULL;

		VectorSetDestroyEntry(servers, free);

		netinfo = NULL;
		netinfo_size = 0;

		/* First get the require buffer size. */
		if (GetNetworkParams(netinfo, &netinfo_size) != ERROR_BUFFER_OVERFLOW)
			goto error3;

		if ((netinfo = malloc(netinfo_size)) == NULL)
			goto error3;

		/* Now fetch the information. */
		if (GetNetworkParams(netinfo, &netinfo_size) != ERROR_SUCCESS) {
error3:
			VectorDestroy(servers);
			free(netinfo);
			return NULL;
		}

		for (ip = &netinfo->DnsServerList; ip != NULL; ip = ip->Next)
			(void) VectorAdd(servers, strdup(ip->IpAddress.String));

		free(netinfo);

		return servers;
#else
		cf = RESOLV_CONF;
#endif
	}

	if ((fp = fopen(cf, "r")) == NULL)
		goto error0;

	if ((line = malloc(BUFSIZ)) == NULL)
		goto error1;

	if ((servers = VectorCreate(5)) == NULL)
		goto error2;

	VectorSetDestroyEntry(servers, free);

	while (0 <= TextInputLine(fp, line, BUFSIZ)) {
		if (*line == '\0' || *line == '#')
			continue;

		if (TextSensitiveStartsWith(line, "nameserver") < 0)
			continue;

		line[strcspn(line, "\r\n")] = '\0';
		if ((tokens = TextSplit(line, " \t", 0)) == NULL)
			continue;

		if (VectorLength(tokens) == 2) {
			token = VectorReplace(tokens, 1, NULL);
			(void) VectorAdd(servers, token);
		}

		VectorDestroy(tokens);
	}

	if (!feof(fp)) {
		VectorDestroy(servers);
		servers = NULL;
	}
error2:
	free(line);
error1:
	fclose(fp);
error0:
	return servers;
}

static Vector
DnsCheckEtcHosts(const char *query, int type)
{
	Vector list;
	struct host *h;
	DnsEntry *entry;
	int i, found, span;
	unsigned char ip[IPV6_BYTE_LENGTH];

	h = NULL;
	found = 0;
	span = parseIPv6(query, ip);

	for (i = 0; i < VectorLength(etc_hosts); i++) {
		if ((h = VectorGet(etc_hosts, i)) == NULL)
			continue;

		if (span == 0 && TextInsensitiveCompare(h->host, query) == 0) {
			found = 1;
			break;
		}

		if (0 < span && memcmp(h->ip, ip, sizeof (ip)) == 0) {
			found = 1;
			break;
		}
	}

	if ((list = VectorCreate(1)) == NULL)
		goto error0;

	VectorSetDestroyEntry(list, DnsEntryDestroy);

	if ((entry = calloc(1, sizeof (*entry))) == NULL)
		goto error1;

	if (VectorAdd(list, entry))
		goto error1;

	if ((entry->address = malloc(IPV6_BYTE_LENGTH + IPV6_STRING_LENGTH)) == NULL)
		goto error1;

	if (found) {
		/* Query name or IP was found in /etc/hosts. */
		if ((entry->name = strdup(h->host)) == NULL)
			goto error1;

		memcpy(entry->address, h->ip, sizeof (h->ip));
	} else if (query[span] == '\0') {
		/* Query looks like an IP address or an IP-as-domain literal.
		 * Create an identity DNS entry for the IP address.
		 */
		if ((entry->name = strdup(query)) == NULL)
			goto error1;

		memcpy(entry->address, ip, sizeof (ip));
	} else {
		goto error1;
	}

	entry->address_string = (char *) entry->address + IPV6_BYTE_LENGTH;
	entry->address_string[0] = '\0';

	if (isReservedIPv6(entry->address, IS_IP_V6))
		entry->address_length = IPV6_BYTE_LENGTH;
	else
		entry->address_length = IPV4_BYTE_LENGTH;

	formatIP(
		entry->address + IPV6_BYTE_LENGTH - entry->address_length,
		entry->address_length, 0,
		entry->address_string, IPV6_STRING_LENGTH
	);

	if ((entry->value = strdup(entry->address_string)) == NULL)
		goto error1;

	entry->type = (unsigned short) type;
	entry->ttl = 1;

	return list;
error1:
	VectorDestroy(list);
error0:
	return NULL;
}

static int
compareMxPreferences(const void *a, const void *b)
{
	return (*(DnsEntry **) a)->preference - (*(DnsEntry **) b)->preference;
}

/***********************************************************************
 *** Principal DNS routines.
 ***********************************************************************/

void
DnsSetNameServers(char **servers)
{
	if (nameServers == NULL)
		DnsInit(NULL);

	if (servers != NULL) {
		VectorRemoveAll(nameServers);

		for ( ; *servers != NULL; servers++)
			(void) VectorAdd(nameServers, strdup(*servers));
	}
}

/*
 * (Re)Load the /etc/resolv.conf file. Currently only nameserver lines
 * are recognised. This function should be called before the first
 * DnsOpen() call.
 *
 * @param resolv_conf
 *	The file path of a resolv.conf file to parse. If NULL, then
 *	the default for Unix is to parse /etc/resolv.conf and for
 *	Windows the DNS servers are looked up via the Windows API.
 *
 * @return
 *	Zero (0) on success otherwise -1 one error.
 */
int
DnsInit(char *resolv_conf)
{
	FILE *fp;
	int rc = -1;

#if defined(__WIN32__)
{
	static int SocketSupportLoaded = 0;

	if (!SocketSupportLoaded) {
		int rc;
		WORD version;
		WSADATA wsaData;

		version = MAKEWORD(2, 2);
		if ((rc = WSAStartup(version, &wsaData)) != 0) {
			syslog(LOG_ERR, "DnsInit: WSAStartup() failed: %d", rc);
			goto error0;
		}

		if (HIBYTE( wsaData.wVersion ) < 2 || LOBYTE( wsaData.wVersion ) < 2) {
			syslog(LOG_ERR, "DnsInit: WinSock API must be version 2.2 or better.");
			(void) WSACleanup();
			goto error0;
		}

		if (atexit((void (*)(void)) WSACleanup)) {
			syslog(LOG_ERR, "DnsInit: atexit(WSACleanup) failed: %s", strerror(errno));
			goto error0;
		}

		SocketSupportLoaded = 1;
	}
}
#endif

	VectorDestroy(nameServers);
	nameServers = DnsReadResolvConf(resolv_conf);

	if (nameServers == NULL) {
		if ((nameServers = VectorCreate(1)) == NULL)
			goto error0;
		VectorSetDestroyEntry(nameServers, free);
		VectorAdd(nameServers, strdup("0.0.0.0"));
	}

	VectorRemoveAll(etc_hosts);
	if ((fp = fopen(ETC_HOSTS, "r")) != NULL) {
		int span;
		char line[512];
		struct host *h;

		if (etc_hosts == NULL && (etc_hosts = VectorCreate(10)) == NULL) {
			fclose(fp);
			goto error0;
		}

		while (fgets(line, sizeof (line), fp) != NULL) {
			if (*line == '#' || *line == ';'|| line[strspn(line, " \t")] == '\0')
				continue;
			if ((h = malloc(sizeof (*h))) == NULL) {
				fclose(fp);
				goto error0;
			}

			if ((span = parseIPv6(line, h->ip)) == 0) {
				free(h);
				continue;
			}

			sscanf(line+span, "%255s", h->host);
			if (VectorAdd(etc_hosts, h))
				free(h);
		}

		fclose(fp);
	}

	rc = 0;
error0:
	if (0 < debug)
		syslog(LOG_DEBUG, "DnsInit(%s) rc=%d", TextNull(resolv_conf), rc);

	return rc;
}

/**
 * We're finished with the Dns subsystem.
 */
void
DnsFini(void)
{
	VectorDestroy(nameServers);
}

/*
 * @param servers
 *	A NULL terminate array of C string pointers. Each C string
 * 	is the IP address of a DNS server to consult in order of
 *	preference. This argument can be NULL, in which case the
 *	list of DNS servers from /etc/resolv.conf are consulted.
 *
 * @return
 *	A Dns object returned by DnsOpen().
 */
Dns
DnsOpen(void)
{
	int i;
	struct dns *dns;
	unsigned short port;
	const char **servers;
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	if (0 < debug)
		syslog(LOG_DEBUG, "enter DnsOpen()");

	if (nameServers == NULL)
		(void) DnsInit(NULL);

	servers = (const char **) VectorBase(nameServers);
	i = (int) VectorLength(nameServers);

	/* Allocate one block for everything. */
	dns = (struct dns *) malloc(sizeof *dns + i * sizeof (*dns->server));
	if (dns == NULL)
		goto error0;

	if ((dns->circular = VectorCreate(5)) == NULL)
		goto error1;
	VectorSetDestroyEntry(dns->circular, free);

	port = htons(DNS_PORT);

	/* Use "this host" (::0 or 0.0.0.0) instead of "localhost". Jailed
	 * FreeBSD virtual machines have no loopback interface (::1 or
	 * 127.0.0.1).
	 */
	memset(&dns->client, 0, sizeof (dns->client));

	dns->nservers = i;
	dns->server = (union socket_address *)(dns + 1);

	for (i = 0; i < dns->nservers; i++) {
		if (parseIPv6(servers[i], ipv6) == 0)
			goto error1;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
		if (isReservedIPv6(ipv6, IS_IP_V6)) {
			dns->server[i].in6.sin6_port = port;
			dns->server[i].in6.sin6_family = AF_INET6;
			memcpy(&dns->server[i].in6.sin6_addr, ipv6, sizeof (ipv6));
		} else
#endif
		{
			dns->server[i].in.sin_port = port;
			dns->server[i].in.sin_family = AF_INET;
			memcpy(&dns->server[i].in.sin_addr, ipv6+sizeof (ipv6)-IPV4_BYTE_LENGTH, IPV4_BYTE_LENGTH);
		}
	}

	dns->counter = (unsigned short) rand();
	DnsSetAddressOrder(dns, DNS_TYPE_A, DNS_TYPE_AAAA);
	DnsSetTimeout(dns, DNS_DEFAULT_TIMEOUT);
	DnsSetRounds(dns, DNS_DEFAULT_ROUNDS);
	dns->depth = 0;

	if (0) {
error1:
		DnsClose(dns);
error0:
		dns = NULL;
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "exit  DnsOpen() Dns=%lx", (long) dns);

	return (Dns) dns;
}

/*
 * @param dns
 *	A Dns object returned by DnsOpen() to close.
 */
void
DnsClose(Dns dns)
{
	struct dns *d = (struct dns *) dns;

	if (d != NULL) {
		VectorDestroy(d->circular);
		free(d);
	}

	if (0 < debug)
		syslog(LOG_DEBUG, "DnsClose(%lx)", (long) dns);
}

#ifdef MOVED
long
reverseSegmentOrder(const char *string, const char *delims, char *buffer, int size)
{
	char *x, *y, ch;
	long length, span;

	if (string == NULL || size <= 0)
		return 0;

	/* Copy string to the buffer. */
	if (size <= (length = TextCopy(buffer, size, (char *) string)))
		return length;

	/* Remove the trailing delimiter if present. */
	if (0 < length && strchr(delims, buffer[length-1]) != NULL)
		buffer[--length] = '\0';

	/* Reverse the entire string to reverse the segment order. */
	for (x = buffer, y = buffer + length; x < --y; x++) {
		ch = *y;
		*y = *x;
		*x = ch;
	}

	/* For each segement, reverse it to restore the substring order. */
	for ( ; 0 < (span = strcspn(buffer, delims)); buffer += span + (buffer[span] != '\0')) {
		for (x = buffer, y = buffer + span; x < --y; x++) {
			ch = *y;
			*y = *x;
			*x = ch;
		}
	}

	return length;
}

long
reverseByNibble(const char *group, char *buffer, int size)
{
	char *stop;
	unsigned short word;
	int i, nibble, length = 0;

	word = (int) strtol(group, &stop, 16);
	if (*stop == ':')
		length = reverseByNibble(stop+1, buffer, size);

	for (i = 0; i < 4; i++) {
		nibble = word & 0xf;
		word >>= 4;
		length += snprintf(buffer+length, size-length, "%x.", nibble);
	}

	return length;
}

long
reverseSegments(const char *source, const char *delims, char *buffer, int size, int arpa)
{
	long length;
	char ip[IPV6_STRING_LENGTH];
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	if (TextInsensitiveCompareN(source, IPV6_TAG, IPV6_TAG_LENGTH) == 0)
		source += IPV6_TAG_LENGTH;

	if (strchr(source, ':') == NULL) {
		length = reverseSegmentOrder(source, delims, buffer, size);
		if (arpa)
			length += TextCopy(buffer+length, size-length, ".in-addr.arpa.");
	} else {
		/* Is it a compact IPv6 address? */
		if (strstr(source, "::") != NULL) {
			/* Convert to a binary IP address. */
			(void) parseIPv6(source, ipv6);

			/* Convert back to full IPv6 address string. */
			formatIP(ipv6, IPV6_BYTE_LENGTH, 0, ip, sizeof (ip));
			source = ip;
		}

		length = reverseByNibble(source, buffer, size);

		/* Remove trailing dot from last nibble. */
		if (buffer[length-1] == '.')
			buffer[--length] = '\0';

		if (arpa)
			length += TextCopy(buffer+length, size-length, ".ip6.arpa.");
	}

	return length;
}


long
reverseIp(const char *source, char *buffer, int size, int arpa)
{
	return reverseSegments(source, ".", buffer, size, arpa);
}
#endif

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param type
 *	The DNS resource record type to search for. See DNS_TYPE_ constants.
 *	If type is DNS_TYPE_PTR and the name argument is not already in .arpa
 *	form, then the IPv4 or IPv6 address will be reversed into its .arpa
 *	form.
 *
 * @param recursive
 *	If true, DnsGet() will attempt to resolve the answers given by
 *	the initial query to an IP address and discard the supplemental
 *	information once complete. If false, then all the records from
 *	the first query are returned.
 *
 * @param name
 *	A domain or host name to search for. In the case of a PTR
 * 	search, its the IP address.
 *
 * @return
 *	A Vector of DnsEntry pointers on success, otherwise a NULL
 *	pointer on error. The Vector may be empty. When finished
 *	with the Vector, pass it to VectorDestroy() to clean up.
 */
Vector
DnsGet(Dns d, int type, int recursive, const char *arg)
{
	const char *error;
	Vector list = NULL;
	int length, ancount;
	char *buffer = NULL;
	struct dns *dns = d;
	const char *name, *typeName = DnsTypeName(type);

	if (0 < debug)
		syslog(LOG_DEBUG, "enter DnsGet(%lx, %s=%d, %d, %s)", (long) dns, typeName, type, recursive, arg);

	/* We maintain a depth count, so that we empty the CNAME
	 * circular reference list only at the top most level.
	 */
	dns->depth++;
	name = arg;

	if (name == NULL) {
		DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorNullArgument);
		errno = EINVAL;
		goto error1;
	}

	DnsSetError(dns, DNS_RCODE_OK, "");

	switch (type) {
	case DNS_TYPE_A:
	case DNS_TYPE_MX:
	case DNS_TYPE_AAAA:
		/* Handle an for IP-as-domain literal, ie. "[123.45.67.89]".
		 * This is a convience and simply converts the IP address
		 * into a psuedo A or AAAA record.
		 */
		if ((list = DnsCheckEtcHosts(name, type)) != NULL)
			goto error1;
		break;
	case DNS_TYPE_NS:
	case DNS_TYPE_SOA:
	case DNS_TYPE_TXT:
	case DNS_TYPE_CNAME:
		/*** This is a VERY BAD THING. I'm (re)using a large buffer
		 *** that I know to be idle at this point for something other
		 *** than its intended purpose in order to parse the name.
		 ***/
		if (parseIPv6(name, (unsigned char *) &dns->packet.header)) {
			DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorIpParse);
			errno = EINVAL;
			goto error1;
		}
		break;
	case DNS_TYPE_PTR:
		/* If the request is not already rooted in .arpa,
		 * then reverse the IP address ourselves.
		 */
		if (strstr(name, ".arpa") == NULL) {
			if ((buffer = malloc(DOMAIN_LENGTH+1)) == NULL)
				goto error1;

			length = reverseIp(name, buffer, DOMAIN_LENGTH+1, 1);
			name = buffer;

			if (2 < debug)
				syslog(LOG_DEBUG, "reversed-ip=%s", buffer);
		}
		break;
	default:
		DnsSetError(dns, DNS_RCODE_ERRNO, DnsErrorUnsupportedType);
		errno = EINVAL;
		goto error1;
	}

	if ((error = DnsBuildQuery(dns, name, type, OP_QUERY)) != NULL) {
		DnsSetError(dns, DNS_RCODE_FORMAT, error);
		goto error2;
	}

	if (DnsSendQuery(dns))
		goto error2;

	if (DnsGetReturnCode(dns) != DNS_RCODE_OK)
		goto error2;

	if ((list = DnsGetRecordList(dns)) == NULL) {
		if (errno == ENOMEM)
			DnsSetError(dns, DNS_RCODE_ERRNO, "out of memory");
		goto error2;
	}

	/* Save a copy of this value, since the packet buffer
	 * will be destroyed on recursive calls to DnsGet().
	 */
	ancount = dns->packet.header.ancount;

	if (recursive) {
		DnsEntry *entry;

		/*** TODO when the requested record type returns an
		 *** SOA instead, then we should repeat the request
		 *** using their authorative name server.
		 ***/
		if (type != DNS_TYPE_SOA && (entry = VectorGet(list, 0)) != NULL && entry->type == DNS_TYPE_SOA) {
			DnsSetError(dns, DNS_RCODE_UNDEFINED, DnsErrorUndefined);
			VectorRemove(list, 0);
		}

		switch (type) {
		case DNS_TYPE_MX:
			/* If the answer section is empty, then discard the
			 * extra records and do RFC 974 section "Interpreting
			 * the List of MX RRs" paragraph 2 special case handling.
			 */
			if (ancount == 0) {
				VectorRemoveAll(list);

				/* Create a fake MX 0 entry for the host name,
				 * then resolve that below.
				 */
				if ((entry = DnsEntryCreate(DNS_TYPE_MX, name, name)) == NULL)
					goto error2;

				if (VectorAdd(list, entry)) {
					DnsEntryDestroy(entry);
					goto error2;
				}

				DnsSetError(dns, DNS_RCODE_OK, "");
				ancount = dns->packet.header.ancount = 1;
				dns->packet.header.nscount = 0;
				dns->packet.header.arcount = 0;
			}
			/*@fallthrough@*/

		case DNS_TYPE_A:
		case DNS_TYPE_AAAA:
		case DNS_TYPE_NS:
		case DNS_TYPE_PTR:
		case DNS_TYPE_SOA:
		case DNS_TYPE_CNAME:
			/* Fill in the blanks. */
			if (list != NULL && DnsResolveAnswers(dns, list)) {
				VectorDestroy(list);
				list = NULL;
			}
			break;
		}

		if (type == DNS_TYPE_MX) {
			if (debug)
				syslog(LOG_DEBUG, "VectorSort");
			VectorSort(list, compareMxPreferences);
		}
	}

	/* Discard everything but the actual answer records. */
	VectorRemoveSome(list, ancount, VectorLength(list) - ancount);
error2:
	free(buffer);
error1:
	if (--dns->depth == 0)
		VectorRemoveAll(dns->circular);

	if (0 < debug)
		/* FIXME for DNS_TYPE_PTR, name = buffer that was just freed
		 * and could cause undefined and undesiredable behaviour.
		 */
		syslog(
			LOG_DEBUG, "exit  DnsGet(%lx, %s=%d, %d, %s) Vector=%lx rc=%d error=%s",
			(long) dns, typeName, type, recursive, arg, (long) list,
			DnsGetReturnCode(dns), DnsGetError(dns)
		);

	return list;
}

/**
 * A DnsGet() wrapper that handles the calls to DnsOpen(), DnsGet(),
 * and DnsClose() using the default settings. Since DnsGet2() is a
 * simple wrapper for several complex function calls, error reporting
 * is not as precise as it could be; essentially the DNS_RCODE_ values
 * are mapped onto errno values.
 *
 *	DNS_RCODE_OK 			0
 *	DNS_RCODE_FORMAT		EINVAL
 *	DNS_RCODE_SERVER		EFAULT
 *	DNS_RCODE_UNDEFINED		ENOENT
 *	DNS_RCODE_NOT_IMPLEMENTED	EINVAL
 *	DNS_RCODE_REFUSED		EPERM
 *
 * @see DnsGet()
 */
int
DnsGet2(int type, int recursive, const char *name, Vector *result, const char **error)
{
	int rc;
	Dns dns;
	Vector entries = NULL;

	if ((dns = DnsOpen()) == NULL)
		return DNS_RCODE_ERRNO;

	if (name == NULL || *name == '\0')
		name = "127.0.0.1";

	entries = DnsGet(dns, type, recursive, name);
	rc = DnsGetReturnCode(dns);

	if (result == NULL || rc != DNS_RCODE_OK) {
		VectorDestroy(entries);
		entries = NULL;
	}

	if (error != NULL)
		*error = DnsGetError(dns);
	if (result != NULL)
		*result = entries;

	DnsClose(dns);

	return rc;
}

/***********************************************************************
 ***
 ***********************************************************************/

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @return
 *	A C string error message for the corresponding DNS_DNS_RCODE_ value.
 */
const char *
DnsGetError(Dns dns)
{
	if (dns == NULL) {
		errno = EFAULT;
		return (const char *) "null pointer to DNS structure";
	}

	return ((struct dns *) dns)->error;
}

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @return
 *	A DNS_RCODE_ value. The value DNS_RCODE_ERRNO is an internal
 *	error conditions not returned from a DNS server.
 */
int
DnsGetReturnCode(Dns dns)
{
	if (dns == NULL) {
		errno = EFAULT;
		return DNS_RCODE_ERRNO;
	}

	return ((struct dns *) dns)->rcode;
}

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param n
 *	Maximum number of times to cycle through the DNS server
 *	list before giving up on a search. Default is 4.
 */
void
DnsSetRounds(Dns dns, int rounds)
{
	if (rounds < 1)
		rounds = 1;

	if (0 < debug)
		syslog(LOG_DEBUG, "DnsSetRounds(%lx, %d)", (long) dns, rounds);

	((struct dns *) dns)->rounds = rounds * ((struct dns *) dns)->nservers;
}

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param ms
 *	The initial timeout in milliseconds to wait for a response
 *	from a DNS server. An exponential backup algorithm is used.
 *	The default is 5000 ms.
 *
 * Using the defaults and assuming a single DNS server, then the
 * exponential backoff algorithm will result in a max. delay of
 * 75 seconds (5+10+20+40) to find an answer before giving up.
 *
 * With more than one server, the timeout each round is divided
 * by the number of servers available. So for example:
 *
 *	1 server : 5+     10+    20+    40+      = 75 seconds
 *	2 servers: 5+5+   5+5+   10+10+ 20+20    = 80 seconds
 *	3 servers: 5+5+5+ 5+5+5+ 6+6+6+ 13+13+13 = 87 seconds
 */
void
DnsSetTimeout(Dns dns, long ms)
{
	if (ms < 1000)
		ms = 1000;

	if (0 < debug)
		syslog(LOG_DEBUG, "DnsSetTimeout(%lx, %ld)", (long) dns, ms);

	((struct dns *) dns)->timeout = ms;
}

/*
 * @param dns
 *	A Dns object returned by DnsOpen().
 *
 * @param first
 *	One of DNS_TYPE_A or DNS_TYPE_AAAA.
 *
 * @param second
 *	One of DNS_TYPE_A, DNS_TYPE_AAAA, or zero (0).
 */
void
DnsSetAddressOrder(Dns dns, int first, int second)
{
	if (first == DNS_TYPE_A || first == DNS_TYPE_AAAA) {
		((struct dns *) dns)->type_addr1 = first;

		if (first == second || (second != DNS_TYPE_A && second != DNS_TYPE_AAAA))
			second = 0;

		((struct dns *) dns)->type_addr2 = second;
	}
}

/***********************************************************************
 *** Support routines.
 ***********************************************************************/

/*
 * @param level
 */
void
DnsSetDebug(int level)
{
	debug = level;
}

const char *
DnsTypeName(int type)
{
	switch (type) {
	case DNS_TYPE_A:	return "A";
	case DNS_TYPE_NS:	return "NS";
	case DNS_TYPE_CNAME:	return "CNAME";
	case DNS_TYPE_SOA:	return "SOA";
	case DNS_TYPE_WKS:	return "WKS";	/* not supported */
	case DNS_TYPE_PTR:	return "PTR";
	case DNS_TYPE_HINFO:	return "HINFO";	/* not supported */
	case DNS_TYPE_MINFO:	return "MINFO";	/* not supported */
	case DNS_TYPE_MX:	return "MX";
	case DNS_TYPE_TXT:	return "TXT";
	case DNS_TYPE_AAAA:	return "AAAA";
	case DNS_TYPE_A6:	return "A6";	/* not supported */
	}

	return "(unknown)";
}

struct mapping {
	int code;
	char *name;
};

static struct mapping typeMap[] = {
	{ DNS_TYPE_A,		"A"	},
	{ DNS_TYPE_NS,		"NS"	},
	{ DNS_TYPE_CNAME,	"CNAME"	},
	{ DNS_TYPE_SOA,		"SOA"	},
	{ DNS_TYPE_WKS,		"WKS"	},
	{ DNS_TYPE_PTR,		"PTR"	},
	{ DNS_TYPE_HINFO,	"HINFO"	},
	{ DNS_TYPE_MINFO,	"MINFO"	},
	{ DNS_TYPE_MX,		"MX"	},
	{ DNS_TYPE_TXT,		"TXT"	},
	{ DNS_TYPE_AAAA,	"AAAA"	},
	{ DNS_TYPE_A6,		"A6"	},
	{ 0, 			NULL }
};

int
DnsTypeCode(const char *typeName)
{
	struct mapping *map;

	for (map = typeMap; map->name != NULL; map++)
		if (TextInsensitiveCompare(typeName, map->name) == 0)
			return map->code;
	return -1;
}

void
DnsEntryDump(FILE *fp, DnsEntry *entry)
{
	DnsSOA *soa;
	unsigned char buffer[IPV6_STRING_LENGTH];

	if (entry == NULL)
		return;

	*buffer = '\0';
	fprintf(fp, "%s %ld IN %s ", entry->name, entry->ttl, DnsTypeName(entry->type));

	switch (entry->type) {
#ifdef CONVERT_TO_DUMP
	case DNS_TYPE_A:
		formatIP(entry->address+12, IPV4_BYTE_LENGTH, 1, buffer, sizeof (buffer));
		fprintf(fp, "%s ", buffer);
		break;
	case DNS_TYPE_AAAA:
		formatIP(entry->address, IPV6_BYTE_LENGTH, 1, buffer, sizeof (buffer));
		fprintf(fp, "%s ", buffer);
		break;
#endif
	case DNS_TYPE_SOA:
		soa = entry->value;
		fprintf(
			fp, "%s %s (%lu %ld %ld %ld %lu)",
			soa->mname, soa->rname, soa->serial,
			soa->refresh, soa->retry, soa->expire, soa->minimum
		);
		break;
	case DNS_TYPE_MX:
		fprintf(fp, "%d ", entry->preference);
		/*@fallthrough@*/
#ifndef CONVERT_TO_DUMP
	case DNS_TYPE_A:
	case DNS_TYPE_AAAA:
#endif
	case DNS_TYPE_NS:
	case DNS_TYPE_PTR:
	case DNS_TYPE_CNAME:
		fprintf(fp, "%s", (char *) entry->value);
		break;
	case DNS_TYPE_TXT:
		fputc('"', fp);
		DnsTxtPrint(fp, entry->value);
		fputc('"', fp);
		break;
	}

	if (entry->address_string != NULL)
		fprintf(fp, " ; %s ", entry->address_string);

	fprintf(fp, "\n");
}

#ifdef TEST

int
main(int argc, char **argv)
{
	long i;
	Dns dns;
	DnsEntry *entry;
	const char *error;
	Vector entries = NULL;

	if (argc < 3) {
		fprintf(stderr, "usage: Dns type domain|ip [server ...]\n");
		fprintf(stderr, LIBSNERT_STRING " " LIBSNERT_COPYRIGHT "\n");
		exit(2);
	}

	if (DnsInit(NULL)) {
		fprintf(stderr, "DnsInit() failed\n");
		exit(1);
	}

	if (argc == 3) {
		if ((i = DnsGet2(DnsTypeCode(argv[1]), 1, argv[2], &entries, &error)) != DNS_RCODE_OK) {
			fprintf(stderr, "%s (%ld)\n", error, i);
			DnsFini();
			exit(1);
		}
	} else {
		DnsSetNameServers(&argv[3]);
		if ((dns = DnsOpen()) == NULL) {
			fprintf(stderr, "DnsOpen() failed: %s (%d)\n", strerror(errno), errno);
			DnsFini();
			exit(1);
		}

		entries = DnsGet(dns, DnsTypeCode(argv[1]), 1, argv[2]);
		if (entries == NULL || DnsGetReturnCode(dns) != DNS_RCODE_OK) {
			fprintf(stderr, "DnsGet() error: %s (%d)\n", DnsGetError(dns), DnsGetReturnCode(dns));
			DnsFini();
			exit(1);
		}

		DnsClose(dns);
	}

	for (i = 0; i < VectorLength(entries); i++) {
		entry = VectorGet(entries, i);
		DnsEntryDump(stdout, entry);
	}

	VectorDestroy(entries);
	DnsFini();

	return 0;
}

#endif /* TEST */

/***********************************************************************
 *** END
 ***********************************************************************/
