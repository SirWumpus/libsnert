/**
 * pdq.c
 *
 * Parallel Domain Query
 *
 * RFC 1035 (DNS), 1886 (IPv6), 2821 (SMTP), 2874 (IPv6), 3596 (IPv6)
 *
 * Copyright 2002, 2008 by Anthony Howe. All rights reserved.
 */

#undef NDEBUG

#ifndef MAX_CNAME_DEPTH
#define MAX_CNAME_DEPTH		10
#endif

#ifndef RESOLV_CONF
#define RESOLV_CONF		"/etc/resolv.conf"
#endif

#ifndef MAX_SPR_ATTEMPTS
#define MAX_SPR_ATTEMPTS	5
#endif

#ifndef PDQ_POLL_NEXT_PACKET_TIMEOUT_MS
#define PDQ_POLL_NEXT_PACKET_TIMEOUT_MS		5
#endif

#ifndef ETC_HOSTS
# ifdef __WIN32__
#  define ETC_HOSTS		"/WINDOWS/system32/drivers/etc/hosts"
# else
#  define ETC_HOSTS		"/etc/hosts"
# endif
#endif

#ifdef HAVE_RAND_R
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand_r(&rand_seed) / (RAND_MAX+1.0)))
#else
#define RANDOM_NUMBER(max)	((int)((double)(max) * (double) rand() / (RAND_MAX+1.0)))
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

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifndef __MINGW32__
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif
#include <com/snert/lib/io/Log.h>

#include <com/snert/lib/io/file.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/tlds.h>
#include <com/snert/lib/type/Vector.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/timer.h>
#include <com/snert/lib/net/pdq.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define DNS_PORT		53
#define NET_SHORT_BYTE_LENGTH	2
#define NET_LONG_BYTE_LENGTH	4
#define UDP_PACKET_SIZE		512

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
#define DOMAIN_LENGTH		(DOMAIN_STRING_LENGTH-1)

/***********************************************************************
 *** Internal types.
 ***********************************************************************/

struct header {
	uint16_t id;
	uint16_t bits;
	uint16_t qdcount;
	uint16_t ancount;
	uint16_t nscount;
	uint16_t arcount;
};

struct udp_packet {
	uint16_t length;
	struct header header;
	uint8_t data[UDP_PACKET_SIZE - sizeof (struct header)];
};

struct tcp_packet {
	uint16_t length;
	struct header header;
	uint8_t data[64 * 1024 - sizeof (struct header)];
};

typedef struct pdq_link {
	struct pdq_link *prev;
	struct pdq_link *next;
} PDQ_link;

typedef struct pdq_query {
	struct pdq_query *prev;
	struct pdq_query *next;
	struct udp_packet packet;
	SocketAddress address;
	time_t created;
	int next_ns;
} PDQ_query;

typedef struct pdq_reply {
	struct pdq_reply *prev;
	struct pdq_reply *next;
	struct udp_packet packet;
	SocketAddress from;
	socklen_t fromlen;
} PDQ_reply;

typedef struct {
	int length;
	SOCKET *table;
	SOCKET *ready;
	struct udp_packet *packet;
	SocketAddress **address;
} PDQ_udp;

#define PDQ_UDP_SIZE	(2 * sizeof (SOCKET) + sizeof (struct udp_packet) + sizeof (SocketAddress *))

struct pdq {
	SOCKET fd;
	int short_query;
	int round_robin;
	unsigned timeout;
	PDQ_query *pending;
};

struct host {
	char host[DOMAIN_LENGTH+1];
	unsigned char ip[IPV6_BYTE_LENGTH];
};

static int debug = 0;
static int rand_seed;
static int pdq_short_query;
static int pdq_round_robin;
static int pdq_source_port_randomise;
static long servers_length;
static SocketAddress *servers;
static unsigned pdq_max_timeout = PDQ_TIMEOUT_MAX;
static unsigned pdq_initial_timeout = PDQ_TIMEOUT_START * 1000;
static PDQ_rr *root_hints;

struct mapping {
	int code;
	char *name;
};

static struct mapping classMap[] = {
	{ PDQ_CLASS_IN,			"IN" 	},
	{ PDQ_CLASS_CS,			"CS" 	},
	{ PDQ_CLASS_CH,			"CH" 	},
	{ PDQ_CLASS_HS,			"HS" 	},
	{ PDQ_CLASS_ANY,		"ANY"	},
	{ 0, 				NULL 	}
};

static struct mapping typeMap[] = {
	{ PDQ_TYPE_A,			"A"	},
	{ PDQ_TYPE_NS,			"NS"	},
	{ PDQ_TYPE_CNAME,		"CNAME"	},
	{ PDQ_TYPE_SOA,			"SOA"	},
	{ PDQ_TYPE_NULL,		"NULL"	},
	{ PDQ_TYPE_WKS,			"WKS"	},
	{ PDQ_TYPE_PTR,			"PTR"	},
	{ PDQ_TYPE_HINFO,		"HINFO"	},
	{ PDQ_TYPE_MINFO,		"MINFO"	},
	{ PDQ_TYPE_MX,			"MX"	},
	{ PDQ_TYPE_TXT,			"TXT"	},
	{ PDQ_TYPE_AAAA,		"AAAA"	},
	{ PDQ_TYPE_A6,			"A6"	},
	{ PDQ_TYPE_DNAME,		"DNAME"	},
	{ PDQ_TYPE_ANY,			"ANY"	},
	{ 0, 				NULL	}
};

static struct mapping rcodeMap[] = {
	{ PDQ_RCODE_OK,			"OK"			},
	{ PDQ_RCODE_FORMAT,		"FORMAT"		},
	{ PDQ_RCODE_SERVER,		"SERVER"		},
	{ PDQ_RCODE_UNDEFINED,		"UNDEFINED"		},
	{ PDQ_RCODE_NOT_IMPLEMENTED,	"NOT IMPLEMENTED"	},
	{ PDQ_RCODE_REFUSED,		"REFUSED"		},
	{ PDQ_RCODE_ERRNO,		"ERRNO"			},
	{ PDQ_RCODE_TIMEDOUT,		"TIMED OUT"		},
	{ 0, 				NULL			}
};

static struct mapping soaMap[] = {
	{ PDQ_SOA_OK,			"OK"		},
	{ PDQ_SOA_BAD_NAME,		"BAD NAME"	},
	{ PDQ_SOA_UNDEFINED,		"UNDEFINED"	},
	{ PDQ_SOA_MISSING,		"MISSING"	},
	{ PDQ_SOA_BAD_CNAME,		"BAD_CNAME"	},
	{ PDQ_SOA_ROOTED,		"ROOTED"	},
	{ PDQ_SOA_MISMATCH,		"MISMATCH"	},
	{ PDQ_SOA_BAD_NS,		"BAD NS"	},
	{ PDQ_SOA_BAD_CONTACT,		"BAD CONTACT"	},
	{ 0, 				NULL		}
};

static struct mapping sectionMap[] = {
	{ PDQ_SECTION_UNKNOWN,		"UNKNOWN" 	},
	{ PDQ_SECTION_QUESTION,		"QUESTION" 	},
	{ PDQ_SECTION_ANSWER,		"ANSWER" 	},
	{ PDQ_SECTION_AUTHORITY,	"AUTHORITY" 	},
	{ PDQ_SECTION_EXTRA,		"EXTRA"		},
	{ 0, 				NULL 		}
};

static PDQ_type keepMap[] = {
	PDQ_TYPE_A,
	PDQ_TYPE_NS,
	PDQ_TYPE_CNAME,
	PDQ_TYPE_SOA,
	PDQ_TYPE_NULL,
	PDQ_TYPE_WKS,
	PDQ_TYPE_PTR,
	PDQ_TYPE_HINFO,
	PDQ_TYPE_MINFO,
	PDQ_TYPE_MX,
	PDQ_TYPE_TXT,
	PDQ_TYPE_AAAA,
	PDQ_TYPE_A6,
	PDQ_TYPE_DNAME,
	PDQ_TYPE_UNKNOWN
};

static const char usage_dns_max_timeout[] =
  "Maximum timeout in seconds for a DNS query."
;

Option optDnsMaxTimeout	= { "dns-max-timeout",	"45", usage_dns_max_timeout };

static const char usage_dns_round_robin[] =
  "Set true to query NS servers in round robin order. Set false to\n"
"# query all the NS servers in parallel.\n"
"#"
;

Option optDnsRoundRobin	= { "dns-round-robin",	"-", usage_dns_round_robin };

/***********************************************************************
 *** Support
 ***********************************************************************/

static const char *
pdq_code_to_name(struct mapping *map, int code)
{
	for ( ; map->name != NULL; map++) {
		if (code == map->code)
			return map->name;
	}

	return "(unknown)";
}

static int
pdq_name_to_code(struct mapping *map, const char *name)
{
	for ( ; map->name != NULL; map++) {
		if (TextInsensitiveCompare(name, map->name) == 0)
			return map->code;
	}

	return -1;
}

const char *
pdqTypeName(PDQ_type code)
{
	return pdq_code_to_name(typeMap, code);
}

const char *
pdqClassName(PDQ_class code)
{
	return pdq_code_to_name(classMap, code);
}

const char *
pdqRcodeName(PDQ_rcode code)
{
	return pdq_code_to_name(rcodeMap, code);
}

const char *
pdqSoaName(PDQ_valid_soa code)
{
	return pdq_code_to_name(soaMap, code);
}

const char *
pdqSectionName(PDQ_section code)
{
	return pdq_code_to_name(sectionMap, code);
}

PDQ_type
pdqTypeCode(const char *name)
{
	return pdq_name_to_code(typeMap, name);
}

PDQ_class
pdqClassCode(const char *name)
{
	return pdq_name_to_code(classMap, name);
}

const char *
pdqGetAddress(PDQ_rr *rr)
{
	if (rr == NULL)
		return "ERROR";
	if (rr == PDQ_CNAME_TOO_DEEP)
		return "CNAME-TOO-DEEP";
	if (rr == PDQ_CNAME_IS_CIRCULAR)
		return "CNAME-LOOP";
	if (rr->rcode != PDQ_RCODE_OK)
		return pdqRcodeName(rr->rcode);
	return ((PDQ_AAAA *) rr)->address.string.value;
}

static size_t
pdq_dump_packet(unsigned char *packet, size_t packet_length, size_t offset, char *buf, size_t size)
{
	size_t index;
	int count, length, octet;

	length = snprintf(buf, size, "+%.3lu ", (unsigned long) offset);

	for (index = offset, count = 0; count < 16 && index < packet_length; count++, index++)
		length += snprintf(buf+length, size-length, "%.2X ", packet[index]);

	for ( ; count < 16; count++)
		length += snprintf(buf+length, size-length, "__ ");

	for (index = offset, count = 0; count < 16 && index < packet_length; count++, index++) {
		octet = packet[index];
		if (!isprint(octet))
			octet = '.';
		length += snprintf(buf+length, size-length, "%c", octet);
	}

	for ( ; count < 16; count++)
		length += snprintf(buf+length, size-length, ".");

	return index;
}

void
pdqLogPacket(void *_packet, int is_network_order)
{
	int rcode;
	size_t offset;
	char buffer[256];
	struct header pkt_hdr;
	struct udp_packet *packet = _packet;

	pkt_hdr = packet->header;
	if (is_network_order) {
		pkt_hdr.id = ntohs(pkt_hdr.id);
		pkt_hdr.bits = ntohs(pkt_hdr.bits);
		pkt_hdr.qdcount = ntohs(pkt_hdr.qdcount);
		pkt_hdr.ancount = ntohs(pkt_hdr.ancount);
		pkt_hdr.nscount = ntohs(pkt_hdr.nscount);
		pkt_hdr.arcount = ntohs(pkt_hdr.arcount);
	}

	rcode = pkt_hdr.bits & BITS_RCODE;

	syslog(
		LOG_DEBUG, "packet id=%hu %s-order length=%hu bits=0x%.4hx (%s %d %s %s %s %s %d %d=%s) qd=%hu an=%hu ns=%hu ar=%hu",
		pkt_hdr.id, is_network_order ? "net" : "host",
		packet->length, pkt_hdr.bits,
		(pkt_hdr.bits & BITS_QR) ? "AN" : "QR",
		(pkt_hdr.bits >> SHIFT_OP) & 0xF,
		(pkt_hdr.bits & BITS_AA) ? "AA" : "--",
		(pkt_hdr.bits & BITS_TC) ? "TC" : "--",
		(pkt_hdr.bits & BITS_RD) ? "RD" : "--",
		(pkt_hdr.bits & BITS_RA) ? "RA" : "--",
		(pkt_hdr.bits >> SHIFT_Z) & 0x7,
		rcode, pdqRcodeName(rcode),
		pkt_hdr.qdcount, pkt_hdr.ancount,
		pkt_hdr.nscount, pkt_hdr.arcount
	);

	for (offset = 0; offset < packet->length; ) {
		offset = pdq_dump_packet((unsigned char *) &packet->header, packet->length, offset, buffer, sizeof (buffer));
		syslog(LOG_DEBUG, "packet id=%hu %s", pkt_hdr.id, buffer);
	}
}

/***********************************************************************
 *** Resource Records
 ***********************************************************************/

void
pdqDestroy(void *_record)
{
	PDQ_rr *record = _record;

	if (record != NULL) {
		if ((record->type == PDQ_TYPE_TXT || record->type == PDQ_TYPE_NULL)
		&& ((PDQ_TXT *) record)->text.value != NULL
		) {
			free(((PDQ_TXT *) record)->text.value);
		}
		free(record);
	}
}

/**
 * @param _record
 *	Release memory associated with a PDQ_rr pointer previouly
 *	obtained from pdqDup(), pdqFetch(), pdqGet(), or pdqListClone().
 */
void
pdqListFree(void *_record)
{
	PDQ_rr *next, *record;

	for (record = _record; record != NULL; record = next) {
		next = record->next;
		pdqDestroy(record);
	}
}

/**
 * @param type
 *	A DNS PDQ_TYPE_ code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 *
 * @return
 *	Size of the type's structure.
 */
size_t
pdqSizeOfType(PDQ_type type)
{
	switch (type) {
	case PDQ_TYPE_A:
	case PDQ_TYPE_AAAA:
		return sizeof (PDQ_AAAA);

	case PDQ_TYPE_MX:
		return sizeof (PDQ_MX);

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		return sizeof (PDQ_NS);

	case PDQ_TYPE_TXT:
	case PDQ_TYPE_NULL:
		return sizeof (PDQ_TXT);

	case PDQ_TYPE_SOA:
		return sizeof (PDQ_SOA);

	case PDQ_TYPE_HINFO:
	case PDQ_TYPE_MINFO:
		return sizeof (PDQ_HINFO);

	default:
		syslog(LOG_ERR, "unsupported DNS RR type=%d", type);
		errno = EINVAL;
	}

	return 0;
}

/**
 * @param record
 *	A pointer to a previous allocated PDQ_rr record.
 *
 * @return
 *	Size of the type's structure.
 */
size_t
pdqSizeOf(PDQ_rr *record)
{
	return pdqSizeOfType(record->type);
}

/**
 * @param type
 *	A DNS PDQ_TYPE_ code based on RFC 1035, 1886, 3596, and 2874 defined
 *	types.
 *
 * @return
 *	A pointer to a PDQ_rr record of the given type.
 */
PDQ_rr *
pdqCreate(PDQ_type type)
{
	size_t size;
	PDQ_rr *record;

	if ((size = pdqSizeOfType(type)) == 0)
		return NULL;

	if ((record = calloc(1, size)) != NULL) {
		record->created = time(NULL);
		record->type = type;
	}

	return record;
}

