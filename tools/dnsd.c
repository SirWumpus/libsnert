/*
 * dnsd.c
 *
 * Black & White List DNS Server
 *
 * Copyright 2010 by Anthony Howe.  All rights reserved.
 */

/* AlexB wish list...
 *
 * 1. Flat file support, using rbldnsd format.
 *    http://www.corpit.ru/mjt/rbldnsd/rbldnsd.8.html
 *
 * 2. Add record directly to memory via listener port.
 */

#ifndef DATABASE_PATH
#define DATABASE_PATH		"./dnsd.sq3"
#endif

#ifndef DOMAIN_SUFFIX
#define DOMAIN_SUFFIX		".localhost."
#endif

#define _NAME			"dnsd"
#define _COPYRIGHT		"Copyright 2010 by Anthony Howe.  All rights reserved."

#ifdef __WIN32__
# define PID_FILE		"./" _NAME ".pid"
#else
# define PID_FILE		"/var/run/" _NAME ".pid"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#ifdef HAVE_SQLITE3_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

# include <sqlite3.h>
# if SQLITE_VERSION_NUMBER < 3007000
#  error "Thread safe SQLite3 version 3.7.0 or better required."
# endif

#if defined(__MINGW32__)
# define HAVE_PTHREAD_CREATE
# undef HAVE_SIGSET_T
#else
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
#endif /* __MINGW32__ */

#ifdef __WIN32__
# include <windows.h>
# include <sddl.h>
#endif

#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/net/network.h>
#include <com/snert/lib/net/server.h>
#include <com/snert/lib/sys/pid.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/sys/sysexits.h>
#include <com/snert/lib/type/queue.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/getopt.h>

/***********************************************************************
 *** Constants
 ***********************************************************************/

#define DNS_PORT		53
#define DNS_RR_MIN_LENGTH	12
#define NET_SHORT_SIZE		2
#define NET_LONG_SIZE		4
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

#define SQL_TABLE_EXISTS	\
"SELECT name FROM sqlite_master WHERE type='table' AND name='dns';"

#define SQL_CREATE_TABLE	\
"CREATE TABLE dns(type INTEGER NOT NULL, name VARCHAR(255) NOT NULL, value VARCHAR(255));"

#define SQL_INDEX_EXISTS	\
"SELECT name FROM sqlite_master WHERE type='index' AND name='dns_name_index';"

#define SQL_CREATE_INDEX	\
"CREATE INDEX dns_name_index ON dns(name);"

#define SQL_SELECT_ONE	\
"SELECT value FROM dns WHERE type=?1 AND name=?2;"

#define SQL_SELECT_ALL	\
"SELECT value, COUNT(*) FROM dns WHERE name=?1;"

/***********************************************************************
 *** Internal types.
 ***********************************************************************/

typedef enum {
	DNS_TYPE_UNKNOWN		= 0,
	DNS_TYPE_A			= 1,	/* RFC 1035 */
	DNS_TYPE_NS			= 2,	/* RFC 1035 */
	DNS_TYPE_CNAME			= 5,	/* RFC 1035 */
	DNS_TYPE_SOA			= 6,	/* RFC 1035 */
	DNS_TYPE_NULL			= 10,	/* RFC 1035 */
	DNS_TYPE_WKS			= 11,	/* RFC 1035, not supported */
	DNS_TYPE_PTR			= 12,	/* RFC 1035 */
	DNS_TYPE_HINFO			= 13,	/* RFC 1035 */
	DNS_TYPE_MINFO			= 14,	/* RFC 1035 */
	DNS_TYPE_MX			= 15,	/* RFC 1035 */
	DNS_TYPE_TXT			= 16,	/* RFC 1035 */
	DNS_TYPE_AAAA			= 28,	/* RFC 1886, 3596 */
	DNS_TYPE_A6			= 38,	/* RFC 2874, not supported */
	DNS_TYPE_DNAME			= 39,	/* RFC 2672 */
	DNS_TYPE_ANY			= 255,	/* RFC 1035 all (behaves like ``any'') */
	DNS_TYPE_5A			= 256,	/* special API type for pdqListFindName */
} DNS_type;

typedef enum {
	DNS_CLASS_IN			= 1,	/* RFC 1035 Internet */
	DNS_CLASS_CS			= 2,	/* RFC 1035 CSNET */
	DNS_CLASS_CH			= 3,	/* RFC 1035 CHAOS */
	DNS_CLASS_HS			= 4,	/* RFC 1035 Hesiod */
	DNS_CLASS_ANY			= 255,	/* RFC 1035 any */
} DNS_class;

