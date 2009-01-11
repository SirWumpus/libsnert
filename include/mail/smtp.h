/*
 * smtp.h
 *
 * Copyright 2005, 2006 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_mail_smtp_h__
#define __com_snert_lib_mail_smtp_h__	1

#include <stdarg.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/limits.h>
#include <com/snert/lib/mail/parsePath.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SMTP_OK				250
#define SMTP_IS_OK(x)			(200 <= (x) && (x) < 300)
#define SMTP_IS_DEFER(x)		(300 <= (x) && (x) < 400)
#define SMTP_IS_TEMP(x)			(400 <= (x) && (x) < 500)
#define SMTP_IS_PERM(x)			(500 <= (x) && (x) < 600)
#define SMTP_IS_VALID(x)		(200 <= (x) && (x) < 600)

#define SMTP_ISS_OK(x)			(*x == '2')
#define SMTP_ISS_DEFER(x)		(*x == '3')
#define SMTP_ISS_TEMP(x)		(*x == '4')
#define SMTP_ISS_PERM(x)		(*x == '5')
#define SMTP_ISS_VALID(x)		(strchr("2345", *x) != NULL)

#define SMTP_ERROR_OK			0x0000
#define SMTP_ERROR_NULL			0x1000
#define SMTP_ERROR_EMPTY		0x2000
#define SMTP_ERROR_MEMORY		0x3000
#define SMTP_ERROR_INTERNAL		0x4000
#define SMTP_ERROR_INTERNAL_MASK	0xF000
#define SMTP_ERROR_INTERNAL_SHIFT	12

#define SMTP_ERROR_BUSY			0x0001	/* 421 at welcome */
#define SMTP_ERROR_SERVICE		0x0002	/* 554 at welcome */
#define SMTP_ERROR_ADDRESS		0x0003	/* address syntax error */
#define SMTP_ERROR_TEMPORARY		0x0004	/* 4xy */
#define SMTP_ERROR_REJECT		0x0005	/* 5xy */
#define SMTP_ERROR_RCPT			0x0005	/* 5xy */
#define SMTP_ERROR_MESSAGE		0x0006
#define SMTP_ERROR_GREY			0x0007
#define SMTP_ERROR_IP_BLOCKED		0x0008
#define SMTP_ERROR_ANY_RCPT		0x0009
#define SMTP_ERROR_SMTP			0x000A
#define SMTP_ERROR_SMTP_MASK		0x00FF
#define SMTP_ERROR_SMTP_SHIFT		0

#define SMTP_ERROR_CONNECT		0x0100
#define SMTP_ERROR_EOF			0x0200
#define SMTP_ERROR_READ			0x0300
#define SMTP_ERROR_WRITE		0x0400
#define SMTP_ERROR_TIMEOUT		0x0500
#define SMTP_ERROR_UNDERFLOW		0x0600
#define SMTP_ERROR_IO			0x0700
#define SMTP_ERROR_IO_MASK		0x0F00
#define SMTP_ERROR_IO_SHIFT		8

struct smtp_recipient {
	struct smtp_recipient *next;
	ParsePath *rcpt;		/* free */
};

struct smtp_connection {
	struct smtp_connection *next;
	struct smtp_recipient *head;	/* free list */
	int data_start;
	int smtp_error;
	char *domain;
	Socket2 *mx;			/* socketClose */
};

struct smtp_session {
	char id[16];
	long timeout;
	int smtp_error;
	ParsePath *mail;		/* free */
	char *smart_host;		/* free */
	time_t message_date;
	struct smtp_connection *head;	/* free list */
	char helo[SMTP_DOMAIN_LENGTH+1];
	char text[SMTP_TEXT_LINE_LENGTH+1];
	char line[SMTP_COMMAND_LINE_LENGTH+1];
};

typedef struct smtp_session SMTP;

extern void smtpSetDebug(int flag);
extern const char *smtpGetError(unsigned smtp_error);
extern size_t smtpAssertCRLF(char *line, size_t length, size_t size);
extern void smtpSetTimeout(SMTP *session, long ms);
extern long smtpGetTimeout(SMTP *session);
extern void smtpSetHelo(SMTP *session, const char *helo);
extern int smtpSetSmartHost(SMTP *session, const char *smart_host);
extern int smtpOpen(SMTP *session, const char *mail);
extern void smtpClose(SMTP *session);
extern int smtpAddRcpt(SMTP *session, const char *rcpt);
extern int smtpPrint(SMTP *session, const char *line, size_t length);
extern int smtpPrintfV(SMTP *session, const char *fmt, va_list args);
extern int smtpPrintf(SMTP *session, const char *fmt, ...);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param lines
 *	Used to pass back to the caller an array of pointers to C strings.
 *	The array is always terminated by a NULL pointer. Its the caller's
 *	responsibility to free() this array when done, which will also
 *	free its contents. On success there is at least one line.
 *
 * @param code
 *	A pointer to an integer used to return the SMTP response code.
 *	The value return is undefined in case of an error.
 *
 * @return
 *	NULL on success. Otherwise a pointer to a constant C string error
 *	message, in which case lines will be NULL.
 */
extern int smtpRead(Socket2 *s, char ***lines, int *code);

/**
 * @param s
 *	A Socket2 pointer returned by socketOpen() or socketAccept().
 *
 * @param line
 *	A pointer to a buffer to be write to the socket.
 *
 * @param size
 *	The length of the line buffer.
 *
 * @return
 *	NULL on success. Otherwise a pointer to a constant C string
 *	error message
 */
extern int smtpWrite(Socket2 *s, char *line, size_t size);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_smtp_h__ */
