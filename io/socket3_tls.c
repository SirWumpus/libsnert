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
# include <openssl/ssl.h>
# include <openssl/bio.h>
# include <openssl/err.h>
#endif
#if defined(HAVE_SYSLOG_H) && ! defined(__MINGW32__)
# include <syslog.h>
#endif

#include <com/snert/lib/io/socket3.h>
#include <com/snert/lib/util/Text.h>

#ifdef HAVE_OPENSSL_SSL_H
static SSL_CTX *ssl_ctx;

static int
pem_passwd_cb(char *buf, int size, int rwflag, void *userdata)
{
	return (int) TextCopy(buf, size, (char *)userdata);
}
#endif

int
socket3_init_tls(
	const char *ca_pem_dir, const char *ca_pem_chain,
	const char *key_crt_pem, const char *key_pass,
	const char *dh_pem)
{
	static int initialised = 0;

	if (socket3_init())
		goto error0;

	if (initialised)
		return 0;

#ifdef HAVE_OPENSSL_SSL_H
	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	if ((ssl_ctx = SSL_CTX_new(SSLv23_method())) == NULL)
		goto error0;

	/* Limit ourselves to SSLv3 and TLSv1. Note that UW IMAP
	 * requires TLSv1 on port 110 and SSLv2/SSLv3 on port 995.
	 */
	(void) SSL_CTX_set_options(ssl_ctx, SSL_OP_NO_SSLv2);

	if (key_crt_pem != NULL) {
		if (!SSL_CTX_use_certificate_chain_file(ssl_ctx, key_crt_pem))
			goto error1;
		if (key_pass != NULL) {
			SSL_CTX_set_default_passwd_cb(ssl_ctx, pem_passwd_cb);
			SSL_CTX_set_default_passwd_cb_userdata(ssl_ctx, (void *)key_pass);
		}
		if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, key_crt_pem, SSL_FILETYPE_PEM))
			goto error1;
	}

	if ((ca_pem_chain != NULL || ca_pem_dir != NULL)
	&& !SSL_CTX_load_verify_locations(ssl_ctx, ca_pem_chain, ca_pem_dir))
		goto error1;

	/* Applies for SSL/TLS servers only. */
	if (dh_pem != NULL) {
		DH *dh;
		BIO *bio;

		if ((bio = BIO_new_file(dh_pem, "r")) == NULL)
			goto error1;
		dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
		BIO_free(bio);
		if (!SSL_CTX_set_tmp_dh(ssl_ctx, dh))
			goto error1;
	}
#endif
	socket3_fini_hook = socket3_fini_tls;
	socket3_peek_hook = socket3_peek_tls;
	socket3_read_hook = socket3_read_tls;
	socket3_write_hook = socket3_write_tls;
	socket3_close_hook = socket3_close_tls;
	initialised = 1;

	return 0;
#ifdef HAVE_OPENSSL_SSL_H
error1:
	SSL_CTX_free(ssl_ctx);
	ssl_ctx = NULL;
#endif
error0:
	return SOCKET_ERROR;
}

void
socket3_fini_tls(void)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL_CTX_free(ssl_ctx);
	ssl_ctx = NULL;
	EVP_cleanup();
#endif
	socket3_fini_fd();
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

	switch ((err = SSL_get_error(ssl, result))) {
	case SSL_ERROR_NONE:
		return result;

	case SSL_ERROR_ZERO_RETURN:
		return SOCKET_EOF;
	}

	syslog(LOG_ERR, "ssl=%s errno=%d", ssl_error[err], errno);

	return SOCKET_ERROR;
}

int
socket3_start_tls(SOCKET fd, int is_server, long ms)
{
#ifdef HAVE_OPENSSL_SSL_H
	int err;
	SSL *ssl;

	if (fd < 0)
		goto error0;
	if ((ssl = socket3_get_userdata(fd)) != NULL)
		goto error0;
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		goto error0;
	if (socket3_set_userdata(fd, ssl))
		goto error1;
	SSL_set_fd(ssl, fd);

	if (is_server)
		SSL_set_accept_state(ssl);
	else
		SSL_set_connect_state(ssl);

	while ((err = SSL_do_handshake(ssl)) < 1) {
		if (SSL_get_error(ssl, err) != SSL_ERROR_WANT_READ)
			goto error2;
		if (socket3_has_input(fd, ms) != 0)
			goto error2;
	}

	return 0;
error2:
	(void) socket3_log_tls(ssl, err);
	socket3_set_userdata(fd, NULL);
error1:
	SSL_free(ssl);
error0:
#endif
	return SOCKET_ERROR;
}

int
socket3_is_cn_tls(SOCKET fd, const char *expect_cn)
{
#ifdef HAVE_OPENSSL_SSL_H
	X509 *peer;
	char peer_CN[256];
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl == NULL) {
		syslog(LOG_WARNING, "no SSL/TLS connection");
		return 1;
	}

	if (SSL_get_verify_result(ssl) != X509_V_OK) {
		syslog(LOG_ERR, "certificate failed validation");
		return 0;
	}

	/* Check the common name (CN). */
	peer = SSL_get_peer_certificate(ssl);
	X509_NAME_get_text_by_NID(X509_get_subject_name(peer), NID_commonName, peer_CN, sizeof (peer_CN));

	if (TextInsensitiveCompare(peer_CN, expect_cn) != 0) {
		syslog(LOG_ERR, "invalid CN; cn=%s expected=%s", peer_CN, expect_cn);
		return 0;
	}

	return 1;
#else
	syslog(LOG_WARNING, "no SSL/TLS connection");
	return 1;
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
				break;
			if (socket3_has_input(fd, SOCKET3_READ_TIMEOUT) != 0)
				break;
		}

		return socket3_log_tls(ssl, ret);
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
				break;
			if (socket3_has_input(fd, SOCKET3_READ_TIMEOUT) != 0)
				break;
		}

		return socket3_log_tls(ssl, ret);
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
				break;
			if (socket3_can_send(fd, SOCKET3_WRITE_TIMEOUT) != 0)
				break;
		}

		return socket3_log_tls(ssl, ret);
	}
#endif
	return socket3_write_fd(fd, buffer, size, to);
}

void
socket3_close_tls(SOCKET fd)
{
#ifdef HAVE_OPENSSL_SSL_H
	SSL *ssl = socket3_get_userdata(fd);

	if (ssl != NULL) {
		socket3_set_userdata(fd, NULL);
		(void) SSL_shutdown(ssl);
		SSL_free(ssl);
	}
#endif
	socket3_close_fd(fd);
}