typedef enum {
	DNS_RCODE_OK			= 0,	/* RFC 1035 */
	DNS_RCODE_FORMAT		= 1,	/* RFC 1035 */
	DNS_RCODE_SERVER		= 2,	/* RFC 1035 */
	DNS_RCODE_SERVFAIL		= 2,	/* RFC 1035 */
	DNS_RCODE_NXDOMAIN		= 3,	/* RFC 1035 */
	DNS_RCODE_UNDEFINED		= 3,	/* RFC 1035 */
	DNS_RCODE_NOT_IMPLEMENTED	= 4,	/* RFC 1035 */
	DNS_RCODE_REFUSED		= 5,	/* RFC 1035 */
	DNS_RCODE_ERRNO			= 16,	/* local error */
	DNS_RCODE_TIMEDOUT		= 17,	/* timeout error */
	DNS_RCODE_ANY			= 255,	/* any rcode, see pdqListFind */
} DNS_rcode;

typedef enum {
	DNS_SECTION_UNKNOWN		= 0,
	DNS_SECTION_QUESTION		= 1,
	DNS_SECTION_ANSWER		= 2,
	DNS_SECTION_AUTHORITY		= 3,
	DNS_SECTION_EXTRA		= 4,
} DNS_section;

typedef struct {
	uint16_t id;
	uint16_t bits;
	uint16_t qdcount;
	uint16_t ancount;
	uint16_t nscount;
	uint16_t arcount;
} DNS_header;

typedef struct {
	long length;
	DNS_header header;
	uint8_t data[UDP_PACKET_SIZE - sizeof (DNS_header)];
} DNS_packet;

/*
 * We're only going to support A, AAAA, and TXT (max. 255 bytes) records.
 */
typedef struct rr {
	DNS_type type;
	size_t name_length;
	size_t data_length;
	unsigned char name[DOMAIN_SIZE];
	unsigned char data[DOMAIN_SIZE];
} DNS_rr;

typedef struct {
	SocketAddress client;
	DNS_packet packet;
	ListItem node;
} DNS_query;

static int debug;
static int daemon_mode = 1;
static char *windows_service;
static unsigned port = DNS_PORT;
static const char *domain_suffix = DOMAIN_SUFFIX;
static const char *database_path = DATABASE_PATH;

static const char usage_msg[] =
"usage: dnsd [-dv][-f path][-p port][-s suffix][-w add|remove]\n"
"\n"
"-d\t\tdisable daemon, run in foreground\n"
"-f path\t\tfile path of DNS database; default \"" DATABASE_PATH "\"\n"
"-p port\t\tserver port; default 53\n"
"-s suffix\tdomain suffix of server; default \"" DOMAIN_SUFFIX "\"\n"
"-v\t\tverbose debugging\n"
"-w arg\t\tadd or remove Windows service\n"
"\n"
"A simple UDP only DNS server intended for implementing black & white\n"
"lists. Supports A, AAAA, and TXT records.\n"
"\n"
_COPYRIGHT "\n"
;

static const char log_oom[] = "out of memory %s(%d)";
static const char log_init[] = "init error %s(%d): %s (%d)";
static const char log_internal[] = "internal error %s(%d): %s (%d)";
static const char log_buffer[] = "buffer overflow %s(%d)";

/***********************************************************************
 ***
 ***********************************************************************/

#if ! defined(__MINGW32__)
#undef syslog

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

/***********************************************************************
 *** Unix Signal Handling
 ***********************************************************************/

#ifdef __unix__
/*
 * Set up an event loop to wait and act on SIGPIPE, SIGHUP, SIGINT,
 * SIGQUIT, and SIGTERM. The main server thread and all other child
 * threads will ignore them. This way we can do more interesting
 * things than are possible in a typical signal handler.
 */
