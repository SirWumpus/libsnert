/*
 * socket3_tls.c
 *
 * Socket Portability API version 3 with SSL/TLS support
 *
 * Copyright 2011 by Anthony Howe. All rights reserved.
 */

#ifndef SOCKET3_READ_TIMEOUT
#define SOCKET3_READ_TIMEOUT	5000
#endif

#ifndef SOCKET3_WRITE_TIMEOUT
#define SOCKET3_WRITE_TIMEOUT	5000
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#ifdef HAVE_OPENSSL_SSL_H
# define OPENSSL_THREAD_DEFINES
# include <openssl/opensslconf.h>
# if OPENSSL_VERSION_NUMBER > 0x00907000L && !defined(OPENSSL_THREADS)
#  error "OpenSSL not built with thread support"
# endif
# include <openssl/ssl.h>
# include <openssl/bio.h>
# include <openssl/err.h>
# include <openssl/crypto.h>
#endif
#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif
#ifdef HAVE_SEMAPHORE_H
# include <semaphore.h>
#endif

#ifdef _REENTRANT
# include <com/snert/lib/sys/pthread.h>
#endif

#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/util/Text.h>

extern int socket3_debug;

/***********************************************************************
 *** Static support routines & call-backs
 ***********************************************************************/

#ifdef HAVE_OPENSSL_SSL_H
static SSL_CTX *ssl_ctx;

static int
socket3_pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	return (int) TextCopy(buf, size, (char *)userdata);
}

static int
socket3_verify_cb(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	/* Always continue the certification verification.
	 * Actual testing for failures will be handle later.
	 */
	return 1;
}

static sem_t *locks;

