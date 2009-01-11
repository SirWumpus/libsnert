/***********************************************************************
 *** THE API BELOW THIS POINT MAY CHANGE OR GO AWAY.
 ***********************************************************************/

extern void smtpTimeoutSet(long ms);
extern const char *smtpPrintLine(Socket2 *s, char *line);
extern const char *smtpGetResponse(Socket2 *s, char *line, long size, int *code);

typedef struct {
	const char *mx;		/* SMTP host to connect to, MX lookup if NULL. */
	const char *helo;	/* FQDN for HELO argument, gethostname() if NULL */
	const char *mail;	/* MAIL FROM:<%s> */
	const char *rcpt;	/* RCPT TO:<%s> */
	const char *this_ip;	/* Public IP of this host. */
	const char *error;	/* NULL on success, otherwise a smtpError string. */
	int code;		/* The last SMTP status code received. */
	int using_internal_mx;	/* True if isReservedIP() and hasValidTLD() tests
				 * should be skipped.
				 */
} smtpMessage;

/**
 * @param ctx
 *	A pointer to an smtpMessage context. ctx->mx cannot be NULL.
 *
 * @param buffer
 *	A C string to send.
 *
 * @return
 *	NULL on success; otherwise an error string, see smtpError* constants.
 */
extern const char *smtpSendString(smtpMessage *ctx, const char *buffer);

/**
 * @param ctx
 *	A pointer to an smtpMessage context. If the ctx->mx field is NULL,
 *	then an MX lookup is performed for the recipient's domain.
 *
 * @param args
 *	A va_list of arguments for the message.
 *
 * @return
 *	NULL on success; otherwise an error string, see smtpError* constants.
 */
extern const char *smtpSendMessageV(smtpMessage *ctx, const char *msg, va_list args);

/**
 * @param ctx
 *	A pointer to an smtpMessage context. If the ctx->mx field is NULL,
 *	then an MX lookup is performed for the recipient's domain.
 *
 * @param ...
 *	A list of printf() arguments for the message.
 *
 * @return
 *	NULL on success; otherwise an error string, see smtpError* constants.
 */
extern const char *smtpSendMessage(smtpMessage *ctx, const char *msg, ...);

/**
 * @param mx
 *	The SMTP server to connect to. If NULL, then an MX lookup is
 *	performed for the recipient's domain.
 *
 * @param mail
 *	The sender's mail address. If empty, then its the DSN address.
 *
 * @param rcpt
 *	The recipient's mail address.
 *
 * @param msg
 *	The message printf() format string.
 *
 * @param args
 *	A va_list of arguments for the message.
 *
 * @return
 *	NULL on success; otherwise an error string, see smtpError* constants.
 */
extern const char *smtpSendMailV(const char *mx, const char *mail, const char *rcpt, const char *msg, va_list args);

/**
 * @param mx
 *	The SMTP server to connect to. If NULL, then an MX lookup is
 *	performed for the recipient's domain.
 *
 * @param mail
 *	The sender's mail address. If empty, then its the DSN address.
 *
 * @param rcpt
 *	The recipient's mail address.
 *
 * @param msg
 *	The message printf() format string.
 *
 * @param ...
 *	A list of printf() arguments for the message.
 *
 * @return
 *	NULL on success; otherwise an error string, see smtpError* constants.
 */
extern const char *smtpSendMail(const char *mx, const char *mail, const char *rcpt, const char *msg, ...);