int
serverSignalsInit(ServerSignals *signals)
{
        (void) sigemptyset(&signals->signal_set);
# ifdef SIGHUP
        (void) sigaddset(&signals->signal_set, SIGHUP);
# endif
# ifdef SIGINT
        (void) sigaddset(&signals->signal_set, SIGINT);
# endif
# ifdef SIGPIPE
	(void) sigaddset(&signals->signal_set, SIGPIPE);
# endif
# ifdef SIGQUIT
	(void) sigaddset(&signals->signal_set, SIGQUIT);
# endif
# ifdef SIGTERM
	(void) sigaddset(&signals->signal_set, SIGTERM);
# endif
# ifdef SERVER_CATCH_USER_SIGNALS
#  ifdef SIGUSR1
	(void) sigaddset(&signals->signal_set, SIGUSR1);
#  endif
#  ifdef SIGUSR2
	(void) sigaddset(&signals->signal_set, SIGUSR2);
#  endif
# endif
# ifdef SERVER_CATCH_ALARMS_SIGNALS
#  ifdef SIGALRM
	(void) sigaddset(&signals->signal_set, SIGALRM);
#  endif
#  ifdef SIGPROF
	(void) sigaddset(&signals->signal_set, SIGPROF);
#  endif
#  ifdef SIGVTALRM
	(void) sigaddset(&signals->signal_set, SIGVTALRM);
#  endif
# endif /* SERVER_CATCH_ALARMS_SIGNALS */
# ifdef SERVER_CATCH_ULIMIT_SIGNALS
#  ifdef SIGXCPU
	(void) sigaddset(&signals->signal_set, SIGXCPU);
#  endif
#  ifdef SIGXFSZ
	(void) sigaddset(&signals->signal_set, SIGXFSZ);
#  endif
# endif /* SERVER_CATCH_ULIMIT_SIGNALS */
        if (pthread_sigmask(SIG_BLOCK, &signals->signal_set, NULL)) {
		syslog(LOG_ERR, log_init, __FILE__, __LINE__, strerror(errno), errno);
		return -1;
	}

	return 0;
}

void
serverSignalsFini(ServerSignals *signals)
{
	(void) pthread_sigmask(SIG_UNBLOCK, &signals->signal_set, NULL);
}

int
serverSignalsLoop(ServerSignals *signals)
{
	int signal, running;

	for (running = 1; running; ) {
		signal = 0;
		if (sigwait(&signals->signal_set, &signal))
			continue;

		switch (signal) {
		case SIGINT:
		case SIGTERM:		/* Immediate termination */
		case SIGQUIT:		/* Slow quit, wait for sessions to complete. */
			running = 0;
			break;
# ifdef SIGPIPE
		case SIGPIPE:
			/* Silently ignore since we can get LOTS of
			 * these during the life of the server.
			 */
			break;
# endif
# ifdef SIGHUP
		case SIGHUP:
# endif
# ifdef SERVER_CATCH_USER_SIGNALS
#  ifdef SIGUSR1
		case SIGUSR1:
#  endif
#  ifdef SIGUSR2
		case SIGUSR2:
#  endif
# endif
# ifdef SERVER_CATCH_ALARMS_SIGNALS
#  ifdef SIGALRM
		case SIGALRM:
#  endif
#  ifdef SIGPROF
		case SIGPROF:
#  endif
#  ifdef SIGVTALRM
		case SIGVTALRM:
#  endif
# endif /* SERVER_CATCH_ALARMS_SIGNALS */
# ifdef SERVER_CATCH_ULIMIT_SIGNALS
#  ifdef SIGXCPU
		case SIGXCPU:
#  endif
#  ifdef SIGXFSZ
		case SIGXFSZ:
#  endif
# endif /* SERVER_CATCH_ULIMIT_SIGNALS */
			syslog(LOG_INFO, "signal %d ignored", signal);
			break;
		}
	}

	syslog(LOG_INFO, "signal %d received", signal);

	return signal;
}
#endif /* __unix__ */

/***********************************************************************
 *** Windows Signal Handling
 ***********************************************************************/

#ifdef __WIN32__

# ifdef ENABLE_SECURITY_DESCRIPTOR
/* Cygwin/Mingw do not define ConvertStringSecurityDescriptorToSecurityDescriptor().
 * This would allow for ./smtpf -quit by an admin. user. The current alternative is
 * to use the Windows service console or "net start smtp" and "net stop smtpf".
 */
static int
createMyDACL(SECURITY_ATTRIBUTES *sa)
{
	TCHAR * szSD =
	TEXT("D:")			/* Discretionary ACL */
	TEXT("(OD;OICI;GA;;;BG)")     	/* Deny access to built-in guests */
	TEXT("(OD;OICI;GA;;;AN)")     	/* Deny access to anonymous logon */
#  ifdef ALLOW_AUTH_USER
	TEXT("(OA;OICI;GRGWGX;;;AU)") 	/* Allow read/write/execute auth. users */
#  endif
	TEXT("(OA;OICI;GA;;;BA)");    	/* Allow full control to administrators. */

	if (sa == NULL)
		return 0;

	return ConvertStringSecurityDescriptorToSecurityDescriptor(
		szSD, SDDL_REVISION_1, &sa->lpSecurityDescriptor, NULL
	);
}
# endif