static void
socket3_lock_cb(int mode, int n, const char *file, int line)
{
	if (2 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_lock_cb(%d, %d, %s, %d)", mode, n, file, line);

	if (mode & CRYPTO_LOCK)
		(void) sem_wait(&locks[n]);
	else
		(void) sem_post(&locks[n]);
}

# ifdef _REENTRANT
#  if OPENSSL_VERSION_NUMBER < 0x00909000L
unsigned long
socket3_id_cb(void)
{
	return (unsigned long) pthread_self();
}
#  endif
# endif

struct CRYPTO_dynlock_value {
	sem_t lock;
};

static struct CRYPTO_dynlock_value *
socket3_dynlock_create_cb(const char *file, int line)
{
	struct CRYPTO_dynlock_value *dynlock;

	if (2 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_dynlock_create_cb(%s, %d)", file, line);

	if ((dynlock = malloc(sizeof (*dynlock))) != NULL)
		sem_init(&dynlock->lock, 0, 1);

	return dynlock;
}

static void
socket3_dynlock_free_cb(struct CRYPTO_dynlock_value *dynlock, const char *file, int line)
{
	if (2 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_dynlock_free_cb(%lx, %s, %d)", (unsigned long) dynlock, file, line);

	if (dynlock != NULL) {
		sem_destroy(&dynlock->lock);
		free(dynlock);
	}
}

static void
socket3_dynlock_lock_cb(int mode, struct CRYPTO_dynlock_value *dynlock, const char *file, int line)
{
	if (2 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_dynlock_lock_cb(%d, %lx, %s, %d)", mode, (unsigned long) dynlock, file, line);

	if (mode & CRYPTO_LOCK)
		(void) sem_wait(&dynlock->lock);
	else
		(void) sem_post(&dynlock->lock);
}

static char *ssl_error[] = {
	"SSL_ERROR_NONE",
	"SSL_ERROR_SSL",
	"SSL_ERROR_WANT_READ",
	"SSL_ERROR_WANT_WRITE",
	"SSL_ERROR_WANT_X509_LOOKUP",
	"SSL_ERROR_SYSCALL",
	"SSL_ERROR_ZERO_RETURN",
	"SSL_ERROR_WANT_CONNECT",
	"SSL_ERROR_WANT_ACCEPT",
};

/*
 * @return
 *	>= 0		success
 *	SOCKET_ERROR	an error
 *	-EAGAIN		repeat operation
 */
static int
socket3_check_io_state_tls(SSL *ssl, int code)
{
	unsigned long err_num;
	int err, fd = SSL_get_fd(ssl);
	char msg[SOCKET_ERROR_STRING_SIZE];

	switch (err = SSL_get_error(ssl, code)) {
	case SSL_ERROR_NONE:
	case SSL_ERROR_ZERO_RETURN:
		return code;

	case SSL_ERROR_WANT_READ:
		if (socket3_has_input(fd, SOCKET3_READ_TIMEOUT))
			return -EAGAIN;
		break;

	case SSL_ERROR_WANT_WRITE:
		if (socket3_can_send(fd, SOCKET3_WRITE_TIMEOUT))
			return -EAGAIN;
		break;
	}

	*msg = '\0';
	if (0 < (err_num = ERR_peek_error()))
		ERR_error_string_n(err_num, msg, sizeof (msg));
	syslog(LOG_ERR, "fd=%d errno=%d ssl=%s %s", SSL_get_fd(ssl), errno, ssl_error[err], msg);

	return SOCKET_ERROR;
}

typedef enum {
	X509_Issuer,
	X509_Subject,
} X509_Field;

static int
socket3_get_field(SOCKET fd, X509_Field field, char *buffer, size_t size)
{
	SSL *ssl;
	X509 *peer;
	int length;
	char *value;
	X509_NAME *name;

	if (buffer == NULL) {
		errno = EFAULT;
		return 0;
	}

	if (0 < size)
		*buffer = '\0';

	ssl = socket3_get_userdata(fd);
	if (ssl == NULL || (peer = SSL_get_peer_certificate(ssl)) == NULL) {
		errno = EINVAL;
		return 0;
	}

	switch (field) {
	case X509_Issuer: name = X509_get_issuer_name(peer); break;
	case X509_Subject: name = X509_get_subject_name(peer); break;
	}

	value = X509_NAME_oneline(name, NULL, 512);
	length = snprintf(buffer, size, "%s", value);
	free(value);

	return length;
}

#endif

/***********************************************************************
 *** Socket3 API
 ***********************************************************************/

int
socket3_set_ca_certs(const char *cert_dir, const char *ca_chain)
{
#ifdef HAVE_OPENSSL_SSL_H
	if (cert_dir == NULL || *cert_dir == '\0') {
		syslog(LOG_ERR, "CA certificate directory is undefined");
		return SOCKET_ERROR;
	}
	if (ca_chain == NULL || *ca_chain == '\0') {
		syslog(LOG_ERR, "CA certificate chain file is undefined");
		return SOCKET_ERROR;
	}
	if (!SSL_CTX_load_verify_locations(ssl_ctx, ca_chain, cert_dir)) {
		syslog(LOG_ERR, "failed to load CA root certificates");
		return SOCKET_ERROR;
	}
#endif
	return 0;
}

int
socket3_set_server_dh(const char *dh_pem)
{
#ifdef HAVE_OPENSSL_SSL_H
	DH *dh;
	BIO *bio;

	/* Applies for SSL/TLS servers only. */
	if (dh_pem == NULL || *dh_pem == '\0')
		return SOCKET_ERROR;

	if ((bio = BIO_new_file(dh_pem, "r")) == NULL)
		return SOCKET_ERROR;

	dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
	BIO_free(bio);

	if (!SSL_CTX_set_tmp_dh(ssl_ctx, dh))
		return SOCKET_ERROR;
#endif
	return 0;
}

static int
socket3_set_key(const char *key_pem, const char *key_pass)
{
#ifdef HAVE_OPENSSL_SSL_H
	if (key_pass != NULL && *key_pass != '\0') {
		SSL_CTX_set_default_passwd_cb(ssl_ctx, socket3_pem_passwd_cb);
		SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, (void *)key_pass);
	}

	if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, key_pem, SSL_FILETYPE_PEM))
		return SOCKET_ERROR;
#endif
	return 0;
}

int
socket3_set_cert_key(const char *cert_pem, const char *key_pem, const char *key_pass)
{
#ifdef HAVE_OPENSSL_SSL_H
	if (key_pem == NULL || *key_pem == '\0' || cert_pem == NULL || *cert_pem == '\0')
		return SOCKET_ERROR;

	if (!SSL_CTX_use_certificate_file(ssl_ctx, cert_pem, SSL_FILETYPE_PEM))
		return SOCKET_ERROR;
#endif
	return socket3_set_key(key_pem, key_pass);
}

int
socket3_set_cert_key_chain(const char *key_cert_pem, const char *key_pass)
{
#ifdef HAVE_OPENSSL_SSL_H
	if (key_cert_pem == NULL || *key_cert_pem == '\0')
		return SOCKET_ERROR;

	if (!SSL_CTX_use_certificate_chain_file(ssl_ctx, key_cert_pem))
		return SOCKET_ERROR;
#endif
	return socket3_set_key(key_cert_pem, key_pass);
}

static int socket3_initialised_tls = 0;
static unsigned char session_id_ctx[] = "libsnert-socket3";

int
socket3_init_tls(void)
{
	if (socket3_init())
		return SOCKET_ERROR;

	if (socket3_initialised_tls)
		return 0;

#ifdef HAVE_OPENSSL_SSL_H
{
	int i, n;

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	n = CRYPTO_num_locks();
	if ((locks = calloc(n, sizeof (*locks))) == NULL)
		return SOCKET_ERROR;

	for (i = 0; i < n; i++)
		sem_init(&locks[i], 0, 1);

	CRYPTO_set_locking_callback(socket3_lock_cb);

	CRYPTO_set_dynlock_create_callback(socket3_dynlock_create_cb);
	CRYPTO_set_dynlock_destroy_callback(socket3_dynlock_free_cb);
	CRYPTO_set_dynlock_lock_callback(socket3_dynlock_lock_cb);

# ifdef _REENTRANT
#  if OPENSSL_VERSION_NUMBER < 0x00909000L
	CRYPTO_set_id_callback(socket3_id_cb);
#  endif
# endif
	if ((ssl_ctx = SSL_CTX_new(SSLv23_method())) == NULL)
		return SOCKET_ERROR;

	/* Limit ourselves to SSLv3 and TLSv1. Note that UW IMAP
	 * requires TLSv1 on port 110 and SSLv2/SSLv3 on port 995.
	 */
	(void) SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2);

# if defined(CERT_DIR) && defined(CA_CHAIN)
	/* Set default CA root certificate locations. */
	(void) socket3_set_ca_certs(CERT_DIR, CA_CHAIN);
# endif
# if defined(DISABLE_SESS_CACHE)
	/* Disable session caching and force a full handshake each
	 * connection. Session caching would allow for session reuse
	 * and/or the passing of TLS sessions between applications
	 * via the external cache if the OpenSSL documentation makes
	 * any sense. Session caching is probably a bad idea as the
	 * session-id is passed in the clear.
	 *
	 * See
	 *
	 * SSL_CTX_set_session_cache_mode()
	 * SSL_CTX_set_session_id_context()
	 * SSL_CTX_set_generate_session_id()
	 * SSL_set_session_id_context()
	 */
	(void) SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_OFF);
# endif
	SSL_CTX_set_session_id_context(ssl_ctx, session_id_ctx, sizeof (session_id_ctx));
}
#endif
	socket3_fini_hook = socket3_fini_tls;
	socket3_peek_hook = socket3_peek_tls;
	socket3_read_hook = socket3_read_tls;
	socket3_wait_hook = socket3_wait_tls;
	socket3_write_hook = socket3_write_tls;
	socket3_close_hook = socket3_close_tls;
	socket3_shutdown_hook = socket3_shutdown_tls;

	socket3_initialised_tls++;

	return 0;
}

