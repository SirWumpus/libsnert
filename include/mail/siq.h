/*
 * siq.h
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_mail_siq_h__
#define __com_snert_lib_mail_siq_h__	1

#include <stdarg.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SIQ UDP Query Format (draft 02)
 *                                        1  1  1  1  1  1
 *          0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +0   |        VERSION        |HL|ML|   RESERVED   |QT|
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +2   |                      ID                       |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +4   |                                               |
 *        /                     IPv6                      /
 *        /                                               /
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
 *  +20   |       QD-LENGTH       |     EXTRA-LENGTH      |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
 *  +22   |                                               |
 *        /                      QD                       /
 *        /                                               /
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *        |                                               |
 *        |                    EXTRA-ID                   |
 *        |                                               |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *        |                                               |
 *        /                     EXTRA                     /
 *        /                                               /
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * +512
 *
 *
 * HL	Equals 1 if the HELO argument is an FQDN that
 *	resolves to the connecting client IP.
 *
 * ML	Equals 1 if the MAIL FROM base domain is a
 *	suffix of the HELO argument base domain.
 */

#define QUERY_VERSION			0
#define QUERY_FLAGS			1
#define QUERY_ID			2
#define QUERY_IP			4
#define QUERY_QD_LENGTH			20
#define QUERY_EXTRA_LENGTH		21
#define QUERY_QD			22


/* SIQ UDP Response Format (draft 02)
 *
 *                                        1  1  1  1  1  1
 *          0  1  2  3  4  5  6  7  8  9  0  1  2  3  4  5
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +0   |        VERSION        |         SCORE         |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +2   |                      ID                       |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +4   |       IP-SCORE        |     DOMAIN-SCORE      |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +6   |       REL-SCORE       |      TEXT-LENGTH      |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *   +8   |                      TTL                      |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *  +10   |       CONFIDENCE      |     EXTRA-LENGTH      |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
 *  +12   |                                               |
 *        /                     TEXT                      /
 *        /                                               /
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *        |                                               |
 *        |                    EXTRA-ID                   |
 *        |                                               |
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--|
 *        |                                               |
 *        /                     EXTRA                     /
 *        /                                               /
 *        +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * +512 max.
 */

#define RESPONSE_VERSION		0
#define RESPONSE_SCORE			1
#define RESPONSE_ID			2
#define RESPONSE_IP_SCORE		4
#define RESPONSE_DOMAIN_SCORE		5
#define RESPONSE_REL_SCORE		6
#define RESPONSE_TEXT_LENGTH		7
#define RESPONSE_TTL			8
#define RESPONSE_CONFIDENCE		10
#define RESPONSE_EXTRA_LENGTH		11
#define RESPONSE_TEXT			12

#define RESPONSE_TEXT_00		8	/* draft 00 */

#define RESPONSE_CONFIDENCE_01		12	/* draft 01 */
#define RESPONSE_EXTRA_LENGTH_01	13	/* draft 01 */
#define RESPONSE_TEXT_01		14	/* draft 01 */

#define RESPONSE_ERROR			(-4)
#define RESPONSE_REDIRECT		(-3)
#define RESPONSE_TEMPFAIL		(-2)
#define RESPONSE_UNKNOWN		(-1)

typedef struct {
	time_t expires;			/* Response timestamp + TTL */
	int hl;				/* Query experimental flag */
	int ml;				/* Query experimental flag */
	int score;
	int score_ip;
	int score_rel;
	int score_domain;
	int confidence;
	int text_length;
	int extra_length;
	unsigned ttl;
	char text[256];
	char extra[256];
} SIQ;

extern const char siqErrorOpen[];
extern const char siqErrorNullArgument[];
extern const char siqErrorEmptyArgument[];
extern const char siqErrorNoServers[];
extern const char siqErrorDomainTooLong[];
extern const char siqErrorReadTimeout[];
extern const char siqErrorVersionMismatch[];
extern const char siqErrorIdMismatch[];
extern const char siqErrorRead[];

extern int isHeloMailSimilar(char *helo, char *domain);
extern int isClientIpHeloEqual(char *client_addr, char *helo);
extern const char *siqGetScore(SIQ *siq, char *client_addr, char *helo, char *mail_domain, ...);
extern const char *siqGetScoreA(SIQ *siq, char *client_addr, char *helo, char *mail_domain, char **servers);
extern const char *siqGetScoreV(SIQ *siq, char *client_addr, char *helo, char *mail_domain, va_list servers);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_siq_h__ */