int
serverSignalsInit(ServerSignals *signals)
{
	int length;
	char event_name[SIGNAL_LENGTH][128];

	length = snprintf(event_name[SIGNAL_QUIT], sizeof (event_name[SIGNAL_QUIT]), "Global\\%ld-QUIT", GetCurrentProcessId());
	if (sizeof (event_name[SIGNAL_QUIT]) <= length)
		return -1;

	length = snprintf(event_name[SIGNAL_TERM], sizeof (event_name[SIGNAL_TERM]), "Global\\%ld-TERM", GetCurrentProcessId());
	if (sizeof (event_name[SIGNAL_TERM]) <= length)
		return -1;

# ifdef ENABLE_SECURITY_DESCRIPTOR
{
	SECURITY_ATTRIBUTES sa;

	sa.bInheritHandle = 0;
	sa.nLength = sizeof (sa);

	if (createMyDACL(&sa)) {
		signals->signal_event[SIGNAL_QUIT] = CreateEvent(&sa, 0, 0, event_name[SIGNAL_QUIT]);
		signals->signal_event[SIGNAL_TERM] = CreateEvent(&sa, 0, 0, event_name[SIGNAL_TERM]);
		LocalFree(sa.lpSecurityDescriptor);
	}
}
# else
	signals->signal_event[SIGNAL_QUIT] = CreateEvent(NULL, 0, 0, event_name[SIGNAL_QUIT]);
	signals->signal_event[SIGNAL_TERM] = CreateEvent(NULL, 0, 0, event_name[SIGNAL_TERM]);
# endif
	if (signals->signal_event[SIGNAL_QUIT] == NULL || signals->signal_event[SIGNAL_TERM] == NULL) {
		return -1;
	}

	return 0;
}

void
serverSignalsFini(ServerSignals *signals)
{
	ServerSignal i;

	for (i = 0; i < SIGNAL_LENGTH; i++) {
		if (signals->signal_event[i] != NULL)
			CloseHandle(signals->signal_event[i]);
	}
}

int
serverSignalsLoop(ServerSignals *signals)
{
	int signal;

	switch (WaitForMultipleObjects(SIGNAL_LENGTH, signals->signal_event, 0, INFINITE)) {
	case WAIT_OBJECT_0 + SIGNAL_QUIT: signal = SIGQUIT; break;
	case WAIT_OBJECT_0 + SIGNAL_TERM: signal = SIGTERM; break;
	default: signal = SIGABRT; break;
	}

	syslog(LOG_INFO, "signal %d received", signal);

	return signal;
}
#endif /* __WIN32__ */

/***********************************************************************
 ***
 ***********************************************************************/

static int running;
static int server_quit;
static Queue queries_unused;
static Queue queries_waiting;
static ServerSignals signals;
static char *pid_file = PID_FILE;

static Socket2 *service;
static SocketAddress *service_addr;

static sqlite3 *db;
static sqlite3_stmt *db_select_one;

static void *
listen_thread(void *data)
{
	long length;
	DNS_query *query;

	while (running) {
		if (queueIsEmpty(&queries_unused)) {
			if ((query = malloc(sizeof (*query))) == NULL) {
				syslog(LOG_ERR, "out of memory");
				sleep(2);
				continue;
			}
		} else {
			query = queueDequeue(&queries_unused)->data;
		}

		do {
			length = socketReadFrom(service, (unsigned char *) &query->packet.header, UDP_PACKET_SIZE, &query->client);
			if (0 < debug && 0 < length) {
				char *from = socketAddressToString(&query->client);
				syslog(LOG_DEBUG, "from=%s length=%ld", from, length);
				free(from);
			}
		} while (length <= 0);

		query->node.data = query;
		query->packet.length = length;
		queueEnqueue(&queries_waiting, &query->node);
	}

	return NULL;
}

static int
sql_step(sqlite3 *db, sqlite3_stmt *sql_stmt)
{
	int rc;

	PTHREAD_DISABLE_CANCEL();

	/* Using the newer sqlite_prepare_v2() interface means that
	 * sqlite3_step() will return more detailed error codes. See
	 * sqlite3_step() API reference.
	 */
	while ((rc = sqlite3_step(sql_stmt)) == SQLITE_BUSY) {
		(void) sqlite3_sleep(1000);
	}

	if (rc != SQLITE_DONE && rc != SQLITE_ROW)
		syslog(LOG_ERR, "sql error %s", sqlite3_errmsg(db));

	if (rc != SQLITE_ROW) {
		/* http://www.sqlite.org/cvstrac/wiki?p=DatabaseIsLocked
		 *
		 * "Sometimes people think they have finished with a SELECT statement
		 *  because sqlite3_step() has returned SQLITE_DONE. But the SELECT is
		 *  not really complete until sqlite3_reset() or sqlite3_finalize()
		 *  have been called.
		 */
		(void) sqlite3_reset(sql_stmt);
	}
	PTHREAD_RESTORE_CANCEL();

	return rc;
}

