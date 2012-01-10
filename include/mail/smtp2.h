/*
 * smtp2.h
 *
 * A simple SMTP engine.
 *
 * Copyright 2007, 2010 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_mail_smtp2_h__
#define __com_snert_lib_mail_smtp2_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <stdarg.h>
#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/mail/limits.h>

/***********************************************************************
 *** Macros & Constants
 ***********************************************************************/

/* Remove previous definitions. */
#ifdef __com_snert_lib_mail_smtp_h__
#undef SMTP_ERROR
#undef SMTP_ERROR_CONNECT
#undef SMTP_ERROR_TIMEOUT
#undef SMTP_ERROR_EOF
#undef SMTP_ERROR_IO

#undef SMTP_IS_ERROR
#undef SMTP_IS_OK
#undef SMTP_IS_DEFER
#undef SMTP_IS_TEMP
#undef SMTP_IS_PERM
#undef SMTP_IS_VALID

#undef SMTP_ISS_OK
#undef SMTP_ISS_DEFER
#undef SMTP_ISS_TEMP
#undef SMTP_ISS_PERM
#undef SMTP_ISS_VALID
#endif

#undef SMTP_OK
typedef enum {
	/*
	 * RFC 821, 2821, 5321 Reply Codes.
	 */
	SMTP_STATUS			= 211,
	SMTP_HELP			= 214,
	SMTP_WELCOME			= 220,
	SMTP_GOODBYE			= 221,
	SMTP_AUTH_OK			= 235,	/* RFC 4954 section 6 */
	SMTP_OK				= 250,
	SMTP_USER_NOT_LOCAL		= 251,

	SMTP_WAITING			= 354,

	SMTP_CLOSING			= 421,
	SMTP_AUTH_MECHANISM		= 432,	/* RFC 4954 section 6 */
	SMTP_BUSY			= 450,
	SMTP_TRY_AGAIN_LATER		= 451,
	SMTP_NO_STORAGE			= 452,
	SMTP_AUTH_TEMP			= 454,	/* RFC 4954 section 6 */

	SMTP_BAD_SYNTAX			= 500,
	SMTP_BAD_ARGUMENTS		= 501,
	SMTP_UNKNOWN_COMMAND		= 502,
	SMTP_BAD_SEQUENCE		= 503,
	SMTP_UNKNOWN_PARAM		= 504,
	SMTP_AUTH_REQUIRED		= 530,	/* RFC 4954 section 6 */
	SMTP_AUTH_WEAK			= 534,	/* RFC 4954 section 6 */
	SMTP_AUTH_FAIL			= 535,	/* RFC 4954 section 6 */
	SMTP_AUTH_ENCRYPT		= 538,	/* RFC 4954 section 6 */
	SMTP_REJECT			= 550,
	SMTP_UNKNOWN_USER		= 551,
	SMTP_OVER_QUOTA			= 552,
	SMTP_BAD_ADDRESS		= 553,
	SMTP_TRANSACTION_FAILED		= 554,

	/*
	 * Error conditions indicated like SMTP Reply Codes.
	 */
	SMTP_ERROR			= 100,
	SMTP_ERROR_CONNECT		= 110,
	SMTP_ERROR_TIMEOUT		= 120,
	SMTP_ERROR_EOF			= 130,
	SMTP_ERROR_IO			= 140,
} SMTP_Reply_Code;

#define SMTP_IS_ERROR(x)		(100 <= (x) && (x) < 200)
#define SMTP_IS_OK(x)			(200 <= (x) && (x) < 300)
#define SMTP_IS_DEFER(x)		(300 <= (x) && (x) < 400)
#define SMTP_IS_TEMP(x)			(400 <= (x) && (x) < 500)
#define SMTP_IS_PERM(x)			(500 <= (x) && (x) < 600)
#define SMTP_IS_VALID(x)		(200 <= (x) && (x) < 600)

#define SMTP_ISS_OK(x)			(*(x) == '2')
#define SMTP_ISS_DEFER(x)		(*(x) == '3')
#define SMTP_ISS_TEMP(x)		(*(x) == '4')
#define SMTP_ISS_PERM(x)		(*(x) == '5')
#define SMTP_ISS_VALID(x)		(strchr("2345", *(x)) != NULL)