void
pdqSetName(PDQ_name *name, const char *string)
{
	name->string.length = TextCopy(
		name->string.value, sizeof (name->string.value),
		string
	);

	/* Make sure the name has the root label. */
	if (name->string.length == 0 || name->string.value[name->string.length-1] != '.') {
		if (name->string.length < sizeof (name->string.value)-1) {
			name->string.value[name->string.length++] = '.';
			name->string.value[name->string.length  ] = '\0';
		}
	}
}

/**
 * @param a
 *	A pointer to a PDQ_rr structure.
 *
 * @param b
 *	A pointer to a PDQ_rr structure.
 *
 * @return
 *	True if the two records are "equal". Note that equality here
 *	does not mean a byte for byte match, but specific member
 *	fields match.
 */
int
pdqEqual(PDQ_rr *a, PDQ_rr *b)
{
	if (a->type != b->type)
		return 0;

	if (a->class != b->class)
		return 0;

	if (TextInsensitiveCompare(a->name.string.value, b->name.string.value) != 0)
		return 0;

	switch (a->type) {
	case PDQ_TYPE_A:
	case PDQ_TYPE_AAAA:
		/* This assumes that pdqCreate allocates an initially zeroed RR . */
#ifdef PDQ_EQUAL_DETAILED
		if (memcmp(&((PDQ_AAAA *) a)->address, &((PDQ_AAAA *) b)->address, sizeof (((PDQ_AAAA *) a)->address)) != 0)
			return 0;
#else
		if (memcmp(&((PDQ_AAAA *) a)->address.ip, &((PDQ_AAAA *) b)->address.ip, sizeof (((PDQ_AAAA *) a)->address.ip)) != 0)
			return 0;
#endif
		break;

	case PDQ_TYPE_SOA:
		if (((PDQ_SOA *) a)->serial != ((PDQ_SOA *) b)->serial)
			return 0;
#ifdef PDQ_EQUAL_DETAILED
		if (TextInsensitiveCompare(((PDQ_SOA *) a)->mname.string.value, ((PDQ_SOA *) b)->mname.string.value) != 0)
			return 0;
		if (TextInsensitiveCompare(((PDQ_SOA *) a)->rname.string.value, ((PDQ_SOA *) b)->rname.string.value) != 0)
			return 0;
#endif
		break;

	case PDQ_TYPE_MX:
		if (((PDQ_MX *) a)->preference != ((PDQ_MX *) b)->preference)
			return 0;
		/*@fallthrough@*/

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		if (memcmp(&((PDQ_NS *) a)->host, &((PDQ_NS *) b)->host, sizeof (((PDQ_NS *) a)->host)) != 0)
			return 0;
		break;

	case PDQ_TYPE_TXT:
	case PDQ_TYPE_NULL:
		if (((PDQ_TXT *) a)->text.length != ((PDQ_TXT *) b)->text.length)
			return 0;
		if (memcmp(((PDQ_TXT *) a)->text.value, ((PDQ_TXT *) b)->text.value, ((PDQ_TXT *) a)->text.length) != 0)
			return 0;
		break;

	case PDQ_TYPE_HINFO:
	case PDQ_TYPE_MINFO:
		if (memcmp(&((PDQ_HINFO *) a)->cpu, &((PDQ_HINFO *) b)->cpu, sizeof (((PDQ_HINFO *) a)->cpu)) != 0)
			return 0;
		if (memcmp(&((PDQ_HINFO *) a)->os, &((PDQ_HINFO *) b)->os, sizeof (((PDQ_HINFO *) a)->os)) != 0)
			return 0;
		break;
	}

	return 1;
}

static PDQ_rr *
pdq_create_rr(PDQ_class class, PDQ_type type, const char *host)
{
	PDQ_rr *record;

	if ((record = pdqCreate(type)) == NULL) {
		syslog(LOG_ERR, "%s(%d) name=%s: %s (%d)", __FILE__, __LINE__, host, strerror(errno), errno);
	} else {
		pdqSetName(&record->name, host);
		record->rcode = PDQ_RCODE_OK;
		record->class = class;
		record->next = NULL;
		record->ttl = 0;
	}

	return record;
}

/**
 * @param record
 *	A pointer to a previous allocated PDQ_rr structure to be
 *	duplicated.
 *
 * @return
 *	A copy of the given PDQ_rr structure or NULL on error. It is
 *	the caller's responsibility to pdqListFree() this record when done.
 *	Note that pointer returned is a single record. To duplicate an
 *	entire PDQ_rr list, use pdqListClone().
 */
PDQ_rr *
pdqDup(PDQ_rr *orig)
{
	size_t size;
	PDQ_rr *copy;

	if (orig == NULL)
		return NULL;

	if ((size = pdqSizeOfType(orig->type)) == 0) {
		syslog(LOG_ERR, "%s(%d) name=%s: %s (%d)", __FILE__, __LINE__, orig->name.string.value, strerror(errno), errno);
		return NULL;
	}

	if ((copy = malloc(size)) != NULL) {
		memcpy(copy, orig, size);

		if (orig->type == PDQ_TYPE_TXT || orig->type == PDQ_TYPE_NULL) {
			((PDQ_TXT *) copy)->text.value = malloc(((PDQ_TXT *) orig)->text.length);
			if (((PDQ_TXT *) copy)->text.value == NULL) {
				free(copy);
				return NULL;
			}

			memcpy(
				((PDQ_TXT *) copy)->text.value,
				((PDQ_TXT *) orig)->text.value,
				((PDQ_TXT *) orig)->text.length
			);
		}
	}

	return copy;
}

/**
 * @param record
 *	A pointer to a previous allocated PDQ_rr list to be cloned.
 *
 * @return
 *	A clone of the given PDQ_rr list or NULL on error. It is the
 *	caller's responsibility to pdqListFree() this record when done.
 */
PDQ_rr *
pdqListClone(PDQ_rr *orig)
{
	PDQ_rr *copy;

	if ((copy = pdqDup(orig)) != NULL && orig->next != NULL) {
		copy->next = pdqListClone(orig->next);
		if (copy->next == NULL)
			pdqListFree(copy);
	}

	return copy;
}

/**
 * @param record
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	Number of entries in the list.
 */