static int
get_value(DNS_rr *query)
{
	int rc = -1;

	if (sqlite3_bind_int(db_select_one, 1, query->type) != SQLITE_OK)
		goto error0;

	if (sqlite3_bind_text(db_select_one, 2, (const char *) query->name, query->name_length, SQLITE_STATIC) != SQLITE_OK)
		goto error1;

	if (sql_step(db, db_select_one) != SQLITE_ROW)
		goto error1;

	if (0 < debug)
		syslog(LOG_DEBUG, "result columns=%d", sqlite3_column_count(db_select_one));

	query->data_length = sqlite3_column_bytes(db_select_one, 0);
	if (sizeof (query->data) < query->data_length)
		query->data_length = sizeof (query->data);

	memcpy(query->data, sqlite3_column_text(db_select_one, 0), query->data_length);
	query->data[query->data_length] = '\0';
	rc = 0;

	if (0 < debug)
		syslog(LOG_DEBUG, "found %d %lu:%s %lu:%s", query->type, (unsigned long) query->name_length, query->name, (unsigned long) query->data_length, query->data);

	(void) sqlite3_reset(db_select_one);
error1:
	(void) sqlite3_clear_bindings(db_select_one);
error0:
	return rc;
}

/*
 * Copy the string of the resource record name refered to by ptr
 * within the message into a buffer of size bytes. The name
 * labels are separated by dots and the final root label is added.
 * The terminating null character is appended.
 *
 * An error here would indicate process memory or stack corruption.
 */
static size_t
name_copy(DNS_packet *packet, unsigned char *ptr, unsigned char **stop, unsigned char *buf, size_t size)
{
	size_t remaining;
	unsigned short offset;
	unsigned char *packet_end, *buf0;

	packet_end = (unsigned char *) &packet->header + packet->length;

	if (ptr < (unsigned char *) &packet->header) {
		syslog(LOG_ERR, "name_copy() below bounds!!!");
		return 0;
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
			syslog(LOG_ERR, "name_copy() buffer overflow!!!");
			return 0;
		}

		(void) strncpy((char *) buf, (char *)(ptr+1), (size_t) *ptr);
		buf += *ptr;
		*buf++ = '.';

		remaining -= *ptr + 1;
		ptr += *ptr + 1;
	}

	if (packet_end <= ptr) {
		*buf = '\0';
		syslog(LOG_ERR, "name_copy() out of bounds!!! start of buf=\"%40s\"", buf0);
		return 0;
	}

	*buf = '\0';
	*stop = ++ptr;

	return (size - remaining);
}

static void
parse_query(DNS_query *query, DNS_rr *rr)
{
	long offset;
	unsigned char *ptr;

	ptr = query->packet.data;

	rr->name_length = name_copy(&query->packet, ptr, &ptr, rr->name, sizeof (rr->name));

	rr->type = networkGetShort(ptr);
	ptr += NET_SHORT_SIZE;

	rr->data_length = 0;

	if (0 < debug)
		syslog(LOG_DEBUG, "query %d %lu:%s", rr->type, (unsigned long) rr->name_length, rr->name);

	if (0 <= (offset = TextInsensitiveEndsWith((char *) rr->name, domain_suffix))) {
		rr->name_length = offset;
		rr->name[offset] = '\0';
	}
}

static size_t
data_length(DNS_rr *record)
{
	switch (record->type) {
	case DNS_TYPE_A:
		return 4;
	case DNS_TYPE_AAAA:
		return IPV6_BYTE_SIZE;
	case DNS_TYPE_TXT:
		if (255 < record->data_length)
			break;
		return record->data_length + 1;
	case DNS_TYPE_NULL:
		return record->data_length;
	default:
		break;
	}

	return ~0L;
}

