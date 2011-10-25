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

#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/util/Text.h>

extern int socket3_debug;

#ifdef HAVE_OPENSSL_SSL_H
static SSL_CTX *ssl_ctx;

static int
pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	return (int) TextCopy(buf, size, (char *)userdata);
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

static int
socket3_verify_cb(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
	/* Always continue the certification verification.
	 * Actual testing for failures will be handle later.
	 */
	return 1;
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

static int
socket3_log_tls(SSL *ssl, int result)
{
	int err;
	char msg[128];

	switch ((err = SSL_get_error(ssl, result))) {
	case SSL_ERROR_NONE:
		return result;

	case SSL_ERROR_ZERO_RETURN:
		return 0;
	}

	ERR_error_string_n(ERR_get_error(), msg, sizeof (msg));
	syslog(LOG_ERR, "ssl=%s errno=%d %s", ssl_error[err], errno, msg);

	return SOCKET_ERROR;
}
#endif

int
socket3_set_ca_certs(const char *cert_dir, const char *ca_chain)
{
#ifdef HAVE_OPENSSL_SSL_H
	if (!SSL_CTX_load_verify_locations(ssl_ctx, ca_chain, cert_dir))
		return SOCKET_ERROR;
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
		SSL_CTX_set_default_passwd_cb(ssl_ctx, pem_passwd_cb);
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

int
socket3_init_tls(void)
{
	static int initialised = 0;

	if (socket3_init())
		return SOCKET_ERROR;

	if (initialised)
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

	if ((ssl_ctx = SSL_CTX_new(SSLv23_method())) == NULL)
		return SOCKET_ERROR;

	/* Limit ourselves to SSLv3 and TLSv1. Note that UW IMAP
	 * requires TLSv1 on port 110 and SSLv2/SSLv3 on port 995.
	 */
	(void) SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2);

	/* Set default CA root certificate locations. */
	(void) socket3_set_ca_certs(CERT_DIR, CA_CHAIN);
}
#endif
	socket3_fini_hook = socket3_fini_tls;
	socket3_peek_hook = socket3_peek_tls;
	socket3_read_hook = socket3_read_tls;
	socket3_write_hook = socket3_write_tls;
	socket3_close_hook = socket3_close_tls;
	socket3_shutdown_hook = socket3_shutdown_tls;
	initialised = 1;

	return 0;
}

void
socket3_fini_tls(void)
{
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
	if (socket3_set_userdata(fd, ssl))
		goto error1;
	SSL_set_fd(ssl, fd);

	/*** See http://www.openssl.org/docs/ssl/SSL_CTX_set_verify.html
	 *** for optional client cert verification example
	 ***/
	if (is_server) {
		SSL_set_accept_state(ssl);
		if (1 < is_server)
			SSL_set_verify(ssl, SSL_VERIFY_PEER, socket3_verify_cb);
	} else {
		SSL_set_connect_state(ssl);
	}

	while ((err = SSL_do_handshake(ssl)) < 1) {
		if (SSL_get_error(ssl, err) != SSL_ERROR_WANT_READ) {
			(void) socket3_log_tls(ssl, err);
			goto error2;
		}
		if (!socket3_has_input(fd, ms))
			goto error2;
	}

	if (0 < socket3_debug) {
		int prv_bits, alg_bits;
		const char *name, *ver;
		SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);

		prv_bits = SSL_CIPHER_get_bits(cipher, &alg_bits);
		name = SSL_CIPHER_get_name(cipher);
		ver = SSL_CIPHER_get_version(cipher);

		syslog(LOG_DEBUG, "%s %s %d:%d", ver, name, prv_bits, alg_bits);
	}

	return 0;
error2:
	socket3_set_userdata(fd, NULL);
error1:
	SSL_free(ssl);
error0:
#endif
	return SOCKET_ERROR;
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

	return ssl != NULL && SSL_get_verify_result(ssl) == X509_V_OK;
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

	if (SSL_get_verify_result(ssl) != X509_V_OK) {
		syslog(LOG_ERR, "fd=%d peer certificate failed validation", (int) fd);
		return 0;
	}

	/* Check the common name (CN). */
	peer = SSL_get_peer_certificate(ssl);
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
		while ((ret = SSL_peek(ssl, buffer, (int) size)) < 1) {
			if (SSL_get_error(ssl, ret) != SSL_ERROR_WANT_READ)
				return socket3_log_tls(ssl, ret);
			if (!socket3_has_input(fd, SOCKET3_READ_TIMEOUT))
				return SOCKET_ERROR;
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
		while ((ret = SSL_read(ssl, buffer, (int) size)) < 1) {
			if (SSL_get_error(ssl, ret) != SSL_ERROR_WANT_READ)
				return socket3_log_tls(ssl, ret);
			if (!socket3_has_input(fd, SOCKET3_READ_TIMEOUT))
				return SOCKET_ERROR;
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
		while ((ret = SSL_write(ssl, buffer, (int) size)) < 1) {
			if (SSL_get_error(ssl, ret) != SSL_ERROR_WANT_WRITE)
				return socket3_log_tls(ssl, ret);
			if (!socket3_has_input(fd, SOCKET3_WRITE_TIMEOUT))
				return SOCKET_ERROR;
		}

		if (1 < socket3_debug)
			syslog(LOG_DEBUG, "%d = socket3_write_tls(%d, %lx, %ld, %lx)", ret, (int) fd, (unsigned long)buffer, (unsigned long)size, (unsigned long)to);

		return (long) ret;
	}
#endif
	return socket3_write_fd(fd, buffer, size, to);
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
		while ((ret = SSL_shutdown(ssl)) < 1) {
			switch (SSL_get_error(ssl, ret)) {
			case SSL_ERROR_WANT_READ:
				if (!socket3_has_input(fd, SOCKET3_READ_TIMEOUT))
					goto shutdown_timeout;
				break;
			case SSL_ERROR_WANT_WRITE:
				if (!socket3_can_send(fd, SOCKET3_WRITE_TIMEOUT))
					goto shutdown_timeout;
				break;
			}
		}
shutdown_timeout:
		rc = socket3_log_tls(ssl, ret);
		socket3_set_userdata(fd, NULL);
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