unsigned
pdqListLength(PDQ_rr *record)
{
	unsigned count;

	for (count = 0; record != NULL; record = record->next)
		count++;

	return count;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param record
 *	A pointer to a PDQ_rr record.
 *
 * @return
 *	True if there is already a duplicate of the record present.
 */
int
pdqListIsMember(PDQ_rr *list, PDQ_rr *record)
{
	for ( ; list != NULL; list = list->next) {
		if (pdqEqual(list, record))
			return 1;
	}

	return 0;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	The updated head of the list or NULL if the list is empty.
 *	The list will only contain unique records.
 */
PDQ_rr *
pdqListPruneDup(PDQ_rr *list)
{
	PDQ_rr **prev, *next, *r1, *r2;

	for (r1 = list; r1 != NULL; r1 = r1->next) {
		prev = &r1->next;
		for (r2 = r1->next; r2 != NULL; r2 = next) {
			next = r2->next;

			if (pdqEqual(r1, r2)) {
				*prev = r2->next;
				pdqDestroy(r2);
				continue;
			}

			prev = &r2->next;
		}
	}

	return list;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list containing A or AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @param must_have_ip
 *	When true, keep only A/AAAA records that return PDQ_RCODE_OK
 *	ie. they have an IP address "at this point in time". Otherwise
 *	keep A/AAAA records that returned PDQ_RCODE_OK or PDQ_RCODE_SERVER
 *	(temporary DNS failure).
 *
 * @return
 *	The updated head of the list or NULL if the list is empty.
 *	The list will only contain A/AAAA records that successfully
 *	returned an IP address. Other record types remain untouched.
 */
PDQ_rr *
pdqListPrune5A(PDQ_rr *list, is_ip_t is_ip_mask, int must_have_ip)
{
	PDQ_rr **prev, *rr, *next;

	/* Remove impossible to reach A/AAAA records. */
	prev = &list;
	for (rr = list; rr != NULL; rr = next) {
		next = rr->next;

		if ((rr->type == PDQ_TYPE_A || rr->type == PDQ_TYPE_AAAA)
		&& ((rr->rcode != PDQ_RCODE_OK && (must_have_ip || rr->rcode != PDQ_RCODE_SERVER))
		 || (rr->rcode == PDQ_RCODE_OK && isReservedIPv6(((PDQ_AAAA *) rr)->address.ip.value, is_ip_mask)))) {
			*prev = rr->next;
			pdqDestroy(rr);
			continue;
		}

		prev = &rr->next;
	}

	return list;
}

PDQ_keep
pdqKeepMask(PDQ_type type)
{
	PDQ_keep mask;
	PDQ_type *map;

	for (mask = 1, map = keepMap; *map != PDQ_TYPE_UNKNOWN; map++, mask <<= 1) {
		if (*map == type)
			return mask;
	}

	return 0;
}

int
pdqKeepType(PDQ_keep mask, PDQ_type type)
{
	int bit;
	PDQ_type *map;

	for (bit = 1, map = keepMap; *map != PDQ_TYPE_UNKNOWN; map++, bit <<= 1) {
		if ((bit & mask) && *map == type)
			return 1;
	}

	return 0;
}

PDQ_rr *
pdqListKeepType(PDQ_rr *list, PDQ_keep mask)
{
	PDQ_rr **prev, *rr, *next;

	prev = &list;
	for (rr = list; rr != NULL; rr = next) {
		next = rr->next;

		if (!pdqKeepType(mask, rr->type)) {
			*prev = rr->next;
			pdqDestroy(rr);
			continue;
		}

		prev = &rr->next;
	}

	return list;
}


/**
 * @param list
 *	A pointer to a PDQ_rr list containing MX, NS, SOA, A, or AAAA records.
 *
 * @return
 *	The updated head of the list or NULL if the list is empty. This list
 *	will contain valid MX/NS/SOA records with matching A/AAAA records.
 */
PDQ_rr *
pdqListPruneMatch(PDQ_rr *list)
{
	PDQ_rr **prev, *rr, *next, *ar;

	/* Remove MX/NS/SOA records with no matching A/AAAA records. Note
	 * that pdqGet() returns the implicit MX 0 record as a convenience.
	 */
	prev = &list;
	for (rr = list; rr != NULL; rr = next) {
		next = rr->next;

		/* Discard records we can't use (NXDOMAIN, CNAME, DNAME). */
		if (rr->type == PDQ_TYPE_CNAME || rr->type == PDQ_TYPE_DNAME) {
			*prev = rr->next;
			pdqDestroy(rr);
			continue;
		}

		/* Discard MX records without matching A/AAAA records. */
		if (rr->type == PDQ_TYPE_MX || rr->type == PDQ_TYPE_NS || rr->type == PDQ_TYPE_SOA) {
			ar = pdqListFindName(list, rr->class, PDQ_TYPE_5A, ((PDQ_MX *) rr)->host.string.value);
			if (PDQ_RR_IS_NOT_VALID(ar)) {
				*prev = rr->next;
				pdqDestroy(rr);
				continue;
			}
		}

		prev = &rr->next;
	}

	return list;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list containing MX, NS, SOA, A, or AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @return
 *	The updated head of the list or NULL if the list is empty. This list
 *	will only contain valid MX/NS/SOA records with matching A/AAAA which
 *	themselves have an IP address "at this time" (ie. PDQ_RCODE_SERVER
 *	results are discarded).
 */
PDQ_rr *
pdqListPrune(PDQ_rr *list, is_ip_t is_ip_mask)
{
	list = pdqListPruneDup(list);
	list = pdqListPrune5A(list, is_ip_mask, 1);
	return pdqListPruneMatch(list);
}

/**
 * @param record
 *	A pointer to a PDQ_rr list.
 *
 * @param index
 *	The index of the record to fetch.
 *
 * @return
 *	A pointer to the Nth record or NULL if the Nth record does not
 *	exist.
 */
PDQ_rr *
pdqListGet(PDQ_rr *record, unsigned index)
{
	for ( ; record != NULL && 0 < index; record = record->next, index--)
		;

	return record;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	A pointer to the last record in the list.
 */
PDQ_rr *
pdqListLast(PDQ_rr *list)
{
	if (list != NULL) {
		for ( ; list->next != NULL; list = list->next)
			;
	}

	return list;
}

/**
 * @param a
 *	A pointer to a PDQ_rr list.
 *
 * @param b
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	A pointer to a PDQ_rr list where list ``b'' has been linked
 *	to the end of list ``a''.
 */
PDQ_rr *
pdqListAppend(PDQ_rr *a, PDQ_rr *b)
{
	if (a == NULL)
		return b;

	if (b != NULL)
		pdqListLast(a)->next = b;

	return a;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list to reverse.
 *
 * @return
 *	A pointer to the new head of the PDQ_rr list.
 */
PDQ_rr *
pdqListReverse(PDQ_rr *list)
{
	PDQ_rr *tmp;
	PDQ_rr *prev = NULL;

	while (list != NULL) {
		tmp = list->next;
		list->next = prev;
		prev = list;
		list = tmp;
	}

	return prev;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param rcode
 *	A PDQ_RCODE_ code of the DNS record return code to find.
 *
 * @param name
 *	A record name to find. NULL for any. CNAME redirection
 *	is NOT followed.
 *
 * @return
 *	A pointer to the first PDQ_rr record found, or NULL if not found.
 */
PDQ_rr *
pdqListFind(PDQ_rr *list, PDQ_class class, PDQ_type type, PDQ_rcode rcode, const char *name)
{
	for ( ; list != NULL; list = list->next) {
		if (! (list->type == type
		||  type == PDQ_TYPE_ANY
		|| (type == PDQ_TYPE_5A && (list->type == PDQ_TYPE_A || list->type == PDQ_TYPE_AAAA))) )
			continue;

		if (class != PDQ_CLASS_ANY && list->class != class)
			continue;

		if (rcode != PDQ_RCODE_ANY && list->rcode != rcode)
			continue;

		if (name != NULL && TextInsensitiveCompare(list->name.string.value, name) != 0)
			continue;

		break;
	}

	return list;
}

#ifdef NOT_SURE
int
pdqListAllRcode(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *name, PDQ_rcode rcode)
{
	int count, match;

	for (count = match = 0; list != NULL; list = list->next) {
		if (! (list->type == type
		||  type == PDQ_TYPE_ANY
		|| (type == PDQ_TYPE_5A && (list->type == PDQ_TYPE_A || list->type == PDQ_TYPE_AAAA))) )
			continue;

		if (name != NULL && TextInsensitiveCompare(list->name.string.value, name) != 0)
			continue;

		if (class != PDQ_CLASS_ANY && list->class != class)
			continue;

		count++;

		if (rcode != PDQ_RCODE_ANY && list->rcode != rcode)
			continue;

		match++;
	}

	return count == match;
}

pdqListHasAll(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *name, PDQ_rcode rcode)
{
	unsigned count, class_count, type_count, name_count, rcode_count;

	for (count = match = 0; list != NULL; list = list->next) {
		count++;

		if (! (list->type == type
		||  type == PDQ_TYPE_ANY
		|| (type == PDQ_TYPE_5A && (list->type == PDQ_TYPE_A || list->type == PDQ_TYPE_AAAA))) )
			continue;

		type_count++;

		if (class != PDQ_CLASS_ANY && list->class != class)
			continue;

		class_count++;

		if (name != NULL && TextInsensitiveCompare(list->name.string.value, name) != 0)
			continue;

		name_count++;

		if (rcode != PDQ_RCODE_ANY && list->rcode != rcode)
			continue;

		rcode_count++;
	}

	if (class != PDQ_CLASS_ANY && class_count >= rcode_count)
		return 0;

	if (type != PDQ_TYPE_ANY && type_count != rcode_count)
		return 0;

	if (name != NULL && name_count >= rcode_count)
		return 0;

	return 1;
}
#endif

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find. Specify the
 *	special type PDQ_TYPE_5A to find either A or AAAA records.
 *
 * @param name
 *	A record name to find. CNAME redirection is followed.
 *
 * @return
 *	NULL if not found, PDQ_CNAME_TOO_DEEP, or PDQ_CNAME_IS_CIRCULAR.
 *	Otherwise a pointer to PDQ_rr A or AAAA record.
 */
PDQ_rr *
pdqListFindName(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *name)
{
	long length;
	PDQ_rr *rr, *next;
	int i, cnames_length = 0;
	const char *cnames[MAX_CNAME_DEPTH], *start;

	for (start = name, rr = list; rr != NULL; rr = next) {
		next = rr->next;

		length = TextInsensitiveStartsWith(rr->name.string.value, name);

		/* Match the name with or without a trailing root label. */
		if (0 < length && (rr->name.string.value[length] == '\0'
		|| (rr->name.string.value[length] == '.' && rr->name.string.value[length+1] == '\0'))) {
			if (rr->class != class && class != PDQ_CLASS_ANY)
				continue;

			if (rr->type == type
			||  type == PDQ_TYPE_ANY
			|| (type == PDQ_TYPE_5A && (rr->type == PDQ_TYPE_A || rr->type == PDQ_TYPE_AAAA)))
				break;

			if (rr->type == PDQ_TYPE_CNAME || rr->type == PDQ_TYPE_DNAME) {
				if (MAX_CNAME_DEPTH <= cnames_length) {
					if (0 < debug) {
						syslog(
							LOG_DEBUG, "%s %s %s is too deep!",
							start, pdqClassName(class), pdqTypeName(type)
						);
					}
					return PDQ_CNAME_TOO_DEEP;
				}

				cnames[cnames_length++] = name;
				name = ((PDQ_CNAME *) rr)->host.string.value;

				/* Have we seen this host name before? */
				for (i = 0; i < cnames_length; i++) {
					if (TextInsensitiveCompare(name, cnames[i]) == 0) {
						if (0 < debug) {
							syslog(
								LOG_DEBUG, "%s %s %s is an infinite loop!",
								start, pdqClassName(class), pdqTypeName(type)
							);
						}
						return PDQ_CNAME_IS_CIRCULAR;
					}
				}

				/* Restart the search for the next name
				 * in case the records are not in order.
				 */
				next = list;
				continue;
			}
		}
	}

	return rr;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_A,  PDQ_TYPE_AAAA, or PDQ_TYPE_5A code of the DNS
 *	record type to find. PDQ_TYPE_5A looks for bother A and AAAA
 *	records.
 *
 * @param ipv6
 *	A record's IP address value to find.
 *
 * @return
 *	A pointer to the first PDQ_rr record found or NULL if not found.
 *	Only records of type PDQ_A or PDQ_AAAA are returned.
 */
PDQ_rr *
pdqListFindIP(PDQ_rr *list, PDQ_class class, PDQ_type type, const unsigned char ipv6[IPV6_BYTE_LENGTH])
{
	/* We can only find IP in A/AAAA records. */
	if (type != PDQ_TYPE_A && type != PDQ_TYPE_AAAA && type != PDQ_TYPE_5A)
		return NULL;

	for ( ; list != NULL; list = list->next) {
		if (! (list->type == type
		|| (type == PDQ_TYPE_5A && (list->type == PDQ_TYPE_A || list->type == PDQ_TYPE_AAAA))) )
			continue;

		if (class != PDQ_CLASS_ANY && list->class != class)
			continue;

		if (list->rcode != PDQ_RCODE_OK)
			continue;

		if (memcmp(ipv6, ((PDQ_AAAA *) list)->address.ip.value, sizeof (((PDQ_AAAA *) list)->address.ip.value)) == 0)
			break;
	}

	return list;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_A,  PDQ_TYPE_AAAA, or PDQ_TYPE_5A code of the DNS
 *	record type to find. PDQ_TYPE_5A looks for bother A and AAAA
 *	records.
 *
 * @param ip
 *	A C string of the record's IP address value to find.
 *
 * @return
 *	A pointer to the first PDQ_rr record found or NULL if not found.
 *	Only records of type PDQ_A or PDQ_AAAA are returned.
 */
PDQ_rr *
pdqListFindAddress(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *ip)
{
	/* We can only find IP in A/AAAA records. */
	if (type != PDQ_TYPE_A && type != PDQ_TYPE_AAAA && type != PDQ_TYPE_5A)
		return NULL;

	for ( ; list != NULL; list = list->next) {
		if (! (list->type == type
		|| (type == PDQ_TYPE_5A && (list->type == PDQ_TYPE_A || list->type == PDQ_TYPE_AAAA))) )
			continue;

		if (class != PDQ_CLASS_ANY && list->class != class)
			continue;

		if (list->rcode != PDQ_RCODE_OK)
			continue;

		if (TextInsensitiveCompare(ip, ((PDQ_AAAA *) list)->address.string.value) == 0)
			break;
	}

	return list;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param host
 *	A record's host name value to find.
 *
 * @return
 *	A pointer to the first PDQ_rr record found or NULL if not found.
 *	Only records of type PDQ_CNAME, PDQ_TYPE_DNAME, PDQ_MX, PDQ_NS,
 *	PDQ_PTR, or PDQ_SOA are returned.
 */
PDQ_rr *
pdqListFindHost(PDQ_rr *list, PDQ_class class, PDQ_type type, const char *host)
{
	switch (type) {
	/* Only these types have a host name value field. */
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
	case PDQ_TYPE_MX:
	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_SOA:
		break;

	/* Ignore the rest, since trying to find a host name of
	 * the other types has no meaning, and could potentially
	 * cause a bus error or segmentation fault if we tried to
	 * compare the wrong structure type.
	 */
	default:
		return NULL;
	}

	for ( ; list != NULL; list = list->next) {
		if (list->rcode != PDQ_RCODE_OK)
			continue;

		if (type != PDQ_TYPE_ANY && list->type != type)
			continue;

		if (class != PDQ_CLASS_ANY && list->class != class)
			continue;

		if (TextInsensitiveCompare(((PDQ_CNAME *) list)->host.string.value, host) == 0)
			break;
	}

	return list;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param record
 *	A pointer to a record in the PDQ_rr list to be removed.
 *	Note that the record is not freed.
 *
 * @return
 *	A pointer to the new head of the PDQ_rr list.
 */
PDQ_rr *
pdqListRemove(PDQ_rr *list, PDQ_rr *record)
{
	PDQ_rr **prev, *rr, *next;

	for (prev = &list, rr = list; rr != NULL; prev = &rr->next, rr = next) {
		next = rr->next;
		if (rr == record) {
			record->next = NULL;
			*prev = next;
			break;
		}
	}

	return list;
}

#define PDQ_LOG_FMT		"%s %lu %s %s "
#define PDQ_LOG_ARG(l)		(l)->name.string.value, (unsigned long) (l)->ttl,\
				pdqClassName((l)->class), pdqTypeName((l)->type)
#define PDQ_LOG_FMT_END		" ; %s"
#define PDQ_LOG_ARG_END(l)	pdqSectionName((l)->section)

void
pdqDump(FILE *fp, PDQ_rr *record)
{
	if (fp == NULL || record == NULL)
		return;

	(void) fprintf(fp, PDQ_LOG_FMT, PDQ_LOG_ARG(record));

	switch (record->type) {
	case PDQ_TYPE_A:
		(void) fprintf(fp, "%s", ((PDQ_A *) record)->address.string.value);
		break;

	case PDQ_TYPE_AAAA:
		(void) fprintf(fp, "%s", ((PDQ_AAAA *) record)->address.string.value);
		break;

	case PDQ_TYPE_SOA:
		(void) fprintf(
			fp, "%s %s (%lu %ld %ld %ld %lu)",
			((PDQ_SOA *) record)->mname.string.value,
			((PDQ_SOA *) record)->rname.string.value,
			(unsigned long)((PDQ_SOA *) record)->serial,
			(long)((PDQ_SOA *) record)->refresh,
			(long)((PDQ_SOA *) record)->retry,
			(long)((PDQ_SOA *) record)->expire,
			(unsigned long)((PDQ_SOA *) record)->minimum
		);
		break;

	case PDQ_TYPE_MX:
		(void) fprintf(fp, "%d ", ((PDQ_MX *) record)->preference);
		/*@fallthrough@*/

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		(void) fprintf(fp, "%s", ((PDQ_NS *) record)->host.string.value);
		break;

	case PDQ_TYPE_TXT:
		(void) fprintf(fp, "\"%s\"", TextEmpty((char *) ((PDQ_TXT *) record)->text.value));
		break;

	case PDQ_TYPE_NULL:
		(void) fprintf(fp, "%lu bytes", ((PDQ_NULL *) record)->text.length);
		break;

	case PDQ_TYPE_HINFO:
	case PDQ_TYPE_MINFO:
		(void) fprintf(
			fp, "\"%s\" \"%s\"",
			((PDQ_HINFO *) record)->cpu.string.value,
			((PDQ_HINFO *) record)->os.string.value
		);
		break;
	}

	(void) fprintf(fp, PDQ_LOG_FMT_END, PDQ_LOG_ARG_END(record));

	if (record->rcode != PDQ_RCODE_OK)
		(void) fprintf(fp, " rcode=%s", pdqRcodeName(record->rcode));

	(void) fputc('\n', fp);
}

void
pdqListDump(FILE *fp, PDQ_rr *list)
{
	for ( ; list != NULL; list = list->next) {
		pdqDump(fp, list);
	}
}

void
pdqLog(PDQ_rr *record)
{
	if (record->rcode != PDQ_RCODE_OK) {
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "%s", PDQ_LOG_ARG(record),
			pdqRcodeName(record->rcode)
		);
		return;
	}

	switch (record->type) {
	case PDQ_TYPE_A:
	case PDQ_TYPE_AAAA:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "%s" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_A *) record)->address.string.value,
			PDQ_LOG_ARG_END(record)
		);
		break;

	case PDQ_TYPE_SOA:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "%s %s (%lu %ld %ld %ld %lu)" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_SOA *) record)->mname.string.value,
			((PDQ_SOA *) record)->rname.string.value,
			(unsigned long)((PDQ_SOA *) record)->serial,
			(long)((PDQ_SOA *) record)->refresh,
			(long)((PDQ_SOA *) record)->retry,
			(long)((PDQ_SOA *) record)->expire,
			(unsigned long)((PDQ_SOA *) record)->minimum,
			PDQ_LOG_ARG_END(record)
		);
		break;

	case PDQ_TYPE_MX:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "%d %s" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_MX *) record)->preference,
			((PDQ_MX *) record)->host.string.value,
			PDQ_LOG_ARG_END(record)
		);
		break;

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "%s" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_NS *) record)->host.string.value,
			PDQ_LOG_ARG_END(record)
		);
		break;

	case PDQ_TYPE_TXT:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "\"%s\"" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_TXT *) record)->text.value,
			PDQ_LOG_ARG_END(record)
		);
		break;

	case PDQ_TYPE_NULL:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "%lu bytes" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_NULL *) record)->text.length,
			PDQ_LOG_ARG_END(record)
		);
		break;

	case PDQ_TYPE_HINFO:
	case PDQ_TYPE_MINFO:
		syslog(
			LOG_DEBUG, PDQ_LOG_FMT "\"%s\" \"%s\"" PDQ_LOG_FMT_END,
			PDQ_LOG_ARG(record),
			((PDQ_HINFO *) record)->cpu.string.value,
			((PDQ_HINFO *) record)->os.string.value,
			PDQ_LOG_ARG_END(record)
		);
		break;
	}
}

void
pdqListLog(PDQ_rr *list)
{
	for ( ; list != NULL; list = list->next) {
		pdqLog(list);
	}
}

size_t
pdqStringSize(PDQ_rr *record)
{
	size_t length;

	if (record == NULL)
		return 0;

	/* "%s %lu %s %s " */
	length = record->name.string.length + 1;
	length += 10 + 1 + 2 + 1 + 5 + 1;

	switch (record->type) {
	case PDQ_TYPE_A:
	case PDQ_TYPE_AAAA:
		/* "%s" */
		length += ((PDQ_A *) record)->address.string.length + 1;
		break;

	case PDQ_TYPE_SOA:
		/* "%s %s (%lu %ld %ld %ld %lu)" */
		length += ((PDQ_SOA *) record)->mname.string.length + 1;
		length += ((PDQ_SOA *) record)->rname.string.length + 1;
		length += 1 + 10 + 1 + 11 + 1 + 11 + 1 + 11 + 1 + 10 + 1 + 1;
		break;

	case PDQ_TYPE_MX:
		/* "%d %s" */
		length += 5 + 1;
		length += ((PDQ_MX *) record)->host.string.length + 1;
		break;

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		/* "%s" */
		length += ((PDQ_NS *) record)->host.string.length + 1;
		break;

	case PDQ_TYPE_TXT:
		/* "%s" */
		length += 1 + ((PDQ_TXT *) record)->text.length + 1 + 1;
		break;

	case PDQ_TYPE_NULL:
		/* "%02x..." */
		length += ((PDQ_NULL *) record)->text.length * 2 + 1;
		break;

	case PDQ_TYPE_HINFO:
	case PDQ_TYPE_MINFO:
		/* "\"%s\" \"%s\"" */
		length += 1 + ((PDQ_HINFO *) record)->cpu.string.length + 1 + 1;
		length += 1 + ((PDQ_HINFO *) record)->os.string.length + 1 + 1;
		break;
	}

	return length;
}

int
pdqStringFormat(char *buffer, size_t size, PDQ_rr * record)
{
	int length;

	length = snprintf(buffer, size, PDQ_LOG_FMT, PDQ_LOG_ARG(record));

	switch (record->type) {
	case PDQ_TYPE_A:
	case PDQ_TYPE_AAAA:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0, "%s",
			((PDQ_A *) record)->address.string.value
		);
		break;

	case PDQ_TYPE_SOA:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0,
			"%s %s (%lu %ld %ld %ld %lu)",
			((PDQ_SOA *) record)->mname.string.value,
			((PDQ_SOA *) record)->rname.string.value,
			(unsigned long)((PDQ_SOA *) record)->serial,
			(long)((PDQ_SOA *) record)->refresh,
			(long)((PDQ_SOA *) record)->retry,
			(long)((PDQ_SOA *) record)->expire,
			(unsigned long)((PDQ_SOA *) record)->minimum
		);
		break;

	case PDQ_TYPE_MX:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0, "%d ",
			((PDQ_MX *) record)->preference
		);
		/*@fallthrough@*/

	case PDQ_TYPE_NS:
	case PDQ_TYPE_PTR:
	case PDQ_TYPE_CNAME:
	case PDQ_TYPE_DNAME:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0, "%s",
			((PDQ_NS *) record)->host.string.value
		);
		break;

	case PDQ_TYPE_TXT:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0, "\"%s\"",
			TextEmpty((char *) ((PDQ_TXT *) record)->text.value)
		);
		break;

	case PDQ_TYPE_NULL:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0, "%lu bytes",
			((PDQ_NULL *) record)->text.length
		);
		break;

	case PDQ_TYPE_HINFO:
	case PDQ_TYPE_MINFO:
		length += snprintf(
			buffer+length, 0 < size ? size-length : 0, "\"%s\" \"%s\"",
			((PDQ_HINFO *) record)->cpu.string.value,
			((PDQ_HINFO *) record)->os.string.value
		);
		break;
	}

	return length;
}