static int
append_answer(DNS_query *query, DNS_rr *answer)
{
	size_t rdlength;
	unsigned offset;
	unsigned char *pkt, *eom;
	unsigned char ipv6[IPV6_BYTE_SIZE];

	rdlength = data_length(answer);
	if (UDP_PACKET_SIZE < query->packet.length + DNS_RR_MIN_LENGTH + rdlength)
		return -1;

	query->packet.header.bits = htons(BITS_QR | DNS_RCODE_OK);
	query->packet.header.ancount = ntohs(1);

	pkt = (unsigned char *) &query->packet.header;
	eom = pkt + query->packet.length;

	/* The answer has the same name as the query. Add pointer to query name. */
	offset = (unsigned char *) &query->packet.data - pkt;
	eom += networkSetShort(eom, 0xC000 | offset);

	eom += networkSetShort(eom, answer->type);
	eom += networkSetShort(eom, DNS_CLASS_IN);
	eom += networkSetLong(eom, 1L);
	eom += networkSetShort(eom, rdlength);

	switch (answer->type) {
	case DNS_TYPE_A:
		(void) parseIPv6((char *) answer->data, ipv6);
		memcpy(eom, ipv6+IPV6_OFFSET_IPV4, 4);
		break;

	case DNS_TYPE_AAAA:
/*** GH-7 TODO fix
 *** warning: ‘parseIPv6’ accessing 16 bytes in a region of size 12 [-Wstringop-overflow=]
 ***/
		(void) parseIPv6((char *) answer->data, eom);
		break;

	case DNS_TYPE_TXT:
		*eom++ = (unsigned char) answer->data_length;
		/*@fallthrough@*/

	case DNS_TYPE_NULL:
		if (UDP_PACKET_SIZE < eom - pkt + answer->data_length)
			return -1;
		memcpy(eom, answer->data, answer->data_length);
		break;

	default:
		return -1;
	}

	query->packet.length = eom - pkt + rdlength;

	return 0;
}

static void
set_no_answer(DNS_query *query)
{
	query->packet.header.bits = htons(BITS_QR | DNS_RCODE_NXDOMAIN);
}

static void
find_answer(DNS_query *query)
{
	DNS_rr query_rr;

	parse_query(query, &query_rr);
	if (get_value(&query_rr) || append_answer(query, &query_rr))
		set_no_answer(query);
}

static void *
answer_thread(void *data)
{
	DNS_query *query;

	while (running) {
		query = queueDequeue(&queries_waiting)->data;
		find_answer(query);
		socketWriteTo(service, (unsigned char *) &query->packet.header, query->packet.length, &query->client);
		queueEnqueue(&queries_unused, &query->node);
	}

	return NULL;
}

static int
sql_count(void *data, int ncolumns, char **col_values, char **col_names)
{
	(*(int*) data)++;
	return 0;
}

static int
create_database(sqlite3 *db)
{
	int count;
	char *error;

	count = 0;
	if (sqlite3_exec(db, SQL_TABLE_EXISTS, sql_count, &count, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql %s: %s", SQL_TABLE_EXISTS, error);
		sqlite3_free(error);
		return -1;
	}

	if (count != 1 && sqlite3_exec(db, SQL_CREATE_TABLE, NULL, NULL, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql %s: %s", SQL_CREATE_TABLE, error);
		sqlite3_free(error);
		return -1;
	}

	count = 0;
	if (sqlite3_exec(db, SQL_INDEX_EXISTS, sql_count, &count, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql %s: %s", SQL_INDEX_EXISTS, error);
		sqlite3_free(error);
		return -1;
	}

	if (count != 1 && sqlite3_exec(db, SQL_CREATE_INDEX, NULL, NULL, &error) != SQLITE_OK) {
		syslog(LOG_ERR, "sql %s: %s", SQL_CREATE_INDEX, error);
		sqlite3_free(error);
		return -1;
	}

	return 0;
}

int
serverMain(void)
{
	int rc, signal;
	pthread_t thread_listen, thread_answer;

	running = 1;
	signal = SIGTERM;
	rc = EX_SOFTWARE;

	if (!sqlite3_threadsafe()) {
		fprintf(stderr, "thread-safe sqlite3 required\n");
		goto error0;
	}

	if (pthreadInit()) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error0;
	}

	if ((service_addr = socketAddressNew("0.0.0.0", port)) == NULL) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error1;
	}

	if ((service = socketOpen(service_addr, 0)) == NULL) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error2;
	}

	if (socketSetReuse(service, 1)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error2;
	}

	if (socketBind(service, &service->address)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error2;
	}

	if (serverSignalsInit(&signals)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error3;
	}
	if (queueInit(&queries_unused)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error4;
	}
	if (queueInit(&queries_waiting)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error5;
	}

	if (sqlite3_open(database_path, &db) != SQLITE_OK) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error6;
	}
	if (create_database(db)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error7;
	}
	if (sqlite3_prepare_v2(db, SQL_SELECT_ONE, -1, &db_select_one, NULL) != SQLITE_OK) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error7;
	}
	if (0 < debug)
		syslog(LOG_DEBUG, "sql=\"%s\"", sqlite3_sql(db_select_one));

	if (pthread_create(&thread_answer, NULL, answer_thread, NULL)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error8;
	}

	if (pthread_create(&thread_listen, NULL, listen_thread, NULL)) {
		fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
		goto error9;
	}

	syslog(LOG_INFO, "ready");
	signal = serverSignalsLoop(&signals);
	syslog(LOG_INFO, "signal %d, terminating process", signal);

	running = 0;
	rc = EXIT_SUCCESS;

	(void) pthread_cancel(thread_answer);
	(void) pthread_join(thread_answer, NULL);