void
socket3_fini_tls(void)
{
	if (socket3_initialised_tls) {
		socket3_initialised_tls--;
#ifdef HAVE_OPENSSL_SSL_H
		if (0 < socket3_debug)
			syslog(LOG_DEBUG, "socket3_fini_tls()");
		SSL_CTX_free(ssl_ctx);
		ssl_ctx = NULL;
		EVP_cleanup();
		free(locks);
#endif
		socket3_fini_fd();
	}
}

int
socket3_get_valid_tls(SOCKET fd)
{
	int rc = 0;
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl != NULL) {
		rc = 1;
		if (SSL_get_peer_certificate(ssl) != NULL) {
			rc = 2;
			if (SSL_get_verify_result(ssl) == X509_V_OK)
				rc = 3;
		}
	}
#endif
	return rc;
}

int
socket3_get_cipher_tls(SOCKET fd, char *buffer, size_t size)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL_CIPHER *cipher;
	const char *name, *ver;
	int prv_bits, alg_bits, valid;
	SSL *ssl = socket3_get_userdata(fd);
	static char *cert_is_valid[] = { "N/A", "NONE", "FAIL", "PASS" };

	if (0 < (valid = socket3_get_valid_tls(fd))) {
 		cipher = SSL_get_current_cipher(ssl);
		prv_bits = SSL_CIPHER_get_bits(cipher, &alg_bits);
		name = SSL_CIPHER_get_name(cipher);
		ver = SSL_CIPHER_get_version(cipher);

		return snprintf(
			buffer, size, "%s cipher=%s bits=%d/%d valid=%s",
			ver, name, prv_bits, alg_bits, cert_is_valid[valid]
		);
	}

	return snprintf(buffer, size, "valid=%s", cert_is_valid[valid]);