/***********************************************************************
 *** SMTP Protocol API (multiple recipients, same destination)
 ***********************************************************************/

/* smtp2Open, mailOpen */
#define SMTP_FLAG_LOG			0x0001
#define SMTP_FLAG_DEBUG			0x0002
#define SMTP_FLAG_TRY_ALL		0x0004

/* Internal flags. */
#define SMTP_FLAG_SUBJECT		0x0010
#define SMTP_FLAG_FROM			0x0020
#define SMTP_FLAG_DATE			0x0040
#define SMTP_FLAG_MSGID			0x0080
#define SMTP_FLAG_EOH			0x0100
#define SMTP_FLAG_DATA			0x0200
#define SMTP_FLAG_EHLO			0x0400
#define SMTP_FLAG_ERROR			0x8000

typedef struct smtp2 {
	struct smtp2 *next;
	char id_string[20];		/* Session / Message ID */
	unsigned short id;		/* Session ID */
	unsigned connect_to;
	unsigned command_to;
	unsigned count;			/* Message count. */
	time_t start;			/* Session start time. */
	Socket2 *mx;
	int flags;
	int code;			/* Last SMTP response code. */
	char *domain;			/* Domain or host for connection. */
	char *sender;
	char local_ip[IPV6_STRING_LENGTH];
	char text[SMTP_TEXT_LINE_LENGTH+1];
} SMTP2;

extern SMTP2 *smtp2OpenMx(const char *domain, unsigned connect_ms, unsigned command_ms, int flags);
extern SMTP2 *smtp2Open(const char *host, unsigned connect_ms, unsigned command_ms, int flags);
extern void smtp2Close(void *_session);

extern SMTP_Reply_Code smtp2Auth(SMTP2 *session, const char *user, const char *pass);
extern SMTP_Reply_Code smtp2Mail(SMTP2 *session, const char *sender);
extern SMTP_Reply_Code smtp2Rcpt(SMTP2 *session, const char *recipient);
extern SMTP_Reply_Code smtp2Data(SMTP2 *session);
extern SMTP_Reply_Code smtp2Print(SMTP2 *session, const char *line, size_t length);
extern SMTP_Reply_Code smtp2PrintfV(SMTP2 *session, const char *fmt, va_list args);
extern SMTP_Reply_Code smtp2Printf(SMTP2 *session, const char *fmt, ...);
extern SMTP_Reply_Code smtp2Dot(SMTP2 *session);
extern SMTP_Reply_Code smtp2Noop(SMTP2 *session);
extern SMTP_Reply_Code smtp2Rset(SMTP2 *session);

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
 * @return
 *	An SMTP_ code.
 */
extern SMTP_Reply_Code smtp2Read(Socket2 *s, char ***lines);

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
 *	An SMTP_ code.
 */
extern SMTP_Reply_Code smtp2Write(Socket2 *s, char *line, size_t size);

/***********************************************************************
 *** Mail Message API (multiple recipients, multiple destinations)
 ***********************************************************************/

typedef struct {
	int flags;
	SMTP2 *list;
	char *sender;
	unsigned connect_to;
	unsigned command_to;
} Mail;

extern Mail *mailOpen(unsigned connect_ms, unsigned command_ms, int flags);
extern void mailClose(void *_mail);

extern SMTP_Reply_Code mailMail(Mail *mail, const char *sender);
extern SMTP_Reply_Code mailRcpt(Mail *mail, const char *recipient);
extern SMTP_Reply_Code mailData(Mail *mail);
extern SMTP_Reply_Code mailPrint(Mail *mail, const char *line, size_t length);
extern SMTP_Reply_Code mailPrintfV(Mail *mail, const char *fmt, va_list args);
extern SMTP_Reply_Code mailPrintf(Mail *mail, const char *fmt, ...);
extern SMTP_Reply_Code mailDot(Mail *mail);
extern SMTP_Reply_Code mailNoop(Mail *mail);
extern SMTP_Reply_Code mailRset(Mail *mail);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_mail_smtp2_h__ */