error9:
	(void) pthread_cancel(thread_listen);
	(void) pthread_join(thread_listen, NULL);
error8:
	sqlite3_finalize(db_select_one);
error7:
	sqlite3_close(db);
error6:
	queueFini(&queries_waiting);
error5:
	queueFini(&queries_unused);
error4:
	serverSignalsFini(&signals);
error3:
	socketClose(service);
error2:
	free(service_addr);
error1:
	pthreadFini();
error0:
	syslog(LOG_INFO, "signal %d, terminated", signal);

	return rc;
}

struct mapping {
	int code;
	char *name;
};

static struct mapping logFacilityMap[] = {
	{ LOG_AUTH,		"auth"		},
	{ LOG_CRON,		"cron" 		},
	{ LOG_DAEMON,		"daemon" 	},
#ifdef LOG_FTP
	{ LOG_FTP,		"ftp" 		},
#endif
	{ LOG_LPR,		"lpr"		},
	{ LOG_MAIL,		"mail"		},
	{ LOG_NEWS,		"news"		},
	{ LOG_UUCP,		"uucp"		},
	{ LOG_USER,		"user"		},
	{ LOG_LOCAL0,		"local0"	},
	{ LOG_LOCAL1,		"local1"	},
	{ LOG_LOCAL2,		"local2"	},
	{ LOG_LOCAL3,		"local3"	},
	{ LOG_LOCAL4,		"local4"	},
	{ LOG_LOCAL5,		"local5"	},
	{ LOG_LOCAL6,		"local6"	},
	{ LOG_LOCAL7,		"local7"	},
	{ 0, 			NULL 		}
};

int log_facility = LOG_DAEMON;

static int
name_to_code(struct mapping *map, const char *name)
{
	for ( ; map->name != NULL; map++) {
		if (TextInsensitiveCompare(name, map->name) == 0)
			return map->code;
	}

	return -1;
}

void
serverOptions(int argc, char **argv)
{
	int ch;

	optind = 1;
	while ((ch = getopt(argc, argv, "df:l:p:qs:vw:")) != -1) {
		switch (ch) {
		case 'd':
			daemon_mode = 0;
			break;

		case 'f':
			database_path = optarg;
			break;

		case 'l':
			log_facility = name_to_code(logFacilityMap, optarg);
			break;

		case 'p':
			port = (unsigned) strtol(optarg, NULL, 10);
			break;

		case 'q':
			server_quit++;
			break;

		case 's':
			domain_suffix = optarg;
			break;

		case 'v':
			debug++;
			break;

		case 'w':
			if (strcmp(optarg, "add") == 0 || strcmp(optarg, "remove") == 0) {
				windows_service = optarg;
				break;
			}
			/*@fallthrough@*/

		default:
			fprintf(stderr, usage_msg);
			exit(EX_USAGE);
		}
	}
}

/***********************************************************************
 *** Unix Daemon
 ***********************************************************************/
#ifdef __unix__

void
atExitCleanUp(void)
{
	closelog();
}

int
main(int argc, char **argv)
{
	serverOptions(argc, argv);

	switch (server_quit) {
	case 1:
		/* Slow quit	-q */
		exit(pidKill(pid_file, SIGQUIT) != 0);

	case 2:
		/* Quit now	-q -q */
		exit(pidKill(pid_file, SIGTERM) != 0);
	default:
		/* Restart	-q -q -q
		 * Restart-If	-q -q -q -q
		 */
		if (pidKill(pid_file, SIGTERM) && 3 < server_quit) {
			fprintf(stderr, "no previous instance running: %s (%d)\n", strerror(errno), errno);
			return EXIT_FAILURE;
		}

		sleep(2);
	}

	if (daemon_mode) {
		if (daemon(1, 1)) {
			fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
			return EX_SOFTWARE;
		}

		if (atexit(atExitCleanUp)) {
			fprintf(stderr, "%s(%d): %s\n", __FILE__, __LINE__, strerror(errno));
			return EX_SOFTWARE;
		}

		openlog("dnsd", LOG_PID|LOG_NDELAY, LOG_DAEMON);
	} else {
		LogOpen("(standard error)");
	}

	return serverMain();
}

#endif /* __unix__ */

# ifdef __WIN32__

#  include <com/snert/lib/sys/winService.h>