#else
	return snprintf(buffer, size, "valid=N/A");
#endif
}

int
socket3_get_issuer_tls(SOCKET fd, char *buffer, size_t size)
{
#ifdef HAVE_OPENSSL_SSL_H
	return socket3_get_field(fd, X509_Issuer, buffer, size);
#else
	return 0;
#endif
}

int
socket3_get_subject_tls(SOCKET fd, char *buffer, size_t size)
{
#ifdef HAVE_OPENSSL_SSL_H
	return socket3_get_field(fd, X509_Subject, buffer, size);
#else
	return 0;
#endif
}

int
socket3_start_tls(SOCKET fd, int is_server, long ms)
{
#ifdef HAVE_OPENSSL_SSL_H
	int err;
	SSL *ssl;

	if (0 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_start_tls(%d, %d, %ld)", (int) fd, is_server, ms);

	if ((ssl = socket3_get_userdata(fd)) != NULL) {
		syslog(LOG_ERR, "fd=%d TLS already started", (int) fd);
		goto error0;
	}
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		goto error0;
	SSL_set_fd(ssl, fd);

	/*** See http://www.openssl.org/docs/ssl/SSL_CTX_set_verify.html
	 *** for optional client cert verification example
	 ***/
	if (is_server) {
		SSL_set_accept_state(ssl);
		if (1 < is_server)
# ifdef VERIFY_ONCE
			SSL_set_verify(ssl, SSL_VERIFY_PEER|SSL_VERIFY_CLIENT_ONCE, socket3_verify_cb);
# else
			SSL_set_verify(ssl, SSL_VERIFY_PEER, socket3_verify_cb);
# endif
	} else {
		SSL_set_connect_state(ssl);
	}

	while ((err = SSL_do_handshake(ssl)) < 1) {
		if (0 <= (err = socket3_check_io_state_tls(ssl, err)))
			break;
		if (err != -EAGAIN)
			goto error1;
	}

	/* Save ssl _after_ the handshake is done. This will stop
	 * socket3_check_io_state_tls() -> socket3_wait_tls() from
	 * calling SSL_pending() during the handshake and thus
	 * avoid the spurious SSL_UNDEFINED_CONST_FUNCTION error.
	 */
	if (socket3_set_userdata(fd, ssl))
		goto error1;

	if (0 < socket3_debug) {
		char cipher[SOCKET_CIPHER_STRING_SIZE];
		(void) socket3_get_cipher_tls(fd, cipher, sizeof (cipher));
		syslog(LOG_DEBUG, "fd=%d %s", (int) fd, cipher);
	}

	return 0;
error1:
	SSL_free(ssl);
error0:
#endif
	return SOCKET_ERROR;
}

void
socket3_get_error_tls(SOCKET fd, char *buffer, size_t size)
{
	if (buffer != NULL && 0 < size)
		*buffer = '\0';

	if (9 < size) {
		int length = snprintf(buffer, size, "fd=%d ", (int) fd);
		buffer += length;
		size -= length;
	}

#ifdef HAVE_OPENSSL_SSL_H
{
	unsigned long err_num;
	if (0 < (err_num = ERR_get_error())) {
		ERR_error_string_n(err_num, buffer, size);
		ERR_clear_error();
	} else if (errno != 0) {
		(void) snprintf(buffer, size, "errno=%d %s", errno, strerror(errno));
	}
}
#endif
}

int
socket3_set_sess_id_ctx(SOCKET fd, unsigned char *id, size_t length)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);
	if (SSL_MAX_SSL_SESSION_ID_LENGTH < length)
		length = SSL_MAX_SSL_SESSION_ID_LENGTH;
	if (ssl == NULL || !SSL_set_session_id_context(ssl, id, length))
		return SOCKET_ERROR;