char *
pdqString(PDQ_rr *record)
{
	int length;
	char *buffer;

	if (record == NULL)
		return NULL;

	length = pdqStringFormat(NULL, 0, record);
	if ((buffer = malloc(length+1)) != NULL)
		pdqStringFormat(buffer, length+1, record);

	return buffer;
}

/***********************************************************************
 *** Response Record Name Routines
 ***********************************************************************/

#ifdef NOT_USED
/*
 * Return the string length of the resource record name refered to
 * by ptr within the message. The length includes for name label
 * (dot) delimiters and the final root label.
 *
 * An error here would tend to indicate corrupt or hacked packets.
 */
static long
pdq_name_length(struct udp_packet *packet, unsigned char *ptr)
{
	long length;
	unsigned short offset;
	unsigned char *packet_end;

	packet_end = (unsigned char *) &packet->header + packet->length;

	if (ptr < (unsigned char *) &packet->header) {
		syslog(LOG_ERR, "pdq_name_length() below bounds!!!");
		return -1;
	}

	for (length = 0; ptr < packet_end && *ptr != 0; ) {
		if ((*ptr & 0xc0) == 0xc0) {
			offset = NET_GET_SHORT(ptr) & 0x3fff;
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
		syslog(LOG_ERR, "pdq_name_length() out of bounds!!!");
		return -1;
	}

	if (DOMAIN_LENGTH < length) {
		syslog(LOG_ERR, "pdq_name_length() domain name too long!!!");
		return -1;
	}

	return length;
}
#endif

/*
 * Copy the string of the resource record name refered to by ptr
 * within the message into a buffer of size bytes. The name
 * labels are separated by dots and the final root label is added.
 * The terminating null character is appended.
 *
 * An error here would indicate process memory or stack corruption.
 */
static int
pdq_name_copy(struct udp_packet *packet, unsigned char *ptr, unsigned char *buf, long size)
{
	long remaining;
	unsigned short offset;
	unsigned char *packet_end, *buf0;

	packet_end = (unsigned char *) &packet->header + packet->length;

	if (ptr < (unsigned char *) &packet->header) {
		syslog(LOG_ERR, "pdq_name_copy() below bounds!!!");
		return -1;
	}

	buf0 = buf;
	remaining = size;
	while (ptr < packet_end && *ptr != 0) {
		if ((*ptr & 0xc0) == 0xc0) {
			offset = NET_GET_SHORT(ptr) & 0x3fff;
			ptr = (unsigned char *) &packet->header + offset;
			continue;
		}

		/* Do we still have room in the buffer for the next label. */
		if (remaining <= *ptr) {
			syslog(LOG_ERR, "pdq_name_copy() buffer overflow!!!");
			return -1;
		}

		(void) strncpy((char *) buf, (char *)(ptr+1), (size_t) *ptr);
		buf += *ptr;
		*buf++ = '.';

		remaining -= *ptr + 1;
		ptr += *ptr + 1;
	}

	if (packet_end <= ptr) {
		*buf = '\0';
		syslog(LOG_ERR, "pdq_name_copy() out of bounds!!! start of buf=\"%40s\"", buf0);
		return -1;
	}

	/* Special case where the root label is the only segment (dot). */
	if (remaining == size && *ptr == 0) {
		*buf++ = '.';
		remaining--;
	}

	if (remaining < 1) {
		syslog(LOG_ERR, "pdq_name_copy() buffer underflow!!!");
		return -1;
	}

	*buf = '\0';

	return (int) (size - remaining);
}

/*
 * Return a pointer to the next field within a resource record that
 * follows after the name field refered to by ptr.
 */
static unsigned char *
pdq_name_skip(struct udp_packet *packet, unsigned char *ptr)
{
	unsigned char *packet_start = ptr;
	unsigned char *packet_end = (unsigned char *) &packet->header + packet->length;

	if (packet_end < ptr) {
		syslog(
			LOG_ERR, "pdq_name_skip() id=%hu pkt=%lx ptr=%lx out of bounds (1)!!!",
			packet->header.id, (long) packet, (long) packet_start
		);
		pdqLogPacket(packet, 0);
		errno = EFAULT;
		return NULL;
	}

	for ( ; ptr < packet_end && *ptr != 0; ptr += *ptr + 1) {
		if ((*ptr & 0xc0) == 0xc0) {
			/* Skip 0xC0 byte and next one. */
			ptr++;
			break;
		}
	}

	/* Move past root label length or 2nd half of compression offset. */
	ptr++;

	if (packet_end < ptr) {
		syslog(
			LOG_ERR, "pdq_name_skip() id=%hu pkt=%lx ptr=%lx out of bounds (2)!!!",
			packet->header.id, (long) packet, (long) packet_start
		);
		pdqLogPacket(packet, 0);
		errno = EFAULT;
		return NULL;
	}

	return ptr;
}

/* The TXT resource consists of one or more binary strings,
 * where each string is prefixed by a length octet followed
 * by the string content upto rdlength bytes.
 */
static int
pdq_txt_create(PDQ_TXT *txt, unsigned char *rdata, unsigned rdlength)
{
	unsigned char *stop, *buf;

	if (rdata == NULL)
		return -1;

	if ((txt->text.value = malloc(rdlength + 1)) == NULL)
		return -1;

	buf = txt->text.value;

	for (stop = rdata + rdlength; rdata < stop; rdata += *rdata + 1) {
		/* Make sure the lengths of the string segments
		 * do not exceed the length of the TXT record.
		 */
		if (rdlength <= txt->text.length + *rdata) {
			free(txt->text.value);
			txt->text.value = NULL;
			return -1;
		}

		memcpy(buf, rdata+1, *rdata);
		txt->text.length += *rdata;
		buf += *rdata;
	}

	txt->text.value[txt->text.length] = '\0';

	return 0;
}

/***********************************************************************
 *** Internal Query Management
 ***********************************************************************/

#ifdef NOT_USED
/*
 * @param name
 * 	A C string of a domain name.
 *
 * @return
 *	An allocated C string suffixed by the "root domain" (.) or NULL
 *	if the current string already suffixed. It is the caller's
 *	responsibility to free() this string.
 */
static char *
pdq_name_root(const char *name)
{
	size_t length;
	char *name_root = NULL;

	errno = 0;
	length = strlen(name);
	if (name[length-1] != '.' && (name_root = malloc(length + 2)) != NULL) {
		TextCopy(name_root, length+2, name);
		name_root[length  ] = '.';
		name_root[length+1] = '\0';
	}

	return name_root;
}

static char *
pdq_ip_to_arpa(const char *name)
{
	char *buffer = NULL;

	errno = 0;
	if (strstr(name, ".arpa") == NULL && (buffer = malloc(DOMAIN_LENGTH+1)) != NULL) {
		(void) reverseIp(name, buffer, DOMAIN_LENGTH+1, 1);
	}

	return buffer;
}
#endif

static int
pdq_parse_ns(const char *ns, SocketAddress *server)
{
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	if (parseIPv6(ns, ipv6) == 0)
		return -1;

#ifdef HAVE_STRUCT_SOCKADDR_IN6
	if (isReservedIPv6(ipv6, IS_IP_V6)) {
		server->in6.sin6_port = htons(DNS_PORT);
		server->in6.sin6_family = AF_INET6;
		memcpy(&server->in6.sin6_addr, ipv6, sizeof (ipv6));
	} else
#endif
	{
		server->in.sin_port = htons(DNS_PORT);
		server->in.sin_family = AF_INET;
		memcpy(&server->in.sin_addr, ipv6+IPV6_OFFSET_IPV4, IPV4_BYTE_LENGTH);
	}

	return 0;
}

static void
pdq_link_add(void *_head, void *_node)
{
	PDQ_link **head = (PDQ_link **) _head;
	PDQ_link *node = (PDQ_link *) _node;

	if (*head != NULL) {
		node->prev = NULL;
		node->next = *head;
	}
	*head = node;
	if (node->prev != NULL)
		node->prev->next = node;
	if (node->next != NULL)
		node->next->prev = node;
}

static void
pdq_link_remove(void *_head, void *_node)
{
	PDQ_link **head = (PDQ_link **) _head;
	PDQ_link *node = (PDQ_link *) _node;

	if (node->prev == NULL)
		*head = node->next;
	else
		node->prev->next = node->next;

	if (node->next != NULL)
		node->next->prev = node->prev;
}

static PDQ_query *
pdq_query_create(void)
{
	PDQ_query *query;

	if ((query = malloc(sizeof (*query))) != NULL) {
		MEMSET(query, 0, sizeof (*query));
		query->prev = query->next = NULL;
		query->created = time(NULL);
		query->next_ns = 0;
	}

	return query;
}

static int
pdq_query_fill(PDQ *pdq, PDQ_query *query, PDQ_class class, PDQ_type type, const char *name, int use_recursion)
{
	int needRoot;
	size_t length;
	struct udp_packet *q;
	unsigned char *s, *t, *label;

	length = strlen(name);
	if (DOMAIN_STRING_LENGTH <= length) {
		errno = EINVAL;
		return -1;
	}

	q = &query->packet;
	needRoot = 0 < length && name[length-1] != '.' && name[length] == '\0';

	/* The header length, length of the name with root segment,
	 * and space for the type and class fields.
	 */
	q->length = sizeof (struct header) + 1 + length + needRoot + 2 * NET_SHORT_BYTE_LENGTH;

	/* Fill in the header fields that are not zero. */
	q->header.id = htons(RANDOM_NUMBER(0xFFFF));

	if (use_recursion)
		/* ASSUMPTION: the DNS servers support recursion. */
		q->header.bits = htons((OP_QUERY << SHIFT_OP) | BITS_RD);

	q->header.qdcount = htons(1);
	q->header.ancount = 0;
	q->header.nscount = 0;
	q->header.arcount = 0;

	/* Copy labels into question. */
	label = q->data;
	t = label + 1;
	for (s = (unsigned char *) name + (*name == '.'); *s != '\0' ; ++s, ++t) {
		if (*s == '.') {
			/* A label cannot exceed 63 octets in length. */
			if (LABEL_LENGTH < t - label - 1) {
				errno = EINVAL;
				return -1;
			}

			/* Set label length. */
			*label = (unsigned char)(t - label - 1);
			label = t;
		} else {
			*t = *s;
		}
	}

	/* A label cannot exceed 63 octets in length. */
	if (LABEL_LENGTH < t - label - 1) {
		errno = EINVAL;
		return -1;
	}

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
	*label   = (unsigned char) class;

	if (0 < debug)
		syslog(LOG_DEBUG, "> query id=%-5u %s %s %s", ntohs(q->header.id), name, pdqClassName(class), pdqTypeName(type));

	return 0;
}

static int
pdq_query_sendto(PDQ_query *query, SocketAddress *ns, SOCKET fd)
{
	int sa_len;
	ssize_t length;

	sa_len = socketAddressLength(ns);
	length = sendto(fd, (void *) &query->packet.header, query->packet.length, 0, &ns->sa, sa_len);

	/* sendto() is not defined to return EINVAL under FreeBSD 4, but under
	 * Linux yes. See known issue http://www.freebsd.org/cgi/query-pr.cgi?pr=26506
	 */
	if (length < 0 && errno == EINVAL)
		return -1;

	if (length != query->packet.length)
		return -1;

	return 0;
}

static int
pdq_query_send(PDQ *pdq, PDQ_query *query)
{
	int i, error_count;

	if (1 < debug)
		pdqLogPacket(&query->packet, 1);

	if (query->next_ns == -1)
		return pdq_query_sendto(query, &query->address, pdq->fd);

	if (pdq->round_robin) {
		if (servers_length <= query->next_ns)
			query->next_ns = 0;

		i = pdq_query_sendto(query, &servers[query->next_ns], pdq->fd);
		query->next_ns++;

		return i;
	}

	error_count = 0;
	for (i = 0; i < servers_length; i++) {
		if (pdq_query_sendto(query, &servers[i], pdq->fd))
			error_count++;
	}

	return -(error_count == servers_length);
}

/*
 * Parse name, type, and class into a PDQ_rr structure.
 */
PDQ_rr *
pdq_reply_rr(struct udp_packet *packet, unsigned char *ptr, unsigned char **stop)
{
	PDQ_type type;
	PDQ_class class;
	PDQ_rr *record;
	unsigned char *name;

	errno = 0;
	name = ptr;
	if ((ptr = pdq_name_skip(packet, ptr)) == NULL) {
		syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
		return NULL;
	}

	type = NET_GET_SHORT(ptr);
	ptr += NET_SHORT_BYTE_LENGTH;

	class = NET_GET_SHORT(ptr);
	ptr += NET_SHORT_BYTE_LENGTH;

	if ((record = pdqCreate(type)) == NULL) {
		syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
	} else {
		record->class = class;
		record->flags = packet->header.bits;
		record->ancount = packet->header.ancount;
		record->name.string.length = pdq_name_copy(
			packet, name,
			(unsigned char *) record->name.string.value,
			sizeof (record->name.string.value)
		);
	}

	if (stop != NULL)
		*stop = ptr;

	return record;
}

static PDQ_rr *
pdq_query_send_all(PDQ *pdq)
{
	time_t now;
	PDQ_query *query, *next;
	PDQ_rr *record, *timedout = NULL;

	(void) time(&now);

	for (query = pdq->pending; query != NULL; query = next) {
		next = query->next;
		if (now < query->created + pdq->timeout) {
			(void) pdq_query_send(pdq, query);
		} else {
			/* Return a record reporting the failed query. */
			record = pdq_reply_rr(&query->packet, query->packet.data, NULL);
			if (record != NULL)
				record->rcode = PDQ_RCODE_TIMEDOUT;
			timedout = pdqListAppend(timedout, record);
			pdq_link_remove(&pdq->pending, query);
			free(query);
		}
	}

	return timedout;
}

static PDQ_rcode
pdq_reply_parse(PDQ *pdq, struct udp_packet *packet, PDQ_rr **list)
{
	int i, j;
	PDQ_rcode rcode;
	unsigned char *ptr;
	unsigned short length;
	PDQ_rr *record, *head, **p, *q;

	head = NULL;
	ptr = packet->data;

	/* Already converted in pdq_query_reply. */
	rcode = packet->header.bits & BITS_RCODE;

	packet->header.id = ntohs(packet->header.id);
	packet->header.qdcount = ntohs(packet->header.qdcount);
	packet->header.ancount = ntohs(packet->header.ancount);
	packet->header.nscount = ntohs(packet->header.nscount);
	packet->header.arcount = ntohs(packet->header.arcount);

	if (0 < debug)
		syslog(LOG_DEBUG, "header id=%u an=%u ns=%u ar=%u rcode=%s", packet->header.id, packet->header.ancount, packet->header.nscount, packet->header.arcount, pdqRcodeName(rcode));

	if (1 < debug)
		pdqLogPacket(packet, 0);

	if (rcode != PDQ_RCODE_OK) {
		/* Return a record reporting the failed query. */
		if ((record = pdq_reply_rr(packet, ptr, NULL)) == NULL) {
			syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
			return PDQ_RCODE_ERRNO;
		}

		record->rcode = rcode;
		*list = record;
		return rcode;
	}

	/* Skip question section. */
	for (i = 0; i < packet->header.qdcount; i++) {
		ptr = pdq_name_skip(packet, ptr);
		if (ptr == NULL) {
			syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
			return PDQ_RCODE_ERRNO;
		}
		ptr += 2 * NET_SHORT_BYTE_LENGTH;
	}

	/* Add all the returned resource-records to the list. */
#undef ONLY_ANSWER_SECTION
#ifdef ONLY_ANSWER_SECTION
	j = packet->header.ancount;
#else
	j = packet->header.ancount + packet->header.nscount + packet->header.arcount;
#endif
	for (i = 0; i < j; i++) {
		if ((record = pdq_reply_rr(packet, ptr, &ptr)) == NULL) {
			/* We're either off the end of the packet (EFAULT,
			 * see pdq_name_skip) or out of memory (ENOMEM) for
			 * creating an RR. Stop parsing!
			 */
			if (errno == EFAULT) {
				/* This should have already been logged by
				 * pdq_name_skip.
				 */
				break;
			}

			/* Skip unknown DNS RR types. */
			if (errno == EINVAL) {
				/* Skip TTL field. */
				ptr += NET_LONG_BYTE_LENGTH;

				/* Get length field for remainder of RR. */
				length = NET_GET_SHORT(ptr);
				ptr += NET_SHORT_BYTE_LENGTH;

				/* Skip this RR. */
				ptr += length;
				continue;
			}

			syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
			pdqListFree(head);
			return PDQ_RCODE_ERRNO;
		}

		if (i < packet->header.ancount)
			record->section = PDQ_SECTION_ANSWER;
		else if (i < packet->header.ancount + packet->header.nscount)
			record->section = PDQ_SECTION_AUTHORITY;
		else
			record->section = PDQ_SECTION_EXTRA;

		record->rcode = rcode;

		record->ttl = NET_GET_LONG(ptr);
		ptr += NET_LONG_BYTE_LENGTH;

		length = NET_GET_SHORT(ptr);
		ptr += NET_SHORT_BYTE_LENGTH;

		switch (record->type) {
		case PDQ_TYPE_A:
			((PDQ_A *) record)->address.ip.offset = IPV6_OFFSET_IPV4;
			/*@fallthrough@*/

		case PDQ_TYPE_AAAA:
			memcpy(
				((PDQ_AAAA *) record)->address.ip.value
				  + ((PDQ_AAAA *) record)->address.ip.offset,
				ptr, length
			);
			((PDQ_AAAA *) record)->address.string.length = formatIP(
				ptr, length, 1,
				((PDQ_AAAA *) record)->address.string.value,
				sizeof (((PDQ_AAAA *) record)->address.string.value)
			);
			ptr += length;
			break;

		case PDQ_TYPE_MX:
			((PDQ_MX *) record)->preference = NET_GET_SHORT(ptr);
			ptr += NET_SHORT_BYTE_LENGTH;
			/*@fallthrough@*/

		case PDQ_TYPE_NS:
		case PDQ_TYPE_PTR:
		case PDQ_TYPE_CNAME:
		case PDQ_TYPE_DNAME:
			((PDQ_NS *) record)->host.string.length = pdq_name_copy(
				packet, ptr,
				(unsigned char *) ((PDQ_NS *) record)->host.string.value,
				sizeof (((PDQ_NS *) record)->host.string.value)
			);
			ptr = pdq_name_skip(packet, ptr);
			if (ptr == NULL) {
				syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
				pdqListFree(head);
				return PDQ_RCODE_ERRNO;
			}
			break;

		case PDQ_TYPE_TXT:
			if (pdq_txt_create((PDQ_TXT *) record, ptr, length)) {
				record->rcode = PDQ_RCODE_ERRNO;
				break;
			}
			ptr += length;
			break;

		case PDQ_TYPE_NULL:
			if ((((PDQ_NULL *) record)->text.value = malloc(length)) == NULL) {
				record->rcode = PDQ_RCODE_ERRNO;
				break;
			}

			memcpy(((PDQ_NULL *) record)->text.value, ptr, length);
			((PDQ_NULL *) record)->text.length = length;
			ptr += length;
			break;

		case PDQ_TYPE_SOA:
			((PDQ_SOA *) record)->mname.string.length = pdq_name_copy(
				packet, ptr,
				(unsigned char *) ((PDQ_SOA *) record)->mname.string.value,
				sizeof (((PDQ_SOA *) record)->mname.string.value)

			);
			ptr = pdq_name_skip(packet, ptr);
			if (ptr == NULL) {
				syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
				pdqListFree(head);
				return PDQ_RCODE_ERRNO;
			}

			((PDQ_SOA *) record)->rname.string.length = pdq_name_copy(
				packet, ptr,
				(unsigned char *) ((PDQ_SOA *) record)->rname.string.value,
				sizeof (((PDQ_SOA *) record)->rname.string.value)
			);
			ptr = pdq_name_skip(packet, ptr);
			if (ptr == NULL) {
				syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
				pdqListFree(head);
				return PDQ_RCODE_ERRNO;
			}

			((PDQ_SOA *) record)->serial = NET_GET_LONG(ptr);
			ptr += NET_LONG_BYTE_LENGTH;

			((PDQ_SOA *) record)->refresh = NET_GET_LONG(ptr);
			ptr += NET_LONG_BYTE_LENGTH;

			((PDQ_SOA *) record)->retry = NET_GET_LONG(ptr);
			ptr += NET_LONG_BYTE_LENGTH;

			((PDQ_SOA *) record)->expire = NET_GET_LONG(ptr);
			ptr += NET_LONG_BYTE_LENGTH;

			((PDQ_SOA *) record)->minimum = NET_GET_LONG(ptr);
			ptr += NET_LONG_BYTE_LENGTH;
			break;

		case PDQ_TYPE_HINFO:
			((PDQ_HINFO *) record)->cpu.string.length = (unsigned short) *ptr;
			memcpy(((PDQ_HINFO *) record)->cpu.string.value, ptr+1, ((PDQ_HINFO *) record)->cpu.string.length);
			((PDQ_HINFO *) record)->cpu.string.value[((PDQ_HINFO *) record)->cpu.string.length] = '\0';
			ptr += *ptr+1;

			((PDQ_HINFO *) record)->os.string.length = (unsigned short) *ptr;
			memcpy(((PDQ_HINFO *) record)->os.string.value, ptr+1, ((PDQ_HINFO *) record)->os.string.length);
			((PDQ_HINFO *) record)->os.string.value[((PDQ_HINFO *) record)->os.string.length] = '\0';
			ptr += *ptr+1;
			break;

		case PDQ_TYPE_MINFO:
			((PDQ_MINFO *) record)->rmailbx.string.length = pdq_name_copy(
				packet, ptr,
				(unsigned char *) ((PDQ_MINFO *) record)->rmailbx.string.value,
				sizeof (((PDQ_MINFO *) record)->rmailbx.string.value)

			);
			ptr = pdq_name_skip(packet, ptr);
			if (ptr == NULL) {
				syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
				pdqListFree(head);
				return PDQ_RCODE_ERRNO;
			}

			((PDQ_MINFO *) record)->emailbx.string.length = pdq_name_copy(
				packet, ptr,
				(unsigned char *) ((PDQ_MINFO *) record)->emailbx.string.value,
				sizeof (((PDQ_MINFO *) record)->emailbx.string.value)
			);
			ptr = pdq_name_skip(packet, ptr);
			if (ptr == NULL) {
				syslog(LOG_ERR, "%s(%d): %s (%d)", __FILE__, __LINE__, strerror(errno), errno);
				pdqListFree(head);
				return PDQ_RCODE_ERRNO;
			}
			break;
		}

#ifndef ONLY_ANSWER_SECTION
		/* Do we already have one of these in the list? */
		if ((record->type == PDQ_TYPE_NS || record->type == PDQ_TYPE_SOA)
		&& pdqListFindHost(head, record->class, record->type, ((PDQ_NS *) record)->host.string.value) != NULL) {
			pdqListFree(record);
			continue;
		}
#endif
		/* Insert MX in reverse sorted order. Remember the list
		 * being added to in reverse order from the order received.
		 * We reverse the list before returning it below.
		 */
		for (p = &head, q = head; q != NULL; p = &q->next, q = q->next) {
			if (q->type != PDQ_TYPE_MX || record->type != PDQ_TYPE_MX
			|| ((PDQ_MX *) q)->preference <= ((PDQ_MX *) record)->preference)
				break;
		}

		if (0 < debug)
			syslog(LOG_DEBUG, "answer id=%-5u %s %s %s %s", packet->header.id, record->name.string.value, pdqClassName(record->class), pdqTypeName(record->type), pdqSectionName(record->section));

		record->next = (PDQ_rr *) q;
		*p = record;
	}

	*list = pdqListReverse(head);

	/* If at least one RR was successfully parsed, return
	 * the rcode from the reply packet, otherwise errno.
	 */
	return head == NULL ? PDQ_RCODE_ERRNO : rcode;
}

static PDQ_rcode
pdq_query_tcp(PDQ *pdq, PDQ_query *query, SocketAddress *address, PDQ_rr **list)
{
	SOCKET fd;
	PDQ_rcode rcode;
	socklen_t socklen;
	ssize_t length, nbytes;
	struct tcp_packet *packet;

	rcode = PDQ_RCODE_ERRNO;

	if (0 < debug)
		syslog(LOG_DEBUG, "query over TCP");

	if ((packet = malloc(sizeof (*packet))) == NULL)
		goto error0;

	if ((fd = socket(address->sa.sa_family, SOCK_STREAM, 0)) == INVALID_SOCKET) {
		UPDATE_ERRNO;
		goto error1;
	}

#ifdef __unix__
	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
	socklen = socketAddressLength(address);

	errno = 0;
	if (connect(fd, &address->sa, socklen)) {
		UPDATE_ERRNO;
		goto error2;
	}

	/* Send two byte length plus the original query packet. */
	length = query->packet.length + sizeof (query->packet.length);
	query->packet.length = htons(query->packet.length);
	if (send(fd, (char *) &query->packet, length, 0) != length) {
		UPDATE_ERRNO;
		goto error2;
	}

	/* Get the overall length of the response first. */
	if (recv(fd, (char *) &packet->length, sizeof (packet->length), 0) != sizeof (packet->length)) {
		UPDATE_ERRNO;
		goto error2;
	}

	/* Get the header portion of the response next. */
	if (recv(fd, (char *) &packet->header, sizeof (packet->header), 0) != sizeof (packet->header)) {
		UPDATE_ERRNO;
		goto error2;
	}

	/* Remember with TCP we might receive less than the full recv()
	 * request so we must keep calling recv() until we get it all or
	 * hit an error.
	 */
	packet->length = ntohs(packet->length);
	for (length = 0; length < packet->length - sizeof (packet->header); length += nbytes) {
		if ((nbytes = recv(fd, packet->data+length, packet->length-length, 0)) <= 0) {
			UPDATE_ERRNO;
			goto error2;
		}
	}

	packet->header.bits = ntohs(packet->header.bits);
	rcode = pdq_reply_parse(pdq, (struct udp_packet *) packet, list);
error2:
	closesocket(fd);
error1:
	free(packet);
error0:
	return rcode;
}

static int
pdq_check_reply_address(SocketAddress *address)
{
	int i;

	for (i = 0; i < servers_length; i++) {
		if (socketAddressEqual((SocketAddress *) &servers[i], address))
			return 1;
	}

	return 0;
}

static PDQ_rcode
pdq_query_reply(PDQ *pdq, struct udp_packet *packet, SocketAddress *address, PDQ_rr **list)
{
	PDQ_rcode rcode;
	PDQ_query *query;

	*list = NULL;

	/* Find the query associated with this response. */
	for (query = pdq->pending; query != NULL; query = query->next) {
		if (packet->header.id == query->packet.header.id)
			break;
	}

	if (query == NULL) {
		/* Not one of our requests or already processed. Ignore it */
		return PDQ_RCODE_ERRNO;
	}

	/* Make sure the result came from queried server. */
	if (query->next_ns == -1) {
		if (!socketAddressEqual(&query->address, address))
			return PDQ_RCODE_ERRNO;
	} else if (!pdq_check_reply_address(address)) {
		return PDQ_RCODE_ERRNO;
	}

	packet->header.bits = ntohs(packet->header.bits);

	if (packet->header.bits & BITS_TC)
		rcode = pdq_query_tcp(pdq, query, address, list);
	else
		rcode = pdq_reply_parse(pdq, packet, list);

	pdq_link_remove(&pdq->pending, query);
	free(query);

	return rcode;
}

/***********************************************************************
 *** Public Query Interface
 ***********************************************************************/

static int
pdq_bind_port(SOCKET fd, SocketAddress *addr, int port)
{
	int rc;
	socklen_t socklen;

	switch (addr->sa.sa_family) {
	case AF_INET:
		addr->in.sin_port = htons(port);
		socklen = sizeof (struct sockaddr_in);
		break;
#ifdef HAVE_STRUCT_SOCKADDR_IN6
	case AF_INET6:
		addr->in6.sin6_port = htons(port);
		socklen = sizeof (struct sockaddr_in6);
		break;
#endif
	default:
		return -1;
	}

	rc = bind(fd, /*(const struct sockaddr *) */ (void *) &addr->sa, socklen);
	UPDATE_ERRNO;

	if (0 < debug)
		syslog(LOG_DEBUG, "pdq_bind_port(port=%d) rc=%d errno=%d (%s)", port, rc, errno, strerror(errno));

	return rc;
}

/**
 * @return
 *	A PDQ structure for handling one or more DNS queries.
 */
PDQ *
pdqOpen(void)
{
	PDQ *pdq = NULL;

	if (servers == NULL && pdqInit())
		goto error0;

	if ((pdq = malloc(sizeof (PDQ))) == NULL)
		goto error0;

	pdq->short_query = pdq_short_query;
	pdq->round_robin = pdq_round_robin;
	pdq->timeout = pdq_max_timeout;
	pdq->pending = NULL;

	if ((pdq->fd = socket(servers[0].sa.sa_family, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		pdqClose(pdq);
		pdq = NULL;
		goto error0;
	}
# ifdef __unix__
	(void) fcntl(pdq->fd, F_SETFD, FD_CLOEXEC);
# endif
#ifdef SO_LINGER
{
	struct linger setlinger = { 0, 0 };
	(void) setsockopt(pdq->fd, SOL_SOCKET, SO_LINGER, (char *) &setlinger, sizeof (setlinger));
}
#endif
	if (pdq_source_port_randomise) {
		int i;
		SocketAddress local;
		char local_name[DOMAIN_STRING_LENGTH], local_ip[IPV6_STRING_LENGTH];

		local_ip[0] = '\0';
		local_name[0] = '\0';
		networkGetMyDetails(local_name, local_ip);

		if (0 < debug)
			syslog(LOG_DEBUG, "host=%s ip=%s", local_name, local_ip);

		if (pdq_parse_ns(local_ip, &local) == 0) {
			local.sa.sa_family = servers[0].sa.sa_family;
			for (i = 0; i < MAX_SPR_ATTEMPTS; i++) {
				if (!pdq_bind_port(pdq->fd, &local, 1025 + RANDOM_NUMBER(64510)))
					break;
			}
		}
	}
error0:
	if (0 < debug)
		syslog(LOG_DEBUG, "pdqOpen() pdq=%lx", (long) pdq);

	return pdq;
}

/**
 * Remove all outstanding queries from the list of requests.
 *
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 */
void
pdqQueryRemoveAll(PDQ *pdq)
{
	PDQ_query *next, *query;

	if (pdq != NULL) {
		for (query = pdq->pending; query != NULL; query = next) {
			next = query->next;
			free(query);
		}

		pdq->pending = NULL;
	}
}

/**
 * @param pdq
 *	A PDQ structure to cleanup.
 */
void
pdqClose(PDQ *pdq)
{
	if (pdq != NULL) {
		pdqQueryRemoveAll(pdq);
		closesocket(pdq->fd);
		free(pdq);
	}
}

/**
 * @param seconds
 * 	Set the maximum timeout for lookups. This will override
 *	the timeout assigned by pdqMaxTimeout() when pdqOpen()
 *	created this PDQ instance.
 */
void
pdqSetTimeout(PDQ *pdq, unsigned seconds)
{
	pdq->timeout = seconds;
}

int
pdqSetBasicQuery(PDQ *pdq, int flag)
{
	int old = pdq->short_query;
	pdq->short_query = flag;
	return old;
}

int
pdqSetLinearQuery(PDQ *pdq, int flag)
{
	int old = pdq->round_robin;
	pdq->round_robin = flag;
	return old;
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param name
 *	A domain, host, or reversed-IP name to find.
 *
 * @param ns
 *	The name or IP address of a specific DNS name server to
 *	query. NULL if the system configued name servers should
 *	be used.
 *
 * @return
 *	Zero on successful posting of the request. Otherwise
 *	-1 on error in posting the request.
 */
int
pdqQuery(PDQ *pdq, PDQ_class class, PDQ_type type, const char *name, const char *ns)
{
	long length;
	PDQ_query *query;
	char *buffer = NULL;

	errno = EFAULT;
	if (pdq == NULL || name == NULL || (query = pdq_query_create()) == NULL)
		goto error0;

	/* If a PTR request is not already rooted in .arpa,
	 * then reverse the IP address ourselves.
	 */
	if (type == PDQ_TYPE_PTR && strstr(name, ".arpa") == NULL) {
		if ((buffer = malloc(DOMAIN_LENGTH+1)) == NULL)
			goto error1;

		length = reverseIp(name, buffer, DOMAIN_LENGTH+1, 1);
		name = buffer;
	}

	/* pdq_query_fill() will build the query and append
	 * the root domain if required.
	 */
	if (pdq_query_fill(pdq, query, class, type, name, 1))
		goto error1;

	if (ns != NULL) {
		if (pdq_parse_ns(ns, &query->address))
			goto error1;
		query->next_ns = -1;
	}

	pdq_link_add(&pdq->pending, query);
	free(buffer);

	return pdq_query_send(pdq, query);
error1:
	free(buffer);
	free(query);
error0:
	return -1;
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param ms
 *	Timeout in milliseconds to wait for an answer.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqListFree() this list when done.
 */
PDQ_rr *
pdqPoll(PDQ *pdq, unsigned ms)
{
	TIMER_DECLARE(mark);
	PDQ_rr *head, *answer;

	if (0 < debug)
		TIMER_START(mark);

	if (pdq == NULL) {
		errno = EFAULT;
		return NULL;
	}

	errno = 0;
	answer = NULL;

#ifdef TEST2
	/* The queries are not sent simultaneously to multiple NS hosts,
	 * so the replies can arrive milli (or nano) seconds apart and
	 * thus give the impression that socketTimeouts is not reporting
	 * multiple ready file descriptors. By introducing a small delay
	 * that allows for other packets to arrive, socketTimeouts does
	 * in fact report multiple sockets with input ready. Execellent!
	 */
	sleep(1);
#endif
	if (socketTimeoutIO(pdq->fd, ms, 1)) {
		int saved_errno = 0;
		PDQ_reply *reply, *replies = NULL, *next;

		/* First collect all the packets on the ready socket... */
		do {
			if ((reply = malloc(sizeof (*reply))) == NULL)
				break;
			reply->prev = reply->next = NULL;
			reply->fromlen = sizeof (reply->from);
			reply->packet.length = recvfrom(
				pdq->fd, (void *) &reply->packet.header, UDP_PACKET_SIZE, 0,
				(struct sockaddr *) &reply->from, &reply->fromlen
			);
			saved_errno = errno;
			pdq_link_add(&replies, reply);
			if (0 < debug) {
				char ipv6[IPV6_STRING_LENGTH];
				*ipv6 = '\0';
#ifdef HAVE_STRUCT_SOCKADDR_IN6
				if (reply->from.sa.sa_family == AF_INET6)
					(void) formatIP((unsigned char *) &reply->from.in6.sin6_addr, IPV6_BYTE_LENGTH, 1, ipv6, sizeof (ipv6));
				else
#endif
					(void) formatIP((unsigned char *) &reply->from.in.sin_addr, IPV4_BYTE_LENGTH, 1, ipv6, sizeof (ipv6));
				syslog(LOG_DEBUG, "< recv id=%u rcode=%d length=%u from=%s", ntohs(reply->packet.header.id), ntohs(reply->packet.header.bits) & BITS_RCODE, reply->packet.length, ipv6);
			}
		} while (socketTimeoutIO(pdq->fd, PDQ_POLL_NEXT_PACKET_TIMEOUT_MS, 1));

		/* Restore the errno related to recvfrom, since we know
		 * that socketTimeoutIO will more than likely set errno
		 * to ETIMEDOUT.
		 */
		errno = saved_errno;

		/* ...then parse the replies. */
		for (reply = replies; reply != NULL; reply = next) {
			next = reply->next;
			pdq_link_remove(&replies, reply);
			(void) pdq_query_reply(pdq, &reply->packet, &reply->from, &head);
			answer = pdqListAppend(answer, head);
			free(reply);
		}
	} else if (errno == ETIMEDOUT) {
		/* pdq_query_send_all() returns a list of timed out queries. */
		answer = pdq_query_send_all(pdq);

		/* Might have been altered after resending queries. */
		errno = ETIMEDOUT;
	}

	if (0 < debug) {
		TIMER_DIFF(mark);

		syslog(
			LOG_DEBUG,
			"poll rc=0x%lx ms=%u pending=%u errno=%d " TIMER_FORMAT,
			(long) answer, ms, pdqListLength((PDQ_rr *) pdq->pending), errno,
			TIMER_FORMAT_ARG(diff_mark)
		);
	}

	return answer;
}

static PDQ_rr *
pdq_wait(PDQ *pdq, int wait_all)
{
	time_t stop;
	unsigned delay;
	TIMER_DECLARE(mark);
	PDQ_rr *head, *answer;

	if (0 < debug)
		TIMER_START(mark);

	answer = NULL;

	if (pdq->pending != NULL) {
		stop = time(NULL) + pdq->timeout;
		delay = pdq_initial_timeout;

		do {
			head = pdqPoll(pdq, delay);
			if (head == NULL && errno == ETIMEDOUT) {
				/* Double the timeout for next iteration. */
				delay += delay;
			}

			answer = pdqListAppend(answer, head);
		} while (pdq->pending != NULL && time(NULL) < stop && (wait_all || answer == NULL));
	}

	if (0 < debug) {
		TIMER_DIFF(mark);

		syslog(
			LOG_DEBUG,
			"wait rc=0x%lx " TIMER_FORMAT, (long) answer,
			TIMER_FORMAT_ARG(diff_mark)
		);
	}

	return answer;
}


/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqListFree() this list when done.
 */
PDQ_rr *
pdqWait(PDQ *pdq)
{
	return pdq_wait(pdq, 0);
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqListFree() this list when done.
 */
PDQ_rr *
pdqWaitAll(PDQ *pdq)
{
	return pdq_wait(pdq, 1);
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @return
 *	True if the list contains a CNAME loop.
 */
int
pdqIsCircular(PDQ_rr *list)
{
	PDQ_rr *r1, *r2;

	for (r1 = list; r1 != NULL; r1 = r1->next) {
		if (r1->type == PDQ_TYPE_CNAME || r1->type == PDQ_TYPE_DNAME) {
			r2 = pdqListFindName(list, r1->class, PDQ_TYPE_5A, ((PDQ_CNAME *) r1)->host.string.value);
			if (r2 == PDQ_CNAME_TOO_DEEP || r2 == PDQ_CNAME_IS_CIRCULAR)
				return 1;
		}
	}

	return 0;
}

/**
 * @param list
 *	A pointer to a PDQ_rr list.
 *
 * @param name
 *	A host or domain name to check.
 *
 * @return
 *	A PDQ_SOA_ code.
 */
PDQ_valid_soa
pdqListHasValidSOA(PDQ_rr *rr_list, const char *name)
{
	int offset;
	PDQ_rr *rr;
	PDQ_valid_soa rc;
	unsigned char ipv6[IPV6_BYTE_LENGTH];

	rc = PDQ_SOA_MISSING;

	if (name == NULL)
		return PDQ_SOA_OK;

	/* Ignore IP addresses by assuming true. */
	if (0 < parseIPv6(name, ipv6)) {
		if (0 < debug)
			syslog(LOG_DEBUG, "pdqIsValidSOA: %s is an IP", name);
		return PDQ_SOA_OK;
	}

	for (rr = rr_list; rr != NULL; rr = rr->next) {
		if (rr->rcode == PDQ_RCODE_UNDEFINED && rr->type == PDQ_TYPE_SOA) {
			if (0 < debug)
				syslog(LOG_DEBUG, "pdqIsValidSOA: %s undefined", rr->name.string.value);
			rc = PDQ_SOA_UNDEFINED;
			break;
		} else if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_CNAME) {
			name = ((PDQ_CNAME *) rr)->host.string.value;
			if ((offset = indexValidTLD(name)) < 0) {
				if (0 < debug)
					syslog(LOG_DEBUG, "pdqIsValidSOA: %s CNAME %s invalid TLD", rr->name.string.value, name);
				rc = PDQ_SOA_BAD_CNAME;
				break;
			}
			offset = strlrcspn(name, offset-1, ".");
			name += offset;

			/*** TODO SOA lookup of CNAME target.
			 ***/

		} else if (rr->rcode == PDQ_RCODE_OK && rr->type == PDQ_TYPE_SOA) {
			/* Is SOA LHS the root domain and query name is not? */
			if (name[0] != '.' && name[1] != '\0' && rr->name.string.length == 1) {
				if (0 < debug)
					syslog(LOG_DEBUG, "pdqIsValidSOA: %s returns root SOA", name);
				rc = PDQ_SOA_ROOTED;
				break;
			}

			/* Is SOA LHS the tail of the name question with a root dot? */
			if (TextInsensitiveEndsWith(name, rr->name.string.value) < 0) {
				/* Is SOA LHS the tail of the name question without a root dot? */
				rr->name.string.value[rr->name.string.length-1] = '\0';
				if (TextInsensitiveEndsWith(name, rr->name.string.value) < 0) {
					if (0 < debug)
						syslog(LOG_DEBUG, "pdqIsValidSOA: %s SOA not tail of %s", rr->name.string.value, name);
					rc = PDQ_SOA_MISMATCH;
					break;
				}
				rr->name.string.value[rr->name.string.length-1] = '.';
			}

			/* Primary name server have a TLD? */
			if (indexValidTLD(((PDQ_SOA *) rr)->mname.string.value) <= 0) {
				if (0 < debug)
					syslog(LOG_DEBUG, "pdqIsValidSOA: %s SOA primary NS %s invalid TLD", rr->name.string.value, ((PDQ_SOA *) rr)->mname.string.value);
				rc = PDQ_SOA_BAD_NS;
				break;
			}

			/* Does the responsible contact have a TLD? */
			if (indexValidTLD(((PDQ_SOA *) rr)->rname.string.value) <= 0) {
				if (0 < debug)
					syslog(LOG_DEBUG, "pdqIsValidSOA: %s SOA contact %s invalid TLD", rr->name.string.value, ((PDQ_SOA *) rr)->mname.string.value);
				rc = PDQ_SOA_BAD_CONTACT;
				break;
			}

			rc = PDQ_SOA_OK;
			break;
		}
	}

	return rc;
}

/***********************************************************************
 *** Convience Functions (pdqGet*, pdqFetch*)
 ***********************************************************************/

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param name
 *	A domain, host, or reversed-IP name to find.
 *
 * @param ns
 *	The name or IP address of a specific DNS name server to
 *	query. NULL if the system configued name servers should
 *	be used.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found, or error occured and errno was set. It
 *	is the caller's responsibility to pdqListFree() this list
 *	when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform a single lookup with an open PDQ session
 *	using pdqQuery() and pdqWaitAll().
 *
 *	For MX, NS, and SOA lookups, it will also perform the lookups
 *	for the A and/or AAAA records, and handle the "implicit MX
 *	0 rule" from RFC 2821.
 *
 *	Depending on the application, it might be nessary to call
 *	pdqQueryRemoveAll() before calling this function in order to
 *	discard any incomplete queries previously queued by earlier
 *	calls to pdqQuery().
 */
PDQ_rr *
pdqGet(PDQ *pdq, PDQ_class class, PDQ_type type, const char *name, const char *ns)
{
	PDQ_rr *head, *rr;

	head = NULL;

	if (pdqQuery(pdq, class, type, name, ns))
		goto error0;

	head = pdqWaitAll(pdq);

	if (type == PDQ_TYPE_MX && pdqListFind(head, PDQ_CLASS_ANY, PDQ_TYPE_MX, PDQ_RCODE_ANY, NULL) == NULL) {
		/* When there is no MX found, apply the implicit MX 0 rule. */
		pdqListFree(head);

		if ((head = pdq_create_rr(class, PDQ_TYPE_MX, name)) == NULL)
			goto error0;

		pdqSetName(&((PDQ_MX *) head)->host, name);
	}

	/* Make sure we have all the associated A / AAAA records. */
	if (head != NULL && !pdq->short_query && (type == PDQ_TYPE_MX || type == PDQ_TYPE_NS || type == PDQ_TYPE_SOA)) {
		for (rr = head; rr != NULL; rr = rr->next) {
			if (rr->rcode == PDQ_RCODE_OK && rr->type == type) {
				/* "domain IN MX ." is a short hand to indicate
				 * that a domain has no MX records. No point in
				 * looking up A/AAAA records. Like wise for NS
				 * and SOA records.
				 */
				if (strcmp(".", ((PDQ_MX *) rr)->host.string.value) == 0)
					continue;

				if (pdqListFindName(rr->next, class, PDQ_TYPE_A, ((PDQ_MX *) rr)->host.string.value) == NULL)
					(void) pdqQuery(pdq, class, PDQ_TYPE_A, ((PDQ_MX *) rr)->host.string.value, ns);
				if (pdqListFindName(rr->next, class, PDQ_TYPE_AAAA, ((PDQ_MX *) rr)->host.string.value) == NULL)
					(void) pdqQuery(pdq, class, PDQ_TYPE_AAAA, ((PDQ_MX *) rr)->host.string.value, ns);
			}
		}

		head = pdqListAppend(head, pdqWaitAll(pdq));
		head = pdqListPruneDup(head);
	}
	if (0 < debug)
		pdqListLog(head);
error0:
	return head;
}

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param name
 *	A domain, host, or reversed-IP name to find.
 *
 * @param ns
 *	The name or IP address of a specific DNS name server to
 *	query. NULL if the system configued name servers should
 *	be used.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found, or error occured and errno was set. It
 *	is the caller's responsibility to pdqListFree() this list
 *	when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform a single lookup using pdqOpen(), pdqGet(),
 *	and pdqClose().
 *
 *	For MX, NS, and SOA lookups, it will also perform the lookups
 *	for the A and/or AAAA records, and handle the "implicit MX
 *	0 rule" from RFC 2821.
 */
PDQ_rr *
pdqFetch(PDQ_class class, PDQ_type type, const char *name, const char *ns)
{
	PDQ *pdq;
	PDQ_rr *head = NULL;

	if ((pdq = pdqOpen()) != NULL) {
		head = pdqGet(pdq, class, type, name, ns);
		pdqClose(pdq);
	}

	return head;
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param prefix_name
 *	A domain, host, or IP name to find.
 *
 * @param suffix_list
 *	A list of DNS black/white lists.
 *
 * @param wait_fn
 *	Specify pdqWait or pdqWaitAll.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqListFree() this list when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform an asynchronus lookup of DNS based lists
 *	with an open PDQ session.
 *
 *	Depending on the application, it might be nessary to call
 *	pdqQueryRemoveAll() before calling this function in order to
 *	discard any incomplete queries previously queued by earlier
 *	calls to pdqQuery().
 */
PDQ_rr *
pdqGetDnsList(PDQ *pdq, PDQ_class class, PDQ_type type, const char *prefix_name, const char **suffix_list, PDQ_rr *(*wait_fn)(PDQ *))
{
	size_t length;
	const char **suffix;
	PDQ_rr *answer, *head, *rr;
	char buffer[DOMAIN_STRING_LENGTH];

	answer = NULL;

	if (pdq == NULL || prefix_name == NULL || suffix_list == NULL || wait_fn == NULL)
		goto error0;

	length = TextCopy(buffer, sizeof (buffer), prefix_name);
	if (sizeof (buffer) <= length)
		goto error0;

	if (0 < length && buffer[length-1] != '.' )
		buffer[length++] = '.';

	for (suffix = suffix_list; *suffix != NULL; suffix++) {
		/* Copy and query if no buffer overflow. */
		if (TextCopy(buffer+length, sizeof (buffer)-length, *suffix + (**suffix == '.')) < sizeof (buffer)-length) {
			if (pdqQuery(pdq, class, type, buffer, NULL))
				goto error1;
		}
	}

	do {
		head = (*wait_fn)(pdq);
		answer = pdqListAppend(answer, head);

		/* When wait_fn == pdqWait, then we have to ignore
		 * responses that are PDQ_RCODE_UNDEFINED (or similar)
		 * as other DNS lists might return a useful answer.
		 * This doesn't affect the wait_fn == pdqWaitAll case.
		 */
		for (rr = head; rr != NULL; rr = rr->next) {
			if (rr->rcode == PDQ_RCODE_OK)
				break;
		}
#ifdef DONT_RETURN_UNDEFINED
		if (rr == NULL && wait_fn == pdqWait) {
			pdqListFree(answer);
			answer = NULL;
		}
#endif
	} while (rr == NULL && pdq->pending != NULL);

	if (0 < debug)
		pdqListLog(answer);
error1:
	pdqQueryRemoveAll(pdq);
error0:
	return answer;
}

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param type
 *	A PDQ_TYPE_ code of the DNS record type to find.
 *
 * @param prefix_name
 *	A domain, host, or IP name to find.
 *
 * @param suffix_list
 *	A list of DNS black/white lists.
 *
 * @param wait_fn
 *	Specify pdqWait or pdqWaitAll.
 *
 * @return
 *	A PDQ_rr pointer to the head of records list or NULL if
 *	no result found. It is the caller's responsibility to
 *	pdqListFree() this list when done.
 *
 * @note
 *	This is a convience function that combines the necessary
 *	steps to perform an asynchronus lookup of DNS based lists
 *	using pdqOpen(), pdqGetDnsList(), and pdqClose().
 */
PDQ_rr *
pdqFetchDnsList(PDQ_class class, PDQ_type type, const char *prefix_name, const char **suffix_list, PDQ_rr *(*wait_fn)(PDQ *))
{
	PDQ *pdq;
	PDQ_rr *answer = NULL;

	if ((pdq = pdqOpen()) != NULL) {
		answer = pdqGetDnsList(pdq, class, type, prefix_name, suffix_list, wait_fn);
		pdqClose(pdq);
	}

	return answer;
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A host name to find.
 *
 * @return
 *	A PDQ_rr pointer to the head of A and/or AAAA records list
 *	or NULL if no result found. It is the caller's responsibility
 *	to pdqListFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
PDQ_rr *
pdqGet5A(PDQ *pdq, PDQ_class class, const char *name)
{
	PDQ_rr *list;

	(void) pdqQuery(pdq, class, PDQ_TYPE_A, name, NULL);
	(void) pdqQuery(pdq, class, PDQ_TYPE_AAAA, name, NULL);
	list = pdqWaitAll(pdq);
	list = pdqListPruneDup(list);

	if (0 < debug)
		pdqListLog(list);

	return list;
}

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A host name to find.
 *
 * @return
 *	A PDQ_rr pointer to the head of A and/or AAAA records list
 *	or NULL if no result found. It is the caller's responsibility
 *	to pdqListFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
PDQ_rr *
pdqFetch5A(PDQ_class class, const char *name)
{
	PDQ *pdq;
	PDQ_rr *answer = NULL;

	if ((pdq = pdqOpen()) != NULL) {
		answer = pdqGet5A(pdq, class, name);
		pdqClose(pdq);
	}

	return answer;
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A domain name for which to find MX records and associated
 *	A/AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @return
 *	A PDQ_rr pointer to the head of MX and A/AAAA records list
 *	or NULL if no result found. A/AAAA records that match the
 *	is_ip_mask are removed, after which any MX without an A/AAAA
 *	record is also removed. It is the caller's responsibility
 *	to pdqListFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
PDQ_rr *
pdqGetMX(PDQ *pdq, PDQ_class class, const char *name, is_ip_t is_ip_mask)
{
	PDQ_rr *list;

	/* Get the MX and A/AAAA list. May have initial CNAME records. */
	list = pdqGet(pdq, class, PDQ_TYPE_MX, name, NULL);

	/* Did we get a result we can use and is it a valid domain? */
	if (list != NULL && list->rcode == PDQ_RCODE_OK) {
		/* Remove impossible to reach MX and A/AAAA records. */
		list = pdqListPrune(list, is_ip_mask);
	}

	if (0 < debug)
		pdqListLog(list);

	return list;
}

/**
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A domain name for which to find MX records and associated
 *	A/AAAA records.
 *
 * @param is_ip_mask
 *	A bit vector of IS_IP_ flags. See isReservedIP*() family of
 *	functions in com/snert/lib/net/network.h.
 *
 * @return
 *	A PDQ_rr pointer to the head of MX and A/AAAA records list
 *	or NULL if no result found. A/AAAA records that match the
 *	is_ip_mask are removed, after which any MX without an A/AAAA
 *	record is also removed. It is the caller's responsibility
 *	to pdqListFree() this list when done.
 *
 * @note
 *	This is a convience function.
 */
PDQ_rr *
pdqFetchMX(PDQ_class class, const char *name, is_ip_t is_ip_mask)
{
	PDQ *pdq;
	PDQ_rr *answer = NULL;

	if ((pdq = pdqOpen()) != NULL) {
		answer = pdqGetMX(pdq, class, name, is_ip_mask);
		pdqClose(pdq);
	}

	return answer;
}

/**
 * @param pdq
 *	A PDQ structure pointer for handling queries.
 *
 * @param class
 *	A PDQ_CLASS_ code of the DNS record class to find.
 *
 * @param name
 *	A host or domain name to check.
 *
 * @param list
 *	A pointer to PDQ_rr pointer in which to pass-back a list
 *	of RR records contains an SOA and A records. The pointer
 *	can be NULL if the list is not required.
 *
 * @return
 *	A PDQ_SOA_ code.
 */
PDQ_valid_soa
pdqTestSOA(PDQ *pdq, PDQ_class class, const char *name, PDQ_rr **list)
{
	int offset;
	PDQ_rr *rr_list;
	PDQ_valid_soa code;

	/* Find start of TLD. */
	if ((offset = indexValidTLD(name)) < 0) {
		unsigned char ipv6[IPV6_BYTE_LENGTH];

		/* Ignore IP addresses by assuming true. */
		if (0 < parseIPv6(name, ipv6)) {
			if (0 < debug)
				syslog(LOG_DEBUG, "pdqIsValidSOA: %s is an IP", name);
			return PDQ_SOA_OK;
		}

		if (0 < debug)
			syslog(LOG_DEBUG, "pdqIsValidSOA: %s invalid TLD", name);
		return PDQ_SOA_BAD_NAME;
	}

	/* Backup one label for the domain. */
	offset = strlrcspn(name, offset-1, ".");
	name += offset;

	rr_list = pdqGet(pdq, class, PDQ_TYPE_SOA, name, NULL);
	code = pdqListHasValidSOA(rr_list, name);

	if (list == NULL)
		pdqListFree(rr_list);
	else
		*list = rr_list;

	return code;
}

static PDQ_rr *
pdq_list_find_5A_by_name(PDQ *pdq, PDQ_class class, const char *name, PDQ_rr **list)
{
	PDQ_rr *a_rr;

	a_rr = pdqListFindName(*list, class, PDQ_TYPE_5A, name);

	if (PDQ_RR_IS_NOT_VALID(a_rr)) {
		/* No IP address for the NS, get it now. */
		a_rr = pdqGet5A(pdq, class, name);
		if (a_rr == NULL || a_rr->rcode != PDQ_RCODE_OK) {
			pdqListFree(a_rr);
			return NULL;
		}

		a_rr = pdqListKeepType(a_rr, PDQ_KEEP_5A|PDQ_KEEP_CNAME);

		/* Append the found list of addresses to the current list
		 * so that they can be reused and eventually freed with
		 * the list.
		 */
		*list = pdqListAppend(*list, a_rr);

		a_rr = pdqListFindName(a_rr, class, PDQ_TYPE_5A, name);
	}

	return a_rr;
}

#undef ORIGINAL
#ifdef ORIGINAL
PDQ_rr *
pdqRootGet(PDQ *pdq, PDQ_class class, PDQ_type type, const char *name, const char *ns)
{
	int old_linear_query, old_basic_query;
	PDQ_rr *ns_list, *ns_rr, *a_rr, *answer;

	answer = NULL;

	/* Also skip extra 5A lookups for all TLD servers. The IP
	 * address of an NS, if not already supplied will be fetched
	 * only as required.
	 */
	old_basic_query = pdqSetBasicQuery(pdq, 1);

	/* Do NOT perform parallel queries on root or TLD servers. */
	old_linear_query = pdqSetLinearQuery(pdq, 1);

	if (ns == NULL) {
		/* From the top. */
		ns_list = root_hints;
	} else {
		ns_list = pdqGet(pdq, class, PDQ_TYPE_NS, name, ns);
		if (ns_list != NULL && (ns_list->flags & BITS_AA)) {
			(void) pdqSetBasicQuery(pdq, old_basic_query);
			(void) pdqSetLinearQuery(pdq, old_linear_query);

			/* Do we have an authoritative result with no answer section? */
			if (ns_list->rcode != PDQ_RCODE_OK || ns_list->ancount == 0) {
				/* Then use NS glue records of immediate parent zone. */
				pdqListFree(ns_list);
				return (PDQ_rr *) 1;
			}

			/* Found authoritative NS servers. */
			return ns_list;
		}
	}

	for (ns_rr = ns_list; ns_rr != NULL; ns_rr = ns_rr->next) {
		if (ns_rr->rcode != PDQ_RCODE_OK || (ns_rr->type != PDQ_TYPE_NS && ns_rr->type != PDQ_TYPE_CNAME))
			continue;

		a_rr = pdq_list_find_5A_by_name(pdq, class, ((PDQ_NS *) ns_rr)->host.string.value, &ns_rr->next);
		if (a_rr == NULL)
			continue;

		if (0 <debug)
			syslog(LOG_DEBUG, "pdqRootGet %s %s %s @%s (%s)", name, pdqClassName(class), pdqTypeName(type), ((PDQ_NS *) ns_rr)->host.string.value, ((PDQ_AAAA *) a_rr)->address.string.value);

		answer = pdqRootGet(pdq, class, PDQ_TYPE_NS, name, ((PDQ_AAAA *) a_rr)->address.string.value);
		if (answer != NULL) {
			if (answer == (PDQ_rr *) 1) {
				if (0 < debug)
					syslog(LOG_DEBUG, "glue records %s %s %s", name, pdqClassName(class), pdqTypeName(type));

				/* Make it look like an authoriative answer.
				 * Required for dnsListQueryNs.
				 */
				for (ns_rr = ns_list; ns_rr != NULL; ns_rr = ns_rr->next) {
					if (ns_rr->type == PDQ_TYPE_NS)
						ns_rr->section = PDQ_SECTION_ANSWER;
				}

				answer = ns_list;
				ns_list = NULL;
			}

			if (answer->rcode == PDQ_RCODE_OK)
				break;

			pdqListFree(answer);
			answer = NULL;
		}
	}

	(void) pdqSetBasicQuery(pdq, old_basic_query);

	if (ns_list != root_hints) {
		pdqListFree(ns_list);
	} else if (type != PDQ_TYPE_NS) {
		/* We should have a set of hopefully authoritative
		 * NS servers or NS glue records.
		 */
		ns_list = answer;
		answer = NULL;
		for (ns_rr = ns_list; ns_rr != NULL; ns_rr = ns_rr->next) {
			if (ns_rr->rcode != PDQ_RCODE_OK
			|| (ns_rr->type != PDQ_TYPE_NS && ns_rr->type != PDQ_TYPE_CNAME))
				continue;

			a_rr = pdq_list_find_5A_by_name(pdq, class, ((PDQ_NS *) ns_rr)->host.string.value, &ns_rr->next);
			if (a_rr == NULL)
				continue;

			answer = pdqGet(pdq, class, type, name, ((PDQ_AAAA *) a_rr)->address.string.value);
			if (answer != NULL) {
				if (answer->rcode == PDQ_RCODE_OK)
					break;
				pdqListFree(answer);
				answer = NULL;
			}
		}
		pdqListFree(ns_list);
	}

	(void) pdqSetLinearQuery(pdq, old_linear_query);

	return answer;
}
#else


PDQ_rr *
pdqRootGetNS(PDQ *pdq, PDQ_class class, const char *name)
{
	PDQ_rr *ns_list, *ns_rr, *a_rr, *answer;
	int offset, old_linear_query, old_basic_query;

	old_basic_query = pdqSetBasicQuery(pdq, 1);
	old_linear_query = pdqSetLinearQuery(pdq, 1);

	ns_list = root_hints;
	offset = (int) strlen(name);

	do {
		answer = NULL;
		offset = strlrcspn(name, offset-1, ".");

		for (ns_rr = ns_list; ns_rr != NULL; ns_rr = ns_rr->next) {
			if (ns_rr->rcode != PDQ_RCODE_OK
			|| (ns_rr->type != PDQ_TYPE_NS && ns_rr->type != PDQ_TYPE_CNAME))
				continue;

			a_rr = pdq_list_find_5A_by_name(pdq, class, ((PDQ_NS *) ns_rr)->host.string.value, &ns_rr->next);
			if (a_rr == NULL)
				continue;

			if (0 <debug)
				syslog(LOG_DEBUG, "pdqRootGetNS %s %s NS @%s (%s)", name, pdqClassName(class), ((PDQ_NS *) ns_rr)->host.string.value, ((PDQ_AAAA *) a_rr)->address.string.value);

			answer = pdqGet(pdq, class, PDQ_TYPE_NS, name+offset, ((PDQ_AAAA *) a_rr)->address.string.value);

			if (answer != NULL) {
				if (answer->rcode == PDQ_RCODE_OK) {
#ifdef MORE_DIG_LIKE
/* Consider dig ns www.snert.com will return a CNAME and SOA,
 * which this code will duplicate, BUT we want to find the NS
 * for a name, be it authoritative or parent zone's glue.
 */
					if ((answer->flags & BITS_AA) && answer->ancount == 0) {
#else
					if ((answer->flags & BITS_AA)) {
						/* Use glue records that got us here. */
#endif
						ns_rr = NULL;
					}
					break;
				}

				pdqListFree(answer);
				answer = NULL;
			}
		}

		if (ns_rr == NULL && ns_list != root_hints) {
			/* Assume end of list without an answer bumps to glue records. */
			if (0 < debug)
				syslog(LOG_DEBUG, "glue records %s %s NS", name, pdqClassName(class));
			pdqListFree(answer);
			answer = ns_list;

			/* Make it look like an authoriative answer.
			 * Required for dnsListQueryNs.
			 */
			for (ns_rr = ns_list; ns_rr != NULL; ns_rr = ns_rr->next) {
				if (ns_rr->type == PDQ_TYPE_NS)
					ns_rr->section = PDQ_SECTION_ANSWER;
			}

			goto use_glue_records;
		}

		if (ns_list != root_hints)
			pdqListFree(ns_list);

		ns_list = answer;
	} while (0 < offset);
use_glue_records:
	(void) pdqSetLinearQuery(pdq, old_linear_query);
	(void) pdqSetBasicQuery(pdq, old_basic_query);

	return answer;
}

PDQ_rr *
pdqRootGet(PDQ *pdq, PDQ_class class, PDQ_type type, const char *name, const char *ns)
{
	int old_linear_query;
	PDQ_rr *ns_list, *ns_rr, *a_rr, *answer;

	/* Find authoritative NS or parent zone's NS glue_records. */
	if ((ns_list = pdqRootGetNS(pdq, class, name)) == NULL)
		return NULL;

	if (type == PDQ_TYPE_NS)
		return ns_list;

	/* Sequential query of the NS servers. */
	old_linear_query = pdqSetLinearQuery(pdq, 1);

	answer = NULL;
	for (ns_rr = ns_list; ns_rr != NULL; ns_rr = ns_rr->next) {
		if (ns_rr->rcode != PDQ_RCODE_OK
		|| (ns_rr->type != PDQ_TYPE_NS && ns_rr->type != PDQ_TYPE_CNAME))
			continue;

		a_rr = pdq_list_find_5A_by_name(pdq, class, ((PDQ_NS *) ns_rr)->host.string.value, &ns_rr->next);
		if (a_rr == NULL)
			continue;

		answer = pdqGet(pdq, class, type, name, ((PDQ_AAAA *) a_rr)->address.string.value);

		if (answer != NULL) {
			if (answer->rcode == PDQ_RCODE_OK)
				break;
			pdqListFree(answer);
			answer = NULL;
		}
	}
	pdqListFree(ns_list);

	(void) pdqSetLinearQuery(pdq, old_linear_query);

	return answer;
}
#endif

/***********************************************************************
 *** Initialisation
 ***********************************************************************/

static Vector
pdq_get_name_servers(const char *cf)
{
	FILE *fp;
	char *line, *token;
	Vector servers, tokens;

	fp = NULL;
	line = NULL;
	servers = NULL;

	if (cf == NULL) {
#ifdef __WIN32__
		IP_ADDR_STRING *ip;
		ULONG netinfo_size;
		FIXED_INFO *netinfo;

		if ((servers = VectorCreate(1)) == NULL)
			goto error0;

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
			free(netinfo);
			goto error3;
		}

		for (ip = &netinfo->DnsServerList; ip != NULL; ip = ip->Next) {
			if (*ip->IpAddress.String != '\0')
				(void) VectorAdd(servers, strdup(ip->IpAddress.String));
		}

		free(netinfo);

		goto empty_list_check;
#else
		cf = RESOLV_CONF;
#endif
	}

	if ((fp = fopen(cf, "r")) == NULL)
		goto error0;

	if ((line = malloc(BUFSIZ)) == NULL)
		goto error1;

	if ((servers = VectorCreate(3)) == NULL)
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

	if (!feof(fp))
		goto error3;
#ifdef __WIN32__
empty_list_check:
#endif
	if (VectorLength(servers) <= 0) {
		/* Default to localhost when resolv.conf is empty. */
		(void) VectorAdd(servers, strdup("127.0.0.1"));
	}

	if (VectorLength(servers) <= 0) {
error3:
		VectorDestroy(servers);
		servers = NULL;
	}
error2:
	free(line);
error1:
	if (fp != NULL)
		fclose(fp);
error0:
	return servers;
}

/**
 * @param servers
 *	A list of pointers to C strings, each specifying a
 *	name server host or IP address. This list will override
 *	the system default list.
 *
 * @return
 *	Zero on success, otherwise -1 on error.
 */
int
pdqSetServers(Vector name_servers)
{
	int i;
	char *server;

	/* Convert the list of name server addresses into IP addresses. */
	free(servers);
	servers_length = VectorLength(name_servers);
	if ((servers = calloc(servers_length, sizeof (*servers))) == NULL)
		return -1;

	rand_seed = 0;
	for (i = 0; i < servers_length; i++) {
		if ((server = VectorGet(name_servers, i)) == NULL)
			continue;

		rand_seed = TextHash(rand_seed, server);
		if (pdq_parse_ns(server, &servers[i])) {
			VectorRemove(name_servers, i--);
			servers_length--;
			continue;
		}
	}

	srand(rand_seed ^ time(NULL));

	return 0;
}

/**
 * (Re)Load the resolv.conf file. Currently only nameserver lines
 * are recognised.
 *
 * @return
 *	Zero (0) on success otherwise -1 one error.
 */
int
pdqInit(void)
{
	int rc = -1;
	Vector name_servers;

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
	/* Get the list of name server addresses. */
	name_servers = pdq_get_name_servers(NULL);
	if (name_servers == NULL) {
		if ((name_servers = VectorCreate(1)) == NULL)
			goto error0;
		VectorSetDestroyEntry(name_servers, free);
		VectorAdd(name_servers, strdup("0.0.0.0"));
	}

	rc = pdqSetServers(name_servers);
	VectorDestroy(name_servers);

	/* Fetch a copy of the current root servers. */
	pdqSetRoundRobin(1);
	pdqSetShortQuery(1);
	if (0 < debug)
		syslog(LOG_DEBUG, "fetch root_hints...");
	root_hints = pdqFetch(PDQ_CLASS_IN, PDQ_TYPE_NS, ".", NULL);
	if (0 < debug)
		syslog(LOG_DEBUG, "prune root_hints...");
	root_hints = pdqListKeepType(root_hints, PDQ_KEEP_NS|PDQ_KEEP_5A);
	root_hints = pdqListPrune(root_hints, IS_IP_RESTRICTED|IS_IP_LAN);
	if (0 < debug)
		pdqListLog(root_hints);
	pdqSetRoundRobin(0);
	pdqSetShortQuery(0);
error0:
	return rc;
}

/**
 * Terminate the DNS subsystem.
 */
void
pdqFini(void)
{
	if (root_hints != NULL) {
		pdqListFree(root_hints);
		root_hints = NULL;
	}
	free(servers);
	servers = NULL;
}

/**
 * @param level
 *	Set debug level. The higher the more verbose.  Zero is silent.
 */
void
pdqSetDebug(int level)
{
	debug = level;
}

/**
 * @param seconds
 *	Set the default initial timeout value for any lookup.
 */
void
pdqInitialTimeout(unsigned seconds)
{
	pdq_initial_timeout = seconds;
	if (pdq_initial_timeout == 0)
		pdq_initial_timeout = 1;
	pdq_initial_timeout *= 1000;
}

/**
 * @param seconds
 *	Set the default maximum timeout value for any lookup.
 *	This is the timeout initially assigned to a PDQ instance
 *	by pdqOpen(). Affects pdqFetch(), pdqGet().
 */
void
pdqMaxTimeout(unsigned seconds)
{
	pdq_max_timeout = seconds;
}

/**
 * @param flag
 *	Set true to query NS servers, per pdqQuery, in round robin order
 *	according to the order defined in resolv.conf. Set false (default)
 *	to query all the NS servers at the same time.
 */
void
pdqSetRoundRobin(int flag)
{
	pdq_round_robin = flag;
}

void
pdqSetShortQuery(int flag)
{
	pdq_short_query = flag;
}

/**
 * @param flag
 *	Set true to enable source port randomisation.
 */
void
pdqSetSourcePortRandomisation(int flag)
{
	pdq_source_port_randomise = flag;
}

/***********************************************************************
 *** CLI
 ***********************************************************************/

#ifdef TEST

#include <stdarg.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/util/getopt.h>

static char *query_server;

static const char usage[] =
"usage: pdq [-LprRsSv][-c class][-l suffixes][-t sec][-q server]\n"
"           type name [type name ...]\n"
"\n"
"-c class\tone of IN (default), CH, CS, HS, or ANY\n"
"-L\t\twait for all the replies from DNS lists, see -l\n"
"-l suffixes\tcomma separated list of DNS list suffixes\n"
"-p\t\tprune invalid MX, NS, or SOA records\n"
"-r\t\tenable round robin mode\n"
"-R\t\tsearch from the root\n"
"-s\t\tenable source port randomisation\n"
"-S\t\tcheck SOA is valid for name\n"
"-t sec\t\ttimeout in seconds, default 45\n"
"-q server\tname server to query\n"
"-v\t\tverbose debug output\n"
"type\t\tone of A, AAAA, CNAME, DNAME, HINFO, MINFO, MX,\n"
"\t\tNS, NULL, PTR, SOA, TXT, or ANY\n"
"name\t\ta host, domain, IPv4, or IPv6 to lookup\n"
"\n"
"Exit Codes\n"
QUOTE(EXIT_SUCCESS) "\t\tresult found\n"
QUOTE(EXIT_FAILURE) "\t\tno result found\n"
QUOTE(EX_USAGE) "\t\tusage error\n"
QUOTE(EX_SOFTWARE) "\t\tinternal error\n"
"\n"
LIBSNERT_STRING " " LIBSNERT_COPYRIGHT "\n"
;

#if ! defined(__MINGW32__)
void
syslog(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (logFile == NULL)
		vsyslog(level, fmt, args);
	else
		LogV(level, fmt, args);
	va_end(args);
}
#endif

#ifdef __WIN32__
static DWORD strerror_tls = TLS_OUT_OF_INDEXES;
static const char unknown_error[] = "(unknown error)";

void
strerror_free_data(void)
{
	if (strerror_tls != TLS_OUT_OF_INDEXES) {
		char *error_string = (char *) TlsGetValue(strerror_tls);
		LocalFree(error_string);
	}
}

char *
strerror(int error_code)
{
	char *error_string;

	if (strerror_tls == TLS_OUT_OF_INDEXES) {
		atexit(strerror_free_data);
		strerror_tls = TlsAlloc();
		if (strerror_tls == TLS_OUT_OF_INDEXES)
			return (char *) unknown_error;
	}

	error_string = (char *) TlsGetValue(strerror_tls);
	LocalFree(error_string);

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &error_string, 0, NULL
	);

	if (!TlsSetValue(strerror_tls, error_string)) {
		LocalFree(error_string);
		return (char *) unknown_error;
	}

	return error_string;
}
#endif

int
main(int argc, char **argv)
{
	PDQ *pdq;
	Vector suffix_list;
	PDQ_rr *list, *answers;
	PDQ_rr *(*wait_fn)(PDQ *);
	char buffer[DOMAIN_STRING_LENGTH+1];
	int ch, type, class, i, prune_list, check_soa, from_root;

	from_root = 0;
	check_soa = 0;
	prune_list = 0;
	wait_fn = pdqWait;
	suffix_list = NULL;
	class = PDQ_CLASS_IN;

	while ((ch = getopt(argc, argv, "LprRsSvc:l:t:q:")) != -1) {
		switch (ch) {
		case 'c':
			class = pdqClassCode(optarg);
			break;

		case 'L':
			wait_fn = pdqWaitAll;
			break;

		case 'l':
			suffix_list = TextSplit(optarg, ",", 0);
			break;

		case 't':
			pdqMaxTimeout(strtol(optarg, NULL, 10));
			break;

		case 'q':
			query_server = optarg;
			break;

		case 'v':
			pdqSetDebug(debug+1);
			break;

		case 'r':
			pdqSetRoundRobin(1);
			break;

		case 'R':
			from_root = 1;
			break;

		case 's':
			pdqSetSourcePortRandomisation(1);
			break;

		case 'S':
			check_soa = 1;
			break;

		case 'p':
			prune_list = 1;
			break;

		default:
			(void) fprintf(stderr, usage);
			exit(EX_USAGE);
		}
	}

	if (argc < optind + 2 || ((argc - optind) & 1)) {
		fprintf(stderr, usage);
		exit(EX_USAGE);
	}

	srand(TextHash(0, argv[optind+1]) ^ time(NULL));

	if (atexit(pdqFini)) {
		fprintf(stderr, "atexit() failed\n");
		exit(EX_SOFTWARE);
	}

	if (0 < debug) {
		LogSetLevel(LOG_DEBUG);
		LogSetProgramName("pdq");
		LogOpen("(standard error)");
		setlogmask(LOG_UPTO(LOG_DEBUG));
	} else {
		openlog("pdq", LOG_PID, LOG_USER);

	}

	if (pdqInit()) {
		fprintf(stderr, "pdqInit() failed\n");
		exit(EX_SOFTWARE);
	}

	if ((pdq = pdqOpen()) == NULL) {
		fprintf(stderr, "pdqOpen() failed\n");
		exit(EX_SOFTWARE);
	}

	answers = NULL;

	for (i = optind; i < argc; i += 2) {
		type = pdqTypeCode(argv[i]);

		if (suffix_list == NULL) {
			if (check_soa) {
				if ((ch = pdqTestSOA(pdq, class, argv[i+1], &list)) != 0)
					printf("%s invalid SOA: %s (%d)\n", argv[i+1], pdqSoaName(ch), ch);
			} else if (from_root) {
				list = pdqRootGet(pdq, class, type, argv[i+1], NULL);
			} else {
				list = pdqGet(pdq, class, type, argv[i+1], query_server);
			}

			if (prune_list)
				list = pdqListPrune(list, IS_IP_RESTRICTED|IS_IP_LAN);
		} else {
			if (spanIP(argv[i+1]) == 0)
				(void) TextCopy(buffer, sizeof (buffer), argv[i+1]);
			else
				(void) reverseIp(argv[i+1], buffer, sizeof (buffer), 0);

			list = pdqGetDnsList(pdq, class, type, buffer, (const char **) VectorBase(suffix_list), wait_fn);
		}

		if (pdqIsCircular(list)) {
			pdqListDump(stdout, list);
			printf("%s %s %s: INFINITE CNAME LOOP!\n", argv[i+1], pdqClassName(class), argv[i]);
			pdqListFree(list);
		} else {
			answers = pdqListAppend(answers, list);
		}
	}

	pdqListDump(stdout, answers);
	pdqListFree(answers);
	pdqClose(pdq);
	pdqFini();

	if (suffix_list != NULL)
		VectorDestroy(suffix_list);

	return answers != NULL ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif /* TEST */

/***********************************************************************
 *** END
 ***********************************************************************/