/***********************************************************************
 *** Windows Logging
 ***********************************************************************/

static HANDLE eventLog;

void
ReportInit(void)
{
	eventLog = RegisterEventSource(NULL, _NAME);
}

void
ReportLogV(int type, char *fmt, va_list args)
{
	LPCTSTR strings[1];
	char message[1024];

	strings[0] = message;
	(void) vsnprintf(message, sizeof (message), fmt, args);

	ReportEvent(
		eventLog,	// handle of event source
		type,		// event type
		0,		// event category
		0,		// event ID
		NULL,		// current user's SID
		1,		// strings in lpszStrings
		0,		// no bytes of raw data
		strings,	// array of error strings
		NULL		// no raw data
	);
}

void
ReportLog(int type, char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	ReportLogV(type, fmt, args);
	va_end(args);
}

static DWORD strerror_tls = TLS_OUT_OF_INDEXES;
static const char unknown_error[] = "(unknown error)";

char *
strerror(int error_code)
{
	char *error_string;

	if (strerror_tls == TLS_OUT_OF_INDEXES) {
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

void
freeThreadData(void)
{
	if (strerror_tls != TLS_OUT_OF_INDEXES) {
		char *error_string = (char *) TlsGetValue(strerror_tls);
		LocalFree(error_string);
	}
}

/***********************************************************************
 *** Windows Service
 ***********************************************************************/

int
main(int argc, char **argv)
{
	/* Get this now so we can use the event log. */
	ReportInit();

	serverOptions(argc, argv);

	if (0 < server_quit) {
		pid_t pid;
		int length;
		HANDLE signal_quit;
		char event_name[128];

		pid = pidLoad(pid_file);
		length = snprintf(event_name, sizeof (event_name), "Global\\%ld-%s", (long) pid, server_quit == 1 ? "QUIT" : "TERM");
		if (sizeof (event_name) <= length) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s pid file name too long", _NAME);
			return EX_SOFTWARE;
		}

		signal_quit = OpenEvent(EVENT_MODIFY_STATE , 0, event_name);
		if (signal_quit == NULL) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s quit error: %s (%d)", _NAME, strerror(errno), errno);
			return EX_OSERR;
		}

		SetEvent(signal_quit);
		CloseHandle(signal_quit);
		return EXIT_SUCCESS;
	}

	if (windows_service != NULL) {
		if (winServiceInstall(*windows_service == 'a', _NAME, NULL) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s %s error: %s (%d)", _NAME, windows_service, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

	openlog(_NAME, LOG_PID|LOG_NDELAY, log_facility);

	if (daemon_mode) {
		if (pidSave(pid_file)) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		if (pidLock(pid_file) < 0) {
			syslog(LOG_ERR, log_init, SERVER_FILE_LINENO, strerror(errno), errno);
			return EX_SOFTWARE;
		}

		winServiceSetSignals(&signals);

		if (winServiceStart(_NAME, argc, argv) < 0) {
			ReportLog(EVENTLOG_ERROR_TYPE, "service %s start error: %s (%d)", _NAME, strerror(errno), errno);
			return EX_OSERR;
		}
		return EXIT_SUCCESS;
	}

#ifdef NOT_USED
{
	long length;
	char *cwd, *backslash, *server_root, default_root[256];

	/* Get the absolute path of this executable and set the working
	 * directory to correspond to it so that we can find the options
	 * configuration file along side the executable, when running as
	 * a service. (I hate using the registry.)
	 */
	if ((length = GetModuleFileName(NULL, default_root, sizeof default_root)) == 0 || length == sizeof default_root) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to find default server root");
		return EXIT_FAILURE;
	}

	/* Strip off the executable filename, leaving its parent directory. */
	for (backslash = default_root+length; default_root < backslash && *backslash != '\\'; backslash--)
		;

	server_root = default_root;
	*backslash = '\0';

	/* Remember where we are in case we are running in application mode. */
	cwd = getcwd(NULL, 0);

	/* Change to the executable's directory for default configuration file. */
	if (chdir(server_root)) {
		ReportLog(EVENTLOG_ERROR_TYPE, "failed to change directory to '%s': %s (%d)\n", server_root, strerror(errno), errno);
		exit(EX_OSERR);
	}

	if (cwd != NULL) {
		(void) chdir(cwd);
		free(cwd);
	}
}
#endif

	return serverMain();
}

# endif /* __WIN32__ */

#else /* no HAVE_SQLITE3_H */

#include <stdio.h>

int
main(int argc, char **argv)
{
	printf("This program requires threaded SQLite3 support.\n");
	return 1;
}

#endif /* HAVE_SQLITE3_H */