#endif
	return 0;
}


int
socket3_is_tls(SOCKET fd)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);

	return ssl != NULL && SSL_get_current_cipher(ssl) != NULL;
#else
	return 0;
#endif
}

int
socket3_is_peer_ok(SOCKET fd)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);

	return ssl != NULL
	&& SSL_get_peer_certificate(ssl) != NULL
	&& SSL_get_verify_result(ssl) == X509_V_OK;
#else
	return 0;
#endif
}

int
socket3_is_cn_tls(SOCKET fd, const char *expect_cn)
{
#ifdef HAVE_OPENSSL_SSL_H
	X509 *peer;
	char peer_CN[256];
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl == NULL) {
		syslog(LOG_WARNING, "fd=%d no SSL/TLS connection", (int) fd);
		return 0;
	}
	if ((peer = SSL_get_peer_certificate(ssl)) == NULL) {
		syslog(LOG_ERR, "fd=%d peer certificate missing", (int) fd);
		return 0;
	}
	if (SSL_get_verify_result(ssl) != X509_V_OK) {
		syslog(LOG_ERR, "fd=%d peer certificate failed validation", (int) fd);
		return 0;
	}

	/* Check the common name (CN). */
	X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, peer_CN, sizeof (peer_CN));

	if (!TextMatch(peer_CN, expect_cn, -1, 0)) {
		syslog(LOG_ERR, "fd=%d invalid CN; cn=%s expected=%s", (int) fd, peer_CN, expect_cn);
		return 0;
	}

	return 1;
#else
		syslog(LOG_WARNING, "fd=%d no SSL/TLS connection", (int) fd);
	return 0;
#endif
}

long
socket3_peek_tls(SOCKET fd, unsigned char *buffer, long size, SocketAddress *from)
{
#ifdef HAVE_OPENSSL_SSL_H
	int ret;
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl != NULL && from == NULL) {
		ERR_clear_error();
		while ((ret = SSL_peek(ssl, buffer, (int) size)) < 1) {
			if ((ret = socket3_check_io_state_tls(ssl, ret)) != -EAGAIN)
				break;
		}

		if (1 < socket3_debug)
			syslog(LOG_DEBUG, "%d = socket3_peek_tls(%d, %lx, %ld, %lx)", ret, (int) fd, (unsigned long)buffer, (unsigned long)size, (unsigned long)from);

		return (long) ret;
	}
#endif
	return socket3_peek_fd(fd, buffer, size, from);
}

long
socket3_read_tls(SOCKET fd, unsigned char *buffer, long size, SocketAddress *from)
{
#ifdef HAVE_OPENSSL_SSL_H
	int ret;
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl != NULL && from == NULL) {
		ERR_clear_error();
		while ((ret = SSL_read(ssl, buffer, (int) size)) < 1) {
			if ((ret = socket3_check_io_state_tls(ssl, ret)) != -EAGAIN)
				break;
		}

		if (1 < socket3_debug)
			syslog(LOG_DEBUG, "%d = socket3_read_tls(%d, %lx, %ld, %lx)", ret, (int) fd, (unsigned long)buffer, (unsigned long)size, (unsigned long)from);

		return (long) ret;
	}
#endif
	return socket3_read_fd(fd, buffer, size, from);
}

long
socket3_write_tls(SOCKET fd, unsigned char *buffer, long size, SocketAddress *to)
{
#ifdef HAVE_OPENSSL_SSL_H
	int ret;
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl != NULL && to == NULL) {
		ERR_clear_error();
		while ((ret = SSL_write(ssl, buffer, (int) size)) < 1) {
			if ((ret = socket3_check_io_state_tls(ssl, ret)) != -EAGAIN)
				break;
		}

		if (1 < socket3_debug)
			syslog(LOG_DEBUG, "%d = socket3_write_tls(%d, %lx, %ld, %lx)", ret, (int) fd, (unsigned long)buffer, (unsigned long)size, (unsigned long)to);

		return (long) ret;
	}
#endif
	return socket3_write_fd(fd, buffer, size, to);
}

int
socket3_wait_tls(SOCKET fd, long timeout, unsigned rw_flags)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);

	if (0 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_wait_tls(%d, %ld, %u)", (int) fd, timeout, rw_flags);

	if (ssl != NULL && (rw_flags & SOCKET_WAIT_READ) && 0 < SSL_pending(ssl))
		return errno = 0;
# ifdef HMM
{
	int ret;
	unsigned char octet;

	while ((ret = SSL_want(ssl)) == SSL_ERROR_WANT_READ || ret == SSL_ERROR_WANT_WRITE) {
		(void) SSL_peek(ssl, &octet, sizeof (octet));
	}
}
# endif
#endif
	return socket3_wait_fd(fd, timeout, rw_flags);
}

int
socket3_end_tls(SOCKET fd)
{
#ifdef HAVE_OPENSSL_SSL_H
	int ret, rc;
	SSL *ssl = socket3_get_userdata(fd);

	if (0 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_end_tls(%d)", (int) fd);

	if (ssl != NULL) {
		socket3_set_userdata(fd, NULL);
		while ((ret = SSL_shutdown(ssl)) < 1) {
			if ((rc = socket3_check_io_state_tls(ssl, ret)) != -EAGAIN)
				break;
		}
		SSL_free(ssl);
		return rc;
	}
#endif
	return 0;
}

int
socket3_shutdown_tls(SOCKET fd, int shut)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);

	if (0 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_shutdown_tls(%d, %d)", (int) fd, shut);

	if (ssl != NULL) {
		socket3_set_userdata(fd, NULL);
		(void) SSL_shutdown(ssl);
		SSL_free(ssl);
	}
#endif
	return socket3_shutdown_fd(fd, shut);
}

void
socket3_close_tls(SOCKET fd)
{
	if (0 < socket3_debug)
		syslog(LOG_DEBUG, "socket3_close_tls(%d)", (int) fd);
	socket3_close_fd(fd);
}
