/*
 * kvm.c
 *
 * Key-Value Map
 *
 * Copyright 2002, 2009 by Anthony Howe. All rights reserved.
 */

#define _VERSION		"0.3"

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#ifndef __MINGW32__
# if defined(HAVE_GRP_H)
#  include <grp.h>
# endif
# if defined(HAVE_PWD_H)
#  include <pwd.h>
# endif
# if defined(HAVE_SYSLOG_H)
#  include <syslog.h>
# endif
# if defined(HAVE_SYS_WAIT_H)
#  include <sys/wait.h>
# endif
#else
extern unsigned int sleep(unsigned int);
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/io/socket2.h>
#include <com/snert/lib/berkeley_db.h>
#include <com/snert/lib/crc/Crc.h>
#include <com/snert/lib/type/kvm.h>
#include <com/snert/lib/io/Log.h>
#include <com/snert/lib/io/file.h>
#include <com/snert/lib/sys/pthread.h>
#include <com/snert/lib/util/Text.h>

#define KVM_EOF				(-3)

static int debug;

static void
kvm_sync_stub(kvm *self)
{
	/* Do nothing. */
}

static int
kvm_truncate_stub(kvm *self)
{
	/* Do nothing. */
	return KVM_NOT_IMPLEMETED;
}

static const char *
kvm_filepath_stub(kvm *self)
{
	/* Do nothing. */
	return NULL;
}

void
kvmAtForkPrepare(kvm *self)
{
#if defined(HAVE_PTHREAD_CREATE)
	if (self != NULL)
		(void) pthread_mutex_lock(&self->_mutex);
#endif
}

void
kvmAtForkParent(kvm *self)
{
#if defined(HAVE_PTHREAD_CREATE)
	if (self != NULL)
		(void) pthread_mutex_unlock(&self->_mutex);
#endif
}

void
kvmAtForkChild(kvm *self)
{
#if defined(HAVE_PTHREAD_CREATE)
	if (self != NULL)
		(void) pthread_mutex_unlock(&self->_mutex);
#endif
}

static void
kvmClose(kvm *self)
{
	if (self != NULL) {
#if defined(HAVE_PTHREAD_CREATE)
		(void) pthread_mutex_destroy(&self->_mutex);
#endif
		free(self->_location);
		free(self->_table);
		free(self);
	}
}

static kvm *
kvmCreate(const char *table_name, const char *map_location, int mode)
{
	kvm *self;

	if ((self = calloc(1, sizeof (*self))) == NULL)
		goto error0;

#if defined(HAVE_PTHREAD_CREATE)
	if (pthread_mutex_init(&self->_mutex, NULL))
		goto error1;
#endif
	if ((self->_table = strdup(table_name)) == NULL)
		goto error1;

	if (map_location == NULL)
		map_location = "hash";

	if ((self->_location = strdup(map_location)) == NULL)
		goto error1;

	return self;
error1:
	if (self->close != NULL)
		(*self->close)(self);
	else
		kvmClose(self);
error0:
	return NULL;
}

static int
kvm_put_stub(struct kvm *self, kvm_data *key, kvm_data *value)
{
	syslog(LOG_WARNING, "kvm=%s PUT denied, read-only access", self->_table);
	return KVM_ERROR;
}

static int
kvm_remove_stub(struct kvm *self, kvm_data *key)
{
	syslog(LOG_WARNING, "kvm=%s REMOVE denied, read-only access", self->_table);
	return KVM_ERROR;
}

static int
kvm_begin_stub(kvm *self)
{
	/* Do nothing. */
	return KVM_NOT_IMPLEMETED;
}

static int
kvm_commit_stub(kvm *self)
{
	/* Do nothing. */
	return KVM_NOT_IMPLEMETED;
}

static int
kvm_rollback_stub(kvm *self)
{
	/* Do nothing. */
	return KVM_NOT_IMPLEMETED;
}

/***********************************************************************
 *** Hash Table
 ***********************************************************************/

#ifndef HASH_FN
#define HASH_FN			2
#endif

typedef struct kvm_hash {
	kvm_data key;
	kvm_data value;
	struct kvm_hash *next;
} kvm_hash;

typedef struct {
	kvm_hash **table;
	kvm_hash *cursor_next;
	unsigned long cursor_index;
} kvm_hash_table;

#if HASH_FN == 1

/*
 * The size of the hash table should be a small prime number:
 *
 *	449, 509, 673, 991, 997, 1021, 2039, 4093, 8191
 *
 * Using a prime number for the table size means that double
 * hashing or linear-probing can visit all possible entries.
 *
 * This is NOT a runtime option, because its not something I
 * want people to play with unless absolutely necessary.
 */
# define TABLE_SIZE		4093

/*
 * Hash using POSIX 32-bit CRC.
 */
unsigned long
hash_index(unsigned char *buffer, unsigned long size)
{
	return hash32(buffer, size) % TABLE_SIZE;
}

#elif HASH_FN == 2

/*
 * The table size is a power of 2.
 */
# define TABLE_SIZE		(8 * 1024)

/*
 * D.J. Bernstien Hash version 2 (+ replaced by ^).
 */
unsigned long
hash_index(unsigned char *buffer, unsigned long size)
{
	unsigned long hash = 5381;

	while (0 < size--)
		hash = ((hash << 5) + hash) ^ *buffer++;

	return hash & (TABLE_SIZE-1);
}

#elif HASH_FN == 3

/*
 * The table size is a power of 2.
 */
# define TABLE_SIZE		(8 * 1024)

/*
 * Hash function used by Sendmail. Original source unknown.
 */
unsigned long
hash_index(unsigned char *buffer, unsigned long size)
{
	int c, d;
	unsigned long hash = 0;

	while (0 < size--) {
		c = d = *buffer++;
		c ^= c<<6;
		hash += (c<<11) ^ (c>>1);
		hash ^= (d<<14) + (d<<7) + (d<<4) + d;
	}

	return hash & (TABLE_SIZE-1);
}

#elif HASH_FN == 4

/*
 * The table size is a power of 2.
 */
# define TABLE_SIZE		(8 * 1024)

/*
 * SMDB Hash Function
 */
unsigned long
hash_index(unsigned char *buffer, unsigned long size)
{
	unsigned long hash = 0;

	while (0 < size--)
		hash = (hash << 6) + (hash << 16) - hash + *buffer++;

	return hash & (TABLE_SIZE-1);
}

#endif /* HASH_FN */

static int
kvm_fetch_hash(kvm *self, kvm_data *key, kvm_data *value)
{
	kvm_hash *entry;
	unsigned long hash;

	hash = hash_index(key->data, key->size);
	entry = ((kvm_hash_table *) self->_kvm)->table[hash];
	memset(value, 0, sizeof (*value));

	for ( ; entry != NULL; entry = entry->next) {
		if (key->size == entry->key.size && memcmp(key->data, entry->key.data, key->size) == 0) {
			/* Pass by reference. */
			*value = entry->value;
			return KVM_OK;
		}
	}

	return KVM_NOT_FOUND;
}

static int
kvm_get_hash(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	kvm_data v;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);

	if (kvm_fetch_hash(self, key, &v) == KVM_NOT_FOUND) {
		rc = KVM_NOT_FOUND;
		goto error1;
	}

	if (value != NULL) {
		if ((value->data = malloc(v.size + 1)) == NULL)
			goto error1;

		/* Pass by value. */
		memcpy(value->data, v.data, v.size);
		value->data[v.size] = '\0';
		value->size = v.size;
#ifdef MULTICAST_VERSIONING
		value->created = v.created;
		value->version = v.version;
#endif
	}

	rc = KVM_OK;
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_put_hash(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	unsigned long hash;
	kvm_hash **prev, *entry;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL || value == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);

	hash = hash_index(key->data, key->size);
	entry = ((kvm_hash_table *) self->_kvm)->table[hash];
	prev = &((kvm_hash_table *) self->_kvm)->table[hash];

	for ( ; entry != NULL; prev = &entry->next, entry = entry->next) {
		if (key->size == entry->key.size && memcmp(key->data, entry->key.data, key->size) == 0) {
			/* Discard current entry. */
			*prev = entry->next;
			free(entry);
			break;
		}
	}

	if ((entry = malloc(sizeof (*entry) + key->size + value->size + 2)) == NULL)
		goto error1;

	/* Copy by value. */
#ifdef MULTICAST_VERSIONING
	entry->key.created = key->created;
	entry->key.version = key->version;
#endif
	entry->key.size = key->size;
	entry->key.data = (unsigned char *) &entry[1];
	memcpy(entry->key.data, key->data, key->size);
	entry->key.data[key->size] = '\0';

#ifdef MULTICAST_VERSIONING
	entry->value.created = value->created;
	entry->value.version = value->version;
#endif
	entry->value.size = value->size;
	entry->value.data = entry->key.data + key->size + 1;
	memcpy(entry->value.data, value->data, value->size);
	entry->value.data[value->size] = '\0';

	/* Insert or replace entry. */
	entry->next = *prev;
	*prev = entry;
	rc = KVM_OK;
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_remove_hash(kvm *self, kvm_data *key)
{
	int rc;
	unsigned long hash;
	kvm_hash **prev, *entry;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);

	hash = hash_index(key->data, key->size);
	entry = ((kvm_hash_table *) self->_kvm)->table[hash];
	prev = &((kvm_hash_table *) self->_kvm)->table[hash];

	for ( ; entry != NULL; prev = &entry->next, entry = entry->next) {
		if (key->size == entry->key.size && memcmp(key->data, entry->key.data, key->size) == 0) {
			*prev = entry->next;
			free(entry);
			rc = KVM_OK;
			goto error1;
		}
	}

	rc = KVM_NOT_FOUND;
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

#ifdef NOT_FINISHED
static int
kvm_first_hash(kvm *self)
{
	((kvm_hash_table *) self->_kvm)->cursor_index = 0;
	((kvm_hash_table *) self->_kvm)->cursor_next = NULL;

	return KVM_OK;
}

static int
kvm_next_hash(kvm *self, kvm_data *key, kvm_data *value)
{
	kvm_hash_table *t;

	t = (kvm_hash_table *) self->_kvm;

	if (t->cursor_next == NULL) {
		for ( ; t->cursor_index < TABLE_SIZE; t->cursor_index++) {
			t->cursor_next = t->table[t->cursor_index++];
			if (t->cursor_next != NULL)
				break;
		}

		if (TABLE_SIZE <= t->cursor_index)
			return KVM_NOT_FOUND;
	}

	*key = t->cursor_next->key;
	*value = t->cursor_next->value;
	t->cursor_next = t->cursor_next->next;

	return KVM_OK;
}

#else

static int
kvm_walk_hash(kvm *self, int (*func)(kvm_data *, kvm_data *, void *), void *data)
{
	int i, rc;
	kvm_hash **table, *entry, **prev;

	rc = KVM_ERROR;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	table = ((kvm_hash_table *) self->_kvm)->table;

	for (i = 0; i < TABLE_SIZE; i++) {
		for (prev = &table[i], entry = *prev; entry != NULL; entry = *prev) {
			switch ((*func)(&entry->key, &entry->value, data)) {
			case 0:
				goto error1;
			case 1:
				prev = &entry->next;
				break;
			case -1:
				*prev = entry->next;
				free(entry);
				break;
			}
		}
	}
error1:
	rc = KVM_OK;
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);

	return rc;
}
#endif

static int
kvm_truncate2_hash(kvm *self)
{
	int i;
	kvm_hash **table, *entry, *next;

	table = ((kvm_hash_table *) self->_kvm)->table;

	for (i = 0; i < TABLE_SIZE; i++) {
		for (entry = table[i]; entry != NULL; entry = next) {
			next = entry->next;
			free(entry);
		}
		table[i] = NULL;
	}

	return KVM_OK;
}

static int
kvm_truncate_hash(kvm *self)
{
	PTHREAD_MUTEX_LOCK(&self->_mutex);
	(void) kvm_truncate2_hash(self);
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
	return KVM_OK;

}

static void
kvm_close_hash(kvm *self)
{
	if (self != NULL) {
		if (self->_kvm != NULL) {
			(void) kvm_truncate2_hash(self);
			free(self->_kvm);
		}

		kvmClose(self);
	}
}

static int
kvm_open_hash(kvm *self, const char *location, int mode)
{
	kvm_hash_table *t;

	self->close = kvm_close_hash;
	self->filepath = kvm_filepath_stub;
	self->fetch = kvm_get_hash;
	self->get = kvm_get_hash;
	self->put = kvm_put_hash;
	self->remove = kvm_remove_hash;
#ifdef NOT_FINISHED
	self->first = kvm_first_hash;
	self->next = kvm_next_hash;
#else
	self->walk = kvm_walk_hash;
#endif
	self->truncate = kvm_truncate_hash;
	self->sync = kvm_sync_stub;

	self->begin = kvm_begin_stub;
	self->commit = kvm_commit_stub;
	self->rollback = kvm_rollback_stub;

	if ((t = malloc(sizeof (kvm_hash) + TABLE_SIZE * sizeof (kvm_hash *))) == NULL)
		return KVM_ERROR;

	self->_kvm = t;
	t->table = (kvm_hash **) (t+1);
	t->cursor_index = 0;
	t->cursor_next = NULL;

	memset(t->table, 0, TABLE_SIZE * sizeof (kvm_hash *));

	return KVM_OK;
}

/***********************************************************************
 *** Flat Text File
 ***********************************************************************/

typedef struct {
	char *path;
	kvm *hash;
} kvm_file;

static int
kvm_write(FILE *fp, unsigned char *data, unsigned long length)
{
	if (fprintf(fp, "%lu:", length) < 1)
		return -1;

	while (0 < length--) {
		if (*data != '=' && isprint(*data))
			fputc(*data, fp);
		else
			fprintf(fp, "=%02X", *data);
		data++;
	}

	if (fputc(',', fp) == EOF)
		return -1;

	return 0;
}

static int
kvm_read(FILE *fp, unsigned char **data, unsigned long *length)
{
	int ch, i;
	unsigned long size;
	unsigned char *buf;

	*data = NULL;
	*length = 0;

	if (fscanf(fp, "%9lu", &size) != 1)
		goto error0;
	if (fgetc(fp) != ':')
		goto error0;

	if ((buf = malloc(size + 1)) == NULL)
		goto error0;

	for (i = 0; i <= size && (ch = fgetc(fp)) != EOF; i++) {
		if (ch == '=' && fscanf(fp, "%2X", &ch) != 1)
			goto error1;
		buf[i] = ch;
	}

	if (buf[size] != ',')
		goto error1;

	buf[size] = '\0';
	*length = size;
	*data = buf;

	return 0;
error1:
	free(buf);
error0:
	return -1;
}

static int
kvm_read_token(FILE *fp, unsigned char **data, unsigned long *length, const char *delims)
{
	int ch;
	long foffset;
	unsigned long len;
	unsigned char *buf;

	/* Find length of next token. */
	foffset = ftell(fp);
	for (len = 0; (ch = fgetc(fp)) != EOF; len++) {
		if (strchr(delims, ch) != NULL)
			break;
	}

	if (fseek(fp, foffset, SEEK_SET) != 0)
		return -1;

	/* Allocate space for next token. */
	if ((buf = malloc(len+1)) == NULL)
		return -1;

	/* Re-read and save the next token. */
	if (fread(buf, 1, len, fp) != len) {
		free(buf);
		return -1;
	}

	buf[len] = '\0';
	*length = len;
	*data = buf;

	/* Skip delimiters. */
	while ((ch = fgetc(fp)) != EOF && strchr(delims, ch) != NULL)
		;
	ungetc(ch, fp);

	return 0;
}

static int
kvm_read_key(FILE *fp, unsigned char **data, unsigned long *length)
{
	int ch, len;

	/* Ignore comment and empty lines. */
	while ((ch = fgetc(fp)) != EOF) {
		if (ch == '#') {
			/* Eat comment line. */
			(void) fscanf(fp, "%*[^\n\r]%*[\n\r]%n", &len);
		} else if (ch == '\n' || ch == '\r') {
			/* Eat empty line or discard CR. */
			;
		} else {
			break;
		}
	}
	ungetc(ch, fp);

	return kvm_read_token(fp, data, length, "\t ");
}

static int
kvm_read_value(FILE *fp, unsigned char **data, unsigned long *length)
{
	return kvm_read_token(fp, data, length, "\n\r");
}

#ifdef NOT_USED
static int
kvm_fetch_file(kvm *self, kvm_data *key, kvm_data *value)
{
	return ((kvm_file *) self->_kvm)->hash->fetch(((kvm_file *) self->_kvm)->hash, key, value);
}
#endif

static int
kvm_get_file(kvm *self, kvm_data *key, kvm_data *value)
{
	return ((kvm_file *) self->_kvm)->hash->get(((kvm_file *) self->_kvm)->hash, key, value);
}

static int
kvm_put_file(kvm *self, kvm_data *key, kvm_data *value)
{
	return ((kvm_file *) self->_kvm)->hash->put(((kvm_file *) self->_kvm)->hash, key, value);
}

static int
kvm_remove_file(kvm *self, kvm_data *key)
{
	return ((kvm_file *) self->_kvm)->hash->remove(((kvm_file *) self->_kvm)->hash, key);
}

#ifdef NOT_FINISHED
static int
kvm_first_file(kvm *self)
{
	return ((kvm_file *) self->_kvm)->hash->first(((kvm_file *) self->_kvm)->hash);
}

static int
kvm_next_file(kvm *self, kvm_data *key, kvm_data *value)
{
	return ((kvm_file *) self->_kvm)->hash->next(((kvm_file *) self->_kvm)->hash, key, value);
}

#else

static int
kvm_walk_file(kvm *self, int (*func)(kvm_data *, kvm_data *, void *), void *data)
{
	return ((kvm_file *) self->_kvm)->hash->walk(((kvm_file *) self->_kvm)->hash, func, data);
}
#endif

static void
kvm_sync_file(kvm *self)
{
	long i;
	FILE *fp;
	kvm_file *file;
	kvm_hash *entry, **table;

	file = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (!(self->_mode & KVM_MODE_READ_ONLY) && (fp = fopen(file->path, "w")) != NULL) {
		table = ((kvm_hash_table *) file->hash->_kvm)->table;

		for (i = 0; i < TABLE_SIZE; i++) {
			for (entry = table[i]; entry != NULL; entry = entry->next) {
				if (kvm_write(fp, entry->key.data, entry->key.size))
					goto error1;
#ifdef MULTICAST_VERSIONING
				if (kvm_write(fp, &entry->value.created, sizeof (entry->value.created)))
					goto error1;
				if (kvm_write(fp, &entry->value.version, sizeof (entry->value.version)))
					goto error1;
#endif
				if (kvm_write(fp, entry->value.data, entry->value.size))
					goto error1;
				if (fputc('\n', fp) == EOF)
					goto error1;
			}
		}
error1:
		(void) fclose(fp);
	}

	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
}

static const char *
kvm_filepath_file(kvm *self)
{
	return ((kvm_file *) self->_kvm)->path;
}

static int
kvm_truncate_file(kvm *self)
{
	int rc;

	rc = ((kvm_file *) self->_kvm)->hash->truncate(((kvm_file *) self->_kvm)->hash);

	if (rc == KVM_OK) {
		PTHREAD_MUTEX_LOCK(&self->_mutex);
#if defined(HAVE_TRUNCATE)
		if (truncate(((kvm_file *) self->_kvm)->path, 0))
			rc = KVM_ERROR;
#elif defined(HAVE_FTRUNCATE)
{
		int fd;

		if ((fd = open(((kvm_file *) self->_kvm)->path, O_RDWR, 0600)) < 0) {
			rc = KVM_ERROR;
		} else {
			if (ftruncate(fd, 0))
				rc = KVM_ERROR;
			close(fd);
		}
}
#else
# error "Require ftruncate() or truncate() equivalent."
#endif
		PTHREAD_MUTEX_UNLOCK(&self->_mutex);
	}

	return rc;
}

static void
kvm_close_file(kvm *self)
{
	kvm_file *file;

	if (self != NULL) {
		if (self->_kvm != NULL) {
			file = self->_kvm;

			if (file->hash != NULL) {
				kvm_sync_file(self);
				file->hash->close(file->hash);
			}

			free(file->path);
			free(file);
		}

		kvmClose(self);
	}
}

static int
kvm_open_file(kvm *self, const char *location, int mode)
{
	FILE *fp;
	kvm_data k, v;
	kvm_file *file;
	char *fmode = "rw";
#ifdef HAVE_SYS_STAT_H
	struct stat sb;
#endif

	self->close = kvm_close_file;
	self->filepath = kvm_filepath_file;
	self->fetch = kvm_get_file;
	self->get = kvm_get_file;
	self->put = kvm_put_file;
	self->remove = kvm_remove_file;
#ifdef NOT_FINISHED
	self->first = kvm_first_file;
	self->next = kvm_next_file;
#else
	self->walk = kvm_walk_file;
#endif
	self->truncate = kvm_truncate_file;
	self->sync = kvm_sync_file;
	self->_mode = mode;

	self->begin = kvm_begin_stub;
	self->commit = kvm_commit_stub;
	self->rollback = kvm_rollback_stub;

	if (mode & KVM_MODE_READ_ONLY) {
		fmode = "r";
		self->put = kvm_put_stub;
		self->remove = kvm_remove_stub;
	}

	if ((file = malloc(sizeof (*file))) == NULL)
		goto error0;

	self->_kvm = file;

	if ((file->hash = kvmOpen(self->_table, NULL, 0)) == NULL)
		goto error0;

	if ((file->path = strdup(location)) == NULL)
		goto error0;

#ifdef HAVE_SYS_STAT_H
	if (stat(location, &sb) == 0 && S_ISREG(sb.st_mode) && (fp = fopen(location, fmode)) != NULL) {
#else
	if ((fp = fopen(location, fmode)) != NULL) {
#endif
		int (*get_key)(FILE *, unsigned char **, unsigned long *);
		int (*get_value)(FILE *, unsigned char **, unsigned long *);

		if (kvm_read(fp, &k.data, &k.size) && !feof(fp) && !ferror(fp)) {
			/* File looks like a human made key-value file. */
			get_key = kvm_read_key;
			get_value = kvm_read_value;
			self->_mode |= KVM_MODE_READ_ONLY;
		} else {
			/* File looks like a kvm file. */
			get_key = kvm_read;
			get_value = kvm_read;
		}

		rewind(fp);
		while (!feof(fp)) {
			if ((*get_key)(fp, &k.data, &k.size)) {
				if (feof(fp))
					break;
				goto error1;
			}
			if ((*get_value)(fp, &v.data, &v.size)) {
				if (feof(fp))
					break;
				goto error2;
			}

			if (*k.data != '#' && file->hash->put(file->hash, &k, &v))
				goto error3;

			free(v.data);
			free(k.data);
		}

		(void) fclose(fp);
	} else if (errno == ENOENT && (self->_mode & KVM_MODE_READ_ONLY)) {
#ifndef NDEBUG
		syslog(LOG_ERR, "open error \"%s\": %s (%d)", location, strerror(errno), errno);
#endif
		goto error0;
	} else if (errno != 0 && errno != ENOENT) {
		goto error0;
	}

	return KVM_OK;
error3:
	free(v.data);
error2:
	free(k.data);
error1:
	(void) fclose(fp);
error0:
	return KVM_ERROR;
}

/***********************************************************************
 *** Berkeley DB Handler
 ***********************************************************************/

#ifdef HAVE_DB_H

typedef struct {
	DB *db;
	int lockfd;
	char *file;
	time_t mtime;
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
	int is_185;
#endif
} kvm_db;

static int
kvm_flock_db(kvm *self, int mode)
{
	mode = mode == 0 ? LOCK_SH : LOCK_EX;

	/* Lock the database to prevent other process from
	 * modifying the database.
	 *
	 * NOTE that if the database failed to be reopened
	 * earlier, then we have to ignore this lock, since
	 * there is no replacement lock file descriptor yet.
	 */
	if (((kvm_db *) self->_kvm)->lockfd != -1) {
		/* Wait until we get the file lock. */
		do {
			errno = 0;
		} while (flock(((kvm_db *) self->_kvm)->lockfd, mode) && errno == EINTR);
	}

	return -(errno != 0);
}

static int
kvm_funlock_db(kvm *self)
{
	(void) flock(((kvm_db *) self->_kvm)->lockfd, LOCK_UN);
	return 0;
}

static void
kvm_sync_db(kvm *self)
{
	kvm_db *kdb;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_flock_db(self, 1) == 0) {
		kdb = (kvm_db *) self->_kvm;
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		if (kdb->is_185) {
#endif
#if defined(HAVE_DBOPEN)
			((DB185 *) kdb->db)->sync((DB185 *) kdb->db, 0);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		} else {
#endif
#if defined(HAVE_DB_CREATE)
			kdb->db->sync(kdb->db, 0);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		}
#endif
		(void) kvm_funlock_db(self);
	}
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
}

static void
kvm_close_db(kvm *self)
{
	kvm_db *bdb;

	if (self != NULL) {
		if (((kvm_db *) self->_kvm) != NULL) {
			bdb = self->_kvm;
			if (bdb->db != NULL) {
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
				if (bdb->is_185) {
#endif
#if defined(HAVE_DBOPEN)
					((DB185 *) bdb->db)->close((DB185 *) bdb->db);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
				} else {
#endif
#if defined(HAVE_DB_CREATE)
					bdb->db->close(bdb->db, 0);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
				}
#endif
			}
			free(bdb->file);
			free(bdb);
		}

		kvmClose(self);
	}
}

static int
kvm_reopen_db(kvm *self)
{
	DB *db;
	kvm_db *kdb;
	struct stat finfo;
	int type, open_mode;

	kdb = (kvm_db *) self->_kvm;

#if defined(HAVE_DB_CREATE)
/* When both BDB 1.85 and a more recent version are linked, we always
 * attempt to open the .db file using the superior version first, in
 * case it creates a new file.
 */
{
	int rc;

# if defined(HAVE_DBOPEN)
	kdb->is_185 = 0;
# endif
	rc = db_create(&db, NULL, 0);
	if (rc != 0 || db == NULL) {
		syslog(LOG_ERR, "db_create() error: %s", db_strerror(rc));
		goto error0;
	}

	if (0 < debug) {
		db->set_errpfx(db, kdb->file);
		db->set_errfile(db, stderr);
	}

	/* Use the same default cache size as Sendmail. */
	(void) db->set_cachesize(db, 0, 1024*1024, 0);

	type = (self->_mode & KVM_MODE_DB_BTREE) ? DB_BTREE : DB_HASH;
	open_mode = (self->_mode & KVM_MODE_READ_ONLY) ? DB_RDONLY : DB_CREATE;

	rc = db->open(db, DBTXN_ROAR kdb->file, NULL, type, open_mode | DB_NOMMAP, 0);
	if (rc != 0) {
# if defined(HAVE_DBOPEN)
/* I do NOT support using Berkeley DB 1.85, especially for read/write
 * caches. I've had too many problems with cache corruption in the past
 * that appeared to only happen when using BDB 1.85. So I confine its
 * use to read-only .db files.
 */
		db->close(db, 0);

		type = (self->_mode & KVM_MODE_DB_BTREE) ? DB185_BTREE : DB185_HASH;

		if ((db = (DB *) dbopen(kdb->file, O_RDONLY, 0644, type, NULL)) == NULL) {
			syslog(LOG_ERR, "failed to open %s \"%s\": %s (%d)", (self->_mode & KVM_MODE_DB_BTREE) ? "btree" : "hash",  kdb->file, strerror(errno), errno);
			goto error0;
		}

		kdb->is_185 = 1;
		self->_mode |= KVM_MODE_READ_ONLY;

		if ((kdb->lockfd = ((DB185 *) db)->fd((DB185 *) db)) == -1) {
			syslog(LOG_ERR, "get lock fd error \"%s\": %s (%d)", kdb->file, strerror(errno), errno);
			goto error1;
		}
# else
		syslog(LOG_ERR, "open error %s \"%s\": %s", (self->_mode & KVM_MODE_DB_BTREE) ? "btree" : "hash",  kdb->file, db_strerror(rc));
		goto error1;
# endif
	} else {
		rc = db->fd(db, &kdb->lockfd);
		if (rc != 0) {
			syslog(LOG_ERR, "get lock fd error \"%s\": %s", kdb->file, db_strerror(rc));
			goto error1;
		}
	}
}
#elif defined(HAVE_DBOPEN)
	type = (self->_mode & KVM_MODE_DB_BTREE) ? DB185_BTREE : DB185_HASH;
	open_mode = (self->_mode & KVM_MODE_READ_ONLY) ? O_RDONLY : O_CREAT|O_RDWR;

	if ((db = (DB *) dbopen(kdb->file, open_mode, 0644, type, NULL)) == NULL) {
		syslog(LOG_ERR, "failed to open %s \"%s\": %s (%d)", (self->_mode & KVM_MODE_DB_BTREE) ? "btree" : "hash", kdb->file, strerror(errno), errno);
		goto error0;
	}

	if ((kdb->lockfd = ((DB185 *) db)->fd((DB185 *) db)) == -1) {
		syslog(LOG_ERR, "get lock fd error \"%s\": %s (%d)", kdb->file, strerror(errno), errno);
		goto error1;
	}
#endif
	if (fstat(kdb->lockfd, &finfo)) {
		syslog(LOG_ERR, "stat error \"%s\": %s (%d)", kdb->file, strerror(errno), errno);
		goto error1;
	}

	kdb->mtime = finfo.st_mtime;
	kdb->db = db;

	return 0;
error1:
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
	if (kdb->is_185)
#endif
#if defined(HAVE_DBOPEN)
		(void) ((DB185 *) db)->close(((DB185 *) db));
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
	else
#endif
#if defined(HAVE_DB_CREATE)
		(void) db->close(db, 0);
#endif
error0:
	kdb->lockfd = -1;
	kdb->db = NULL;

	return -1;
}

static int
kvm_check_db(kvm *self)
{
	int rc;
	kvm_db *kdb;
	struct stat finfo;

	kdb = (kvm_db *) self->_kvm;

	/* If the key-value map is read-only, then we assume that
	 * it can be modified by external methods and so must check
	 * if its been updated. Consider sendmail and how access.db
	 * is read-only by everything except makemap(1).
	 *
	 * If the key-value map is read/write by us, then we assume
	 * that we're in sole control of updates and so don't need to
	 * fstat() the file. NOTE that BDB 4.4 has special support
	 * for multi process updates of a shared .db, which is not
	 * used here.
	 */
	if (self->_mode & KVM_MODE_READ_ONLY) {
		/* The use of fstat() is more efficient IMHO, but some people
		 * think stat() will be just as efficient on most operating
		 * systems, which maintain a cache of file meta data in memory.
		 * stat() will be more reliable in detecting database file updates
		 * that use a build & swap instead of a simple overwrite.
		 *
		 * See Debian /etc/mail/Makefile concerning access.db updates.
		 */
		rc = (self->_mode & KVM_MODE_DB_STAT) ? stat(kdb->file, &finfo) : fstat(kdb->lockfd, &finfo);

		if (rc == 0 && kdb->mtime != finfo.st_mtime) {
			/* If the file has been updated since the last access,
			 * then reopen the database to discard any local state.
			 */
			syslog(LOG_INFO, "reopening \"%s\"...", kdb->file);
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
			if (kdb->is_185)
#endif
#if defined(HAVE_DBOPEN)
				(void) ((DB185 *) kdb->db)->close((DB185 *) kdb->db);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
			else
#endif
#if defined(HAVE_DB_CREATE)
				(void) kdb->db->close(kdb->db, 0);
#endif
			kdb->lockfd = -1;
			kdb->db = NULL;
		}
	}

	if (kdb->db == NULL)
		return kvm_reopen_db(self);

	return 0;
}

static int
kvm_get_db(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	DBT k, v;
	kvm_db *kdb;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	/* Leave BDB with the responsibility of allocating and
	 * releasing the data passed back in a DBT. This is the
	 * only possibilty in BDB 1.85 (used by FreeBSD), but
	 * later versions have different memory management options.
	 *
	 * We don't have BDB 4 allocate this simply because we
	 * would have to reallocate it anyways to make room for
	 * a terminating null byte.
	 *
	 * As a result of FreeBSD being pricks by still using
	 * old BDB code, we have to use a mutex to protect this
	 * chunk of code.
	 */
	if (kvm_flock_db(self, 0) == 0) {
		if (kvm_check_db(self))
			goto error1;

		memset(&k, 0, sizeof (k));
		memset(&v, 0, sizeof (v));

		/* KVM_MODE_KEY_HAS_NUL is a hack for Postfix. It assumes
		 * a C string terminated by a NUL byte. If the key is
		 * actually binary data, such that there is no extra NUL
		 * byte at the end, then you can end up with a seg. fault.
		 */
		k.size = key->size + ((self->_mode & KVM_MODE_KEY_HAS_NUL) == KVM_MODE_KEY_HAS_NUL);
		k.data = key->data;

		kdb = (kvm_db *) self->_kvm;

#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		if ((kdb->is_185 && (rc = ((DB185 *) kdb->db)->get((DB185 *) kdb->db, (DBT185 *) &k, (DBT185 *) &v, 0)) == 1)
		||  (!kdb->is_185 && (rc = kdb->db->get(kdb->db, DBTXN &k, &v, 0)) == DB_NOTFOUND)) {
#elif defined(HAVE_DB_CREATE)
		if ((rc = kdb->db->get(kdb->db, DBTXN &k, &v, 0)) == DB_NOTFOUND) {
#elif defined(HAVE_DBOPEN)
		if ((rc = ((DB185 *) kdb->db)->get((DB185 *) kdb->db, (DBT185 *) &k, (DBT185 *) &v, 0)) == 1) {
#endif
			if (0 < debug)
				syslog(LOG_DEBUG, "kvm_get_db \"%s\" key=%lu:\"%s\" not found", kdb->file, (unsigned long) k.size, (char *) k.data);
			rc = KVM_NOT_FOUND;
			goto error1;
		}

		if (rc != 0) {
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
			if (kdb->is_185)
#endif
#if defined(HAVE_DBOPEN)
				syslog(LOG_ERR, "kvm_get_db \"%s\" failed: %s (%d)", kdb->file, strerror(errno), errno);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
			else
#endif
#if defined(HAVE_DB_CREATE)
				syslog(LOG_ERR, "kvm_get_db \"%s\" failed: %s", kdb->file, db_strerror(rc));
#endif
			goto error1;
		}

		if (value != NULL) {
			/* Add an extra NUL byte just in case its a C string. */
			if ((value->data = malloc(v.size + 1)) == NULL)
				goto error1;

			memcpy(value->data, v.data, v.size);
			value->data[v.size] = '\0';
			value->size = v.size;
		}

		rc = KVM_OK;
error1:
		(void) kvm_funlock_db(self);
	}
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_put_db(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	DBT k, v;
	kvm_db *kdb;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL || value == NULL)
		goto error0;

	memset(&k, 0, sizeof (k));
	k.size = key->size;
	k.data = key->data;

	memset(&v, 0, sizeof (v));
	v.size = value->size;
	v.data = value->data;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_flock_db(self, 1) == 0) {
		kdb = (kvm_db *) self->_kvm;
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		if (kdb->is_185) {
#endif
#if defined(HAVE_DBOPEN)
			if ((rc = ((DB185 *) kdb->db)->put((DB185 *) kdb->db, (DBT185 *) &k, (DBT185 *) &v, 0)) != 0)
				syslog(LOG_ERR, "kvm_put_db \"%s\" failed: %s (%d)", kdb->file, strerror(errno), errno);
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		} else {
#endif
#if defined(HAVE_DB_CREATE)
			if ((rc = kdb->db->put(kdb->db, DBTXN &k, &v, 0)) != 0)
				syslog(LOG_ERR, "kvm_put_db \"%s\" failed: %s", kdb->file, db_strerror(rc));
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		}
#endif
		(void) kvm_funlock_db(self);
	}
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_remove_db(kvm *self, kvm_data *key)
{
	DBT k;
	int rc;
	kvm_db *kdb;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	memset(&k, 0, sizeof (k));
	k.size = key->size;
	k.data = key->data;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_flock_db(self, 1) == 0) {
		kdb = (kvm_db *) self->_kvm;
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		if (kdb->is_185) {
#endif
#if defined(HAVE_DBOPEN)
			switch (((DB185 *) kdb->db)->del((DB185 *) kdb->db, (DBT185 *) &k, 0)) {
			case 1: rc = KVM_NOT_FOUND; break;
			case 0: rc = KVM_OK; break;
			}
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		} else {
#endif
#if defined(HAVE_DB_CREATE)
			switch (kdb->db->del(kdb->db, DBTXN &k, 0)) {
			case DB_NOTFOUND: rc = KVM_NOT_FOUND; break;
			case 0: rc = KVM_OK; break;
			}
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		}
#endif
		(void) kvm_funlock_db(self);
	}
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_truncate_db(kvm *self)
{
	int rc;
	kvm_db *kdb;

	rc = KVM_ERROR;

	if (self == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_flock_db(self, 1) == 0) {
		kdb = (kvm_db *) self->_kvm;
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		if (kdb->is_185) {
#endif
#if defined(HAVE_DBOPEN)
			rc = KVM_NOT_IMPLEMETED;
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		} else {
#endif
#if defined(HAVE_DB_CREATE)
{
			u_int32_t count;
			switch (kdb->db->truncate(kdb->db, DBTXN &count, 0)) {
			default: rc = KVM_NOT_FOUND; break;
			case 0: rc = KVM_OK; break;
			}
}
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		}
#endif
		(void) kvm_funlock_db(self);
	}
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

#ifdef NOT_FINISHED
static int
kvm_first_db(struct kvm *self)
{
	return KVM_OK;
}

static int
kvm_next_db(struct kvm *self, kvm_data *key, kvm_data *value)
{
	return KVM_ERROR;
}

#else

static int
kvm_walk_db(kvm *self, int (*func)(kvm_data *, kvm_data *, void *), void *data)
{
	DBT k, v;
	int rc, ret;
	kvm_db *kdb;
	kvm_data key, value;
#if defined(HAVE_DBOPEN)
	unsigned next = R_FIRST;
#endif
#if defined(HAVE_DB_CREATE)
	DBC *cursor;
#endif

	rc = KVM_ERROR;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_flock_db(self, 1) == 0) {
		ret = 0;
		memset(&k, 0, sizeof (k));
		memset(&v, 0, sizeof (v));
		kdb = (kvm_db *) self->_kvm;

#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		if (kdb->is_185) {
#endif
#if defined(HAVE_DBOPEN)
			next = R_FIRST;
			while (((DB185 *) kdb->db)->seq((DB185 *) kdb->db, (DBT185 *) &k, (DBT185 *) &v, next) == 0) {
				next = R_NEXT;

				if (k.data == NULL || v.data == NULL)
					break;

				/* Convert from DBT to Data object. */
				key.data = k.data;
				key.size = k.size;

				value.data = v.data;
				value.size = v.size;

				if ((ret = (*func)(&key, &value, data)) == 0)
					break;

				if (ret == -1 && ((DB185 *) kdb->db)->del((DB185 *) kdb->db, NULL, R_CURSOR) != 0)
					break;
			}
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		} else {
#endif
#if defined(HAVE_DB_CREATE)
			k.flags = DB_DBT_REALLOC;
			v.flags = DB_DBT_REALLOC;

			if (kdb->db->cursor(kdb->db, DBTXN &cursor, 0) != 0)
				goto error1;

			while (cursor->c_get(cursor, &k, &v, DB_NEXT) == 0) {
				if (k.data == NULL || v.data == NULL)
					break;

				/* Convert from DBT to Data object. */
				key.data = k.data;
				key.size = k.size;

				value.data = v.data;
				value.size = v.size;

				if ((ret = (*func)(&key, &value, data)) == 0)
					break;

				if (ret == -1 && cursor->c_del(cursor, 0) != 0)
					break;
			}

			(void) cursor->c_close(cursor);
			free(k.data);
			free(v.data);
error1:
			;
#endif
#if defined(HAVE_DBOPEN) && defined(HAVE_DB_CREATE)
		}
#endif
		if (0 <= ret)
			rc = KVM_OK;

		(void) kvm_funlock_db(self);
	}
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);

	return rc;
}

#endif

static const char *
kvm_filepath_db(kvm *self)
{
	return ((kvm_db *) self->_kvm)->file;
}

static int
kvm_open_db(kvm *self, const char *location, int mode)
{
	kvm_db *kdb;
	int offset = 0;

	if (0 < TextInsensitiveStartsWith(location, "btree" KVM_DELIM_S)) {
		offset += sizeof ("btree" KVM_DELIM_S)-1;
		mode |= KVM_MODE_DB_BTREE;
	}

	self->close = kvm_close_db;
	self->filepath = kvm_filepath_db;
	self->fetch = kvm_get_db;
	self->get = kvm_get_db;
	self->put = kvm_put_db;
	self->remove = kvm_remove_db;
#ifdef NOT_FINISHED
	self->first = kvm_first_db;
	self->next = kvm_next_db;
#else
	self->walk = kvm_walk_db;
#endif
	self->truncate = kvm_truncate_db;
	self->sync = kvm_sync_db;
	self->_mode = mode;

	self->begin = kvm_begin_stub;
	self->commit = kvm_commit_stub;
	self->rollback = kvm_rollback_stub;

	if (mode & KVM_MODE_READ_ONLY) {
		self->put = kvm_put_stub;
		self->remove = kvm_remove_stub;
	}

	if ((kdb = calloc(1, sizeof (*kdb))) == NULL)
		return KVM_ERROR;

	self->_kvm = kdb;

	if ((kdb->file = strdup(location + offset)) == NULL)
		return KVM_ERROR;

	if (kvm_reopen_db(self)) {
		if ((mode & KVM_MODE_READ_ONLY) != KVM_MODE_READ_ONLY)
			return KVM_ERROR;

		/* Since we're opening read-only, why not try the other
		 * format in case the user is a yutz and specified the
		 * wrong parameter.
		 */
		if (mode & KVM_MODE_DB_BTREE)
			self->_mode &= ~KVM_MODE_DB_BTREE;
		else
			self->_mode |= KVM_MODE_DB_BTREE;

		syslog(LOG_INFO, "trying %s as a %s ...", kdb->file, (self->_mode & KVM_MODE_DB_BTREE) ? "btree" : "hash");

		if (kvm_reopen_db(self))
			return KVM_ERROR;

		syslog(LOG_INFO, "succesfully opened %s as a %s", kdb->file, (self->_mode & KVM_MODE_DB_BTREE) ? "btree" : "hash");
	}

	return KVM_OK;
}

#endif /* HAVE_DB_H */

/***********************************************************************
 *** Socket Map Client Handler
 ***********************************************************************/

/*
 * The original socket map protocol assumes a simple FETCH operation.
 * In order to be more generic, we need additional operations such as
 * GET, PUT, LIST, and REMOVE that can operate on binary data.
 *
 * Sendmail socket map FETCH semantics:
 *
 *	> $length ":" $table_name " " $key ","
 *	< $length ":" $status " " $result ","
 *
 *	$length is the ASCII numeric representation of the data length.
 *	$table_name assumed to be ASCII containing no spaces; $key can
 *	be binary; $status is one of "OK", "NOTFOUND", "PERM", "TEMP",
 *	"TIMEOUT". If $status is OK, then the $result is the stored
 *	value, which can be binary. Otherwise $result is an error
 *	message.
 *
 * To distinguish sendmail socket map FETCH operation from other
 * operations, send only the $table_name without the subsequent space
 * and $key, then send the command, followed by any arguments. Each is
 * a net string, which can contain binary data.
 *
 * GET
 *	> $length ":" $table_name ",3:GET,"
 *	> $length ":" $key ","
 *	< $length ":" $status " " $result ","
 *	< $length ":" $value ","
 *
 *	$value row returned only if $status == OK.
 *
 * PUT
 *	> $length ":" $table_name ",3:PUT,"
 *	> $length ":" $key "," $length ":" $value ","
 *	< $length ":" $status " " $result ","
 *
 * REMOVE
 *	> $length ":" $table_name ",6:REMOVE,"
 *	> $length ":" $key ","
 *	< $length ":" $status " " $result ","
 *
 * FIRST
 *	> $length ":" $table_name ",5:FIRST,"
 *	< $length ":" $status " " $result ","
 *	< $length ":" $key "," $length ":" $value ","
 *
 *	$key and $value row returned only if $status == OK.
 *
 * NEXT
 *	> $length ":" $table_name ",4:NEXT,"
 *	< $length ":" $status " " $result ","
 *	< $length ":" $key "," $length ":" $value ","
 *
 *	$key and $value row returned only if $status == OK.
 *
 * LAST
 *	> $length ":" $table_name ",4:LAST,"
 *	< $length ":" $status " " $result ","
 *	< $length ":" $key "," $length ":" $value ","
 *
 *	$key and $value returned only if $status == OK.
 *
 * PREVIOUS
 *	> $length ":" $table_name ",8:PREVIOUS,"
 *	< $length ":" $status " " $result ","
 *	< $length ":" $key "," $length ":" $value ","
 *
 *	$key and $value row returned only if $status == OK.
 *
 * LIST
 *	> $length ":" $table_name ",4:LIST,"
 *	< $length ":" $status " " $result ","
 *	< $length ":" $key "," $length ":" $value ","
 *	< ...
 *	< ... upto $result rows
 *
 *	A list of $key and $value returned only if $status == OK.
 *	$result will be the ASCII number string of key-value pairs
 *	to follow, otherwise an error message.
 *
 * $status is one of "OK", "NOTFOUND", "PERM", "TEMP", "TIMEOUT".
 */

static int
kvm_send(Socket2 *s, unsigned char *buffer, unsigned long length)
{
	int number_length;
	unsigned char number[20];

	number_length = snprintf((char *) number, sizeof (number), "%lu:", length);

	if (socketWrite(s, number, number_length) != number_length)
		return -1;
	if (socketWrite(s, buffer, length) != length)
		return -1;
	if (socketWrite(s, (unsigned char *) ",", 1) != 1)
		return -1;

	return 0;
}

static int
kvm_recv(Socket2 *s, unsigned char **data, unsigned long *length)
{
	unsigned long size;
	unsigned char *buf;
	long i, timeout, bytes;
	char number[20], *stop;

	*data = NULL;
	*length = 0;
	timeout = socketGetTimeout(s);

	bytes = -1;

	/* Read leading decimal length. */
	for (i = 0; i < sizeof (number)-1; i++) {
		if (!socketHasInput(s, timeout)) {
			/* No input ready. */
			goto error0;
		}
		if ((bytes = socketRead(s, (unsigned char *) number + i, 1)) != 1) {
			/* Read error. */
			goto error0;
		}
		if (!isdigit(number[i])) {
			/* Not a decimal digit or colon found. */
			break;
		}
	}

	if (i <= 0 || number[i] != ':') {
		/* No input or invalid format. */
		goto error0;
	}

	size = (unsigned long) strtol((char *) number, &stop, 10);

	if (stop-number != i || (buf = malloc(size + 1)) == NULL) {
		/* Invalid decimal number or allocation failure? */
		goto error0;
	}

	if (!socketHasInput(s, timeout) || (bytes = socketRead(s, buf, size + 1)) != size + 1) {
		/* No input or read error. */
		goto error1;
	}

	if (buf[size] != ',') {
		/* Invalid format. */
		goto error1;
	}

	buf[size] = '\0';
	*length = size;
	*data = buf;

	return KVM_OK;
error1:
	free(buf);
error0:
	return bytes != 0 ? KVM_ERROR : KVM_EOF;
}

int
kvm_check_socket(kvm *self)
{
	if (self->_kvm != NULL)
		return 0;

	return socketOpenClient(self->_location + sizeof ("socketmap" KVM_DELIM_S)-1, KVM_PORT, SOCKET_CONNECT_TIMEOUT, NULL, (Socket2 **) &self->_kvm);
}

static int
kvm_fetch_socket(struct kvm *self, kvm_data *key, kvm_data *value)
{
	long timeout;
	size_t name_length;
	int i, rc, number_length;
	unsigned char number[20];
	unsigned long query_length;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL || value == NULL)
		goto error0;

	memset(value, 0, sizeof (*value));

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_check_socket(self))
		goto error1;

	name_length = strlen(self->_table);
	query_length = name_length + 1 + key->size;
	number_length = snprintf((char *) number, sizeof (number), "%lu:", query_length);

	timeout = socketGetTimeout(self->_kvm);

	if (socketWrite(self->_kvm, number, number_length) != number_length)
		goto error2;
	if (socketWrite(self->_kvm, (unsigned char *) self->_table, name_length) != name_length)
		goto error2;
	if (socketWrite(self->_kvm, (unsigned char *) " ", 1) != 1)
		goto error2;
	if (socketWrite(self->_kvm, key->data, key->size) != key->size)
		goto error2;
	if (socketWrite(self->_kvm, (unsigned char *) ",", 1) != 1)
		goto error2;

	/* Read leading decimal length. */
	for (i = 0; i < sizeof (number)-1; i++) {
		if (!socketHasInput(self->_kvm, timeout) || socketRead(self->_kvm, number + i, 1) != 1) {
			/* No input or read error. */
			goto error2;
		}
		if (!isdigit(number[i])) {
			/* Not a decimal digit or colon found. */
			break;
		}
	}

	if (i <= 0 || number[i] != ':') {
		/* No input or invalid format. */
		goto error2;
	}

	value->size = (unsigned long) strtol((char *) number, NULL, 10);

	if (value->size < 3 || (value->data = malloc(value->size + 1)) == NULL) {
		/* Too short for a Sendmail fetch or allocation error. */
		goto error2;
	}

	/* Try to read leading "OK ". */
	if (!socketHasInput(self->_kvm, timeout) || socketRead(self->_kvm, value->data, 3) != 3) {
		/* No input or read error. */
		goto error2;
	}

	if (*value->data == 'O') {
		/* The query/response was "OK"; remove
		 * the prefix from the returned value.
		 */
		value->size -= 3;
		i = 0;
	} else {
		/* There was some sort of error. */
		i = 3;
	}

	/* Read remainder of input. */
	if (!socketHasInput(self->_kvm, timeout) || socketRead(self->_kvm, value->data+i, value->size-i+1) != value->size-i+1) {
		/* No input or read error. */
		goto error2;
	}

	if (value->data[value->size] != ',') {
		/* Invalid format. */
		goto error2;
	}

	value->data[value->size] = '\0';

	if (i == 0)
		rc = KVM_OK;
	else if (*value->data == 'N')
		rc = KVM_NOT_FOUND;
error2:
	if (rc == KVM_ERROR) {
		socketClose(self->_kvm);
		self->_kvm = NULL;
		errno = 0;
	}
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_get_socket(struct kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	kvm_data result, v;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_check_socket(self))
		goto error1;
	if (kvm_send(self->_kvm, (unsigned char *) self->_table, strlen(self->_table)))
		goto error2;
	if (kvm_send(self->_kvm, (unsigned char *) "GET", sizeof ("GET")-1))
		goto error2;
	if (kvm_send(self->_kvm, key->data, key->size))
		goto error2;

	if (kvm_recv(self->_kvm, &result.data, &result.size))
		goto error2;
	if (0 < result.size) {
		switch (*result.data) {
		case 'O':
			if (kvm_recv(self->_kvm, &v.data, &v.size))
				goto error2;

			if (value != NULL)
				*value = v;

			rc = KVM_OK;
			break;
		case 'N':
			rc = KVM_NOT_FOUND;
			break;
		}
	}
	free(result.data);
error2:
	if (rc == KVM_ERROR) {
		socketClose(self->_kvm);
		self->_kvm = NULL;
		errno = 0;
	}
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_put_socket(struct kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	kvm_data result;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL || value == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_check_socket(self))
		goto error1;

	if (kvm_send(self->_kvm, (unsigned char *) self->_table, strlen(self->_table)))
		goto error2;
	if (kvm_send(self->_kvm, (unsigned char *) "PUT", sizeof ("PUT")-1))
		goto error2;
	if (kvm_send(self->_kvm, key->data, key->size))
		goto error2;
	if (kvm_send(self->_kvm, value->data, value->size))
		goto error2;

	if (kvm_recv(self->_kvm, &result.data, &result.size))
		goto error2;
	if (0 < result.size && *result.data == 'O')
		rc = KVM_OK;
	free(result.data);
error2:
	if (rc == KVM_ERROR) {
		socketClose(self->_kvm);
		self->_kvm = NULL;
		errno = 0;
	}
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_remove_socket(struct kvm *self, kvm_data *key)
{
	int rc;
	kvm_data result;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_check_socket(self))
		goto error1;

	if (kvm_send(self->_kvm, (unsigned char *) self->_table, strlen(self->_table)))
		goto error2;
	if (kvm_send(self->_kvm, (unsigned char *) "REMOVE",  sizeof ("REMOVE")-1))
		goto error2;
	if (kvm_send(self->_kvm, key->data, key->size))
		goto error2;

	if (kvm_recv(self->_kvm, &result.data, &result.size))
		goto error2;
	if (0 < result.size) {
		switch (*result.data) {
		case 'O': rc = KVM_OK; break;
		case 'N': rc = KVM_NOT_FOUND; break;
		}
	}
	free(result.data);
error2:
	if (rc == KVM_ERROR) {
		socketClose(self->_kvm);
		self->_kvm = NULL;
		errno = 0;
	}
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

#ifdef NOT_FINISHED
static int
kvm_first_socket(struct kvm *self)
{
	return KVM_OK;
}

static int
kvm_next_socket(struct kvm *self, kvm_data *key, kvm_data *value)
{
	return KVM_ERROR;
}
#else

static int
kvm_walk_socket(kvm *self, int (*func)(kvm_data *, kvm_data *, void *), void *data)
{
	int rc, ret, ch;
	kvm_data result, key, value;

	rc = KVM_ERROR;

	if (self == NULL || func)
		goto error0;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_check_socket(self))
		goto error1;

	if (kvm_send(self->_kvm, (unsigned char *) self->_table, strlen(self->_table)))
		goto error2;
	if (kvm_send(self->_kvm, (unsigned char *) "FIRST", sizeof ("FIRST")-1))
		goto error2;

	for (;;) {
		if (kvm_recv(self->_kvm, &result.data, &result.size))
			goto error2;

		ch = *result.data;
		free(result.data);

		if (ch == 'N')
			break;
		if (ch != 'O')
			goto error2;

		if (kvm_recv(self->_kvm, &key.data, &key.size))
			goto error2;

		if (kvm_recv(self->_kvm, &value.data, &value.size)) {
			free(key.data);
			goto error2;
		}

		ret = (*func)(&key, &value, data);

		free(value.data);
		free(key.data);

		if (ret == 0)
			break;

		if (kvm_send(self->_kvm, (unsigned char *) self->_table, strlen(self->_table)))
			goto error2;
		if (kvm_send(self->_kvm, (unsigned char *) "NEXT", sizeof ("NEXT")-1))
			goto error2;
	}

	rc = KVM_OK;
error2:
	if (rc == KVM_ERROR) {
		socketClose(self->_kvm);
		self->_kvm = NULL;
		errno = 0;
	}
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

#endif

static void
kvm_close_socket(kvm *self)
{
	if (self != NULL) {
		socketClose(self->_kvm);
		kvmClose(self);
		errno = 0;
	}
}

static int
kvm_open_socket(kvm *self, const char *location, int mode)
{
	self->close = kvm_close_socket;
	self->filepath = kvm_filepath_stub;
	self->fetch = kvm_fetch_socket;
	self->get = kvm_get_socket;
	self->put = kvm_put_socket;
	self->remove = kvm_remove_socket;
#ifdef NOT_FINISHED
	self->first = kvm_first_socket;
	self->next = kvm_next_socket;
#else
	self->walk = kvm_walk_socket;
#endif
	self->truncate = kvm_truncate_stub;
	self->sync = kvm_sync_stub;
	self->_mode = mode;

	self->begin = kvm_begin_stub;
	self->commit = kvm_commit_stub;
	self->rollback = kvm_rollback_stub;

	if (mode & KVM_MODE_READ_ONLY) {
		self->put = kvm_put_stub;
		self->remove = kvm_remove_stub;
	}

	if (socketOpenClient(location, KVM_PORT, SOCKET_CONNECT_TIMEOUT, NULL, (Socket2 **) &self->_kvm))
		return KVM_ERROR;

	return KVM_OK;
}

/***********************************************************************
 *** Multicast one-to-many link-local updates.
 ***********************************************************************/

typedef struct {
	SocketAddress *group;
	Socket2 *socket;
	int running;
	kvm *map;
} kvm_multicast;

#define KVM_GET		'g'
#define KVM_PUT		'p'
#define KVM_LIST	'l'
#define KVM_REMOVE	'r'

static int
kvm_send_packet(kvm *self, int command, kvm_data *key, kvm_data *value)
{
	long packet_length;
	kvm_multicast *multicast;
	unsigned char packet[512];

	packet_length = 3 + key->size + value->size;

	if (255 < key->size || 255 < value->size || sizeof (packet) < packet_length)
		return KVM_ERROR;

	/*
	 * +0			command
	 * +1			key size
	 * +2   		value size
	 * +3   		key data
	 *			...
	 * +3 + key size	value data
	 */
	packet[0] = command;
	packet[1] = (unsigned char) key->size;
	packet[2] = (unsigned char) value->size;
	memcpy(packet + 3, key->data, key->size);
	memcpy(packet + 3 + key->size, value->data, value->size);

	multicast = self->_kvm;

	if (socketWriteTo(multicast->socket, packet, packet_length, multicast->group) != packet_length)
		return KVM_ERROR;

	return KVM_OK;
}

static int
kvm_fetch_multicast(kvm *self, kvm_data *key, kvm_data *value)
{
	return ((kvm_multicast *) self->_kvm)->map->fetch(((kvm_multicast *) self->_kvm)->map, key, value);
}

static int
kvm_get_multicast(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;

	rc = ((kvm_multicast *) self->_kvm)->map->get(((kvm_multicast *) self->_kvm)->map, key, value);

#ifdef MULTICAST_VERSIONING
	/* Get returns a copy of the value. Remove the version info and
	 * null terminate the buffer just in case its a string.
	 */
	if (rc == KVM_OK) {
		value->size -= 4;
		value->data[value.size] = '\0';
	}
#endif
	return rc;
}

static int
kvm_put_multicast(kvm *self, kvm_data *key, kvm_data *value)
{
#ifdef MULTICAST_VERSIONING
	int rc;
	kvm_data vv;
	char *last_field;
	unsigned long version = 0;

	switch (kvm_get_multicast(self, key, &vv)) {
	case KVM_OK:
		/* Copy the 32-bit version number from memory, which might
		 * be at an odd byte boundary, into an unsigned long variable,
		 * which could be 64-bits (not 32).
		 */
		memcpy(&version + sizeof (version) - 4, &vv.data[vv.size - 4], 4);
		free(vv.data);
		break;
	case KVM_ERROR:
		return KVM_ERROR;
	}

	/* Create the new version. */
	if ((vv.data = malloc(value.size + 4)) == NULL)
		return KVM_ERROR;

	version++;
	vv.size = value.size + 4;
	memcpy(vv.data, value->data, value->size);
	memcpy(&vv.data[vv.size - 4], &version + sizeof (version) - 4, 4);

	/* Insert or replace. */
	rc = KVM_OK;
	if (kvm_send_packet(self, KVM_PUT, key, &vv))
		rc = ((kvm_multicast *) self->_kvm)->map->put(((kvm_multicast *) self->_kvm)->map, key, &vv);

	free(vv.data);

	return rc;
#else
	if (kvm_send_packet(self, KVM_PUT, key, value))
		return ((kvm_multicast *) self->_kvm)->map->put(((kvm_multicast *) self->_kvm)->map, key, value);

	return KVM_OK;
#endif
}

static int
kvm_remove_multicast(kvm *self, kvm_data *key)
{
	kvm_data value = { 0, (unsigned char *) "" };

	if (kvm_send_packet(self, KVM_REMOVE, key, &value))
		return ((kvm_multicast *) self->_kvm)->map->remove(((kvm_multicast *) self->_kvm)->map, key);

	return KVM_OK;
}

#ifdef NOT_FINISHED
static int
kvm_first_multicast(kvm *self)
{
	return ((kvm_multicast *) self->_kvm)->map->first(((kvm_multicast *) self->_kvm)->map);
}

static int
kvm_next_multicast(kvm *self, kvm_data *key, kvm_data *value)
{
	return ((kvm_multicast *) self->_kvm)->map->next(((kvm_multicast *) self->_kvm)->map, key, value);
}

#else

static int
kvm_walk_multicast(kvm *self, int (*func)(kvm_data *, kvm_data *, void *), void *data)
{
	return ((kvm_multicast *) self->_kvm)->map->walk(((kvm_multicast *) self->_kvm)->map, func, data);
}
#endif

static void
kvm_close_multicast(kvm *self)
{
	kvm_multicast *multicast;

	if (self != NULL) {
		if (self->_kvm != NULL) {
			multicast = self->_kvm;
			multicast->running = 0;

			if (multicast->map != NULL)
				multicast->map->close(multicast->map);

			socketClose(multicast->socket);
			free(multicast->group);
			free(multicast);
		}

		kvmClose(self);
	}
}

static void *
kvm_listener_multicast(void *data)
{
	int rc;
	kvm_data k, v;
	Socket2 *socket;
	kvm *self = data;
	SocketAddress *addr;
	const char *this_host;
	unsigned char packet[512];
	kvm_multicast *multicast = self->_kvm;

#if defined(HAVE_PTHREAD_CREATE)
	(void) pthread_detach(pthread_self());
#endif
	this_host = multicast->group->sa.sa_family == AF_INET ? "0.0.0.0" : "::0";

	if ((addr = socketAddressCreate(this_host, socketAddressGetPort(multicast->group))) == NULL)
		goto error0;

	if ((socket = socketOpen(addr, 0)) == NULL) {
		syslog(LOG_ERR, "socketOpen() failed: %s (%d)", strerror(errno), errno);
		goto error1;
	}

	if (socketSetReuse(socket, 1)) {
		syslog(LOG_ERR, "socketSetReuse(true) failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketBind(socket, addr)) {
		syslog(LOG_ERR, "socketBind() failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if (socketMulticast(socket, multicast->group, 1)) {
		syslog(LOG_ERR, "socketMulticast() join failed: %s (%d)", strerror(errno), errno);
		goto error2;
	}

	if (0 < debug)
		syslog(LOG_INFO, "start of multicast listener");

	while (multicast->running) {
		if (!socketHasInput(socket, 30000))
			continue;

		if (socketReadFrom(socket, packet, sizeof (packet), NULL) < 0) {
			syslog(LOG_ERR, "multicast kvm socket read error: %s (%d)", strerror(errno), errno);
			continue;
		}

#ifdef MULTICAST_VERSIONING
		/* The multicast KVM for efficiency broadcasts on put
		 * only and fetches from the local cache on get. There
		 * is no multicast query/get as this would introduce
		 * the need for timeouts. So in the event that one
		 * of our peers goes off-line and becomes out of sync
		 * with the rest of the group we have to implement a
		 * "correction" strategy. This means the out-of-sync
		 * peer will act on incorrect information at least
		 * once before receiving a correction.
		 *
		 * The correction strategy requires a record version
		 * number and create timestamp associated with each
		 * key-value pair. We check the version and create
		 * time of the local key-value pair before accepting
		 * an update according to the following rules:
		 *
		 * a) Accept the update if we have no local record.
		 *
		 * b) If the received record has version number zero,
		 * replace our local record. This can occur when the
		 * version counter rolls over and is treated as a sync
		 * point. Normally this should not occur if records
		 * are expired regularly before a roll over state ever
		 * occurs. Yet this does allow for manual overrides.
		 *
		 * c) If the local record is older than the received
		 * record, then discard the received record and
		 * broadcast our local record as a correction.
		 *
		 * d) If the create times of the local and received
		 * records are the same, but the received record has
		 * a newer version number, accept it.
		 *
		 * e) If the create times of the local and received
		 * records are the same and the version numbers of the
		 * received and local records are the same, then simply
		 * discard the received record. This can still result
		 * in out-of-sync data within the group, but avoids
		 * broadcast correction loops. This degraded state can
		 * occur when an out-of-sync peer's record is only one
		 * version behind its peers.
		 */
#endif
		/*
		 * +0			command
		 * +1			key size
		 * +2   		value size
		 * +3   		key data
		 *			...
		 * +3 + key size	value data
		 */
		k.size = (unsigned long) packet[1];
		k.data = packet + 3;

		v.size = (unsigned long) packet[2];
		v.data = packet + 3 + k.size;

		switch (packet[0]) {
		case KVM_PUT:
			rc = multicast->map->put(multicast->map, &k, &v);
			if (0 < debug)
				syslog(LOG_INFO, "multicast PUT %s %s", self->_table, rc == KVM_OK ? "OK" : "FAILED");
			break;
		case KVM_REMOVE:
			rc = multicast->map->remove(multicast->map, &k);
			if (0 < debug)
				syslog(LOG_INFO, "multicast REMOVE %s %s", self->_table, rc == KVM_OK ? "OK" : "FAILED");
			break;
		}
	}

	if (0 < debug)
		syslog(LOG_INFO, "end of multicast listener");

	if (socketMulticast(socket, multicast->group, 0))
		syslog(LOG_ERR, "socketMulticast() leave failed: %s (%d)", strerror(errno), errno);
error2:
	socketClose(socket);
error1:
	free(addr);
error0:
#ifdef __WIN32__
	pthread_exit(NULL);
#endif
	return NULL;
}

static const char *
kvm_filepath_multicast(kvm *self)
{
	return ((kvm_multicast *) self->_kvm)->map->filepath(((kvm_multicast *) self->_kvm)->map);
}

static int
kvm_open_multicast(kvm *self, const char *location, int mode)
{
	kvm_multicast *multicast;

	self->close = kvm_close_multicast;
	self->filepath = kvm_filepath_multicast;
	self->fetch = kvm_fetch_multicast;
	self->get = kvm_get_multicast;
	self->put = kvm_put_multicast;
	self->remove = kvm_remove_multicast;
#ifdef NOT_FINISHED
	self->first = kvm_first_multicast;
	self->next = kvm_next_multicast;
#else
	self->walk = kvm_walk_multicast;
#endif
	self->truncate = kvm_truncate_stub;
	self->sync = kvm_sync_stub;
	self->_mode = mode;

	self->begin = kvm_begin_stub;
	self->commit = kvm_commit_stub;
	self->rollback = kvm_rollback_stub;

	if ((multicast = malloc(sizeof (*multicast))) == NULL)
		return KVM_ERROR;

	self->_kvm = multicast;

	if ((multicast->group = socketAddressCreate(location, KVM_PORT)) == NULL)
		return KVM_ERROR;

	if ((multicast->socket = socketOpen(multicast->group, 0)) == NULL)
		return KVM_ERROR;

	location += strcspn(location, KVM_DELIM_S);
	location += *location == KVM_DELIM;

	if ((multicast->map = kvmOpen(self->_table, location, mode)) == NULL)
		return KVM_ERROR;

	multicast->running = 1;

#if defined(HAVE_PTHREAD_CREATE)
{
	pthread_t thread;

	if (pthread_create(&thread, NULL, kvm_listener_multicast, self)) {
		syslog(LOG_ERR, "failed to create multicast listener thread: %s, (%d)", strerror(errno), errno);
		socketClose(multicast->socket);
	}
}
#else
{
	pid_t child = fork();

	switch (child) {
	case 0:
		(void) kvm_listener_multicast(self);
		exit(0);
	case -1:
		syslog(LOG_ERR, "kvm=%s failed to create multicast listener: %s, (%d)", location, strerror(errno), errno);
		socketClose(multicast->socket);
		break;
	}
}
#endif

	return KVM_OK;
}

/***********************************************************************
 *** SQLite3 3.3.8+
 *** ./configure  --disable-tcl --enable-threadsafe --enable-static --enable-shared
 ***********************************************************************/

#ifdef HAVE_SQLITE3_H
# include <sqlite3.h>

#if SQLITE_VERSION_NUMBER < 3003009
# error "SQLite3 version 3.3.9 or better required."
#endif

#define USE_TEXT_COLUMNS
#ifdef USE_TEXT_COLUMNS
#define KVM_SQL_CREATE		"CREATE TABLE kvm ( k TEXT PRIMARY KEY, v TEXT );"
#else
#define KVM_SQL_CREATE		"CREATE TABLE kvm ( k BLOB PRIMARY KEY, v BLOB );"
#endif

#define KVM_SQL_SELECT_ONE	"SELECT v FROM kvm WHERE k=?1;"
#define KVM_SQL_SELECT_ALL	"SELECT k,v FROM kvm;"
#define KVM_SQL_INSERT		"INSERT INTO kvm (k,v) VALUES(?1, ?2);"
#define KVM_SQL_UPDATE		"UPDATE kvm SET v=?2 WHERE k=?1;"
#define KVM_SQL_REPLACE		"INSERT OR REPLACE INTO kvm (k,v) VALUES(?1, ?2);"
#define KVM_SQL_REMOVE		"DELETE FROM kvm WHERE k=?1;"
#define KVM_SQL_TRUNCATE	"DELETE FROM kvm;"
#define KVM_SQL_BEGIN		"BEGIN;"
#define KVM_SQL_COMMIT		"COMMIT;"
#define KVM_SQL_ROLLBACK	"ROLLBACK;"

typedef struct kvm_sql {
	char *path;
	sqlite3 *db;
	sqlite3_stmt *select_one;
	sqlite3_stmt *select_all;
	sqlite3_stmt *truncate;
	sqlite3_stmt *replace;
	sqlite3_stmt *remove;
	sqlite3_stmt *begin;
	sqlite3_stmt *commit;
	sqlite3_stmt *rollback;
} kvm_sql;

static int
kvm_sql_step(kvm_sql *sql, sqlite3_stmt *sql_stmt, const char *sql_stmt_text)
{
	int rc;
#ifdef HAVE_PTHREAD_SETCANCELSTATE
	int old_state;
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif
	/* Using the newer sqlite_prepare_v2() interface means that
	 * sqlite3_step() will return more detailed error codes. See
	 * sqlite3_step() API reference.
	 */
	while ((rc = sqlite3_step(sql_stmt)) == SQLITE_BUSY) {
		if (0 < debug)
			syslog(LOG_WARN, "sqlite db %s busy: %s", sql->path, sql_stmt_text);
#if defined(HAVE_PTHREAD_CREATE)
		pthreadSleep(1, 0);
#else
		sleep(1);
#endif
	}

	if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
		syslog(LOG_ERR, "sql=%s step error (%d): %s", sql->path, rc, sqlite3_errmsg(sql->db));
	}

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
#ifdef HAVE_PTHREAD_SETCANCELSTATE
	pthread_setcancelstate(old_state, NULL);
#endif
	return rc;
}

static void
kvm_close_sql(kvm *self)
{
	kvm_sql *sql;

	if (self != NULL) {
		if (self->_kvm != NULL) {
			sql = self->_kvm;

			if (sql->select_one != NULL)
				(void) sqlite3_finalize(sql->select_one);
			if (sql->select_all != NULL)
				(void) sqlite3_finalize(sql->select_all);
			if (sql->truncate != NULL)
				(void) sqlite3_finalize(sql->truncate);
			if (sql->replace != NULL)
				(void) sqlite3_finalize(sql->replace);
			if (sql->remove != NULL)
				(void) sqlite3_finalize(sql->remove);
			if (sql->begin != NULL)
				(void) sqlite3_finalize(sql->begin);
			if (sql->commit != NULL)
				(void) sqlite3_finalize(sql->commit);
			if (sql->rollback != NULL)
				(void) sqlite3_finalize(sql->rollback);
			sqlite3_close(sql->db);

			free(sql);
		}

		kvmClose(self);
	}
}

static int
kvm_get_sql(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
#ifdef USE_TEXT_COLUMNS
	if (sqlite3_bind_text(sql->select_one, 1, (const char *) key->data, key->size, SQLITE_STATIC) != SQLITE_OK)
		goto error1;
#else
	if (sqlite3_bind_blob(sql->select_one, 1, (const void *) key->data, key->size, SQLITE_STATIC) != SQLITE_OK)
		goto error1;
#endif
	switch (kvm_sql_step(sql, sql->select_one, KVM_SQL_SELECT_ONE)) {
	case SQLITE_DONE:
		rc = KVM_NOT_FOUND;
		break;
	case SQLITE_ROW:
		if (value != NULL) {
			value->size = sqlite3_column_bytes(sql->select_one, 0);
			if ((value->data = malloc(value->size+1)) == NULL)
				goto error1;

			/* Pass by value. */
			memcpy(value->data, sqlite3_column_blob(sql->select_one, 0), value->size);
			value->data[value->size] = '\0';
		}
		(void) sqlite3_reset(sql->select_one);
		rc = KVM_OK;
	}
	(void) sqlite3_clear_bindings(sql->select_one);
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_put_sql(kvm *self, kvm_data *key, kvm_data *value)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL || value == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
#ifdef USE_TEXT_COLUMNS
	if (sqlite3_bind_text(sql->replace, 1, (const char *) key->data, key->size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_text(sql->replace, 2, (const char *) value->data, value->size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error2;
#else
	if (sqlite3_bind_blob(sql->replace, 1, (const void *) key->data, key->size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error1;

	if (sqlite3_bind_blob(sql->replace, 2, (const void *) value->data, value->size, SQLITE_TRANSIENT) != SQLITE_OK)
		goto error2;
#endif

	if (kvm_sql_step(sql, sql->replace, KVM_SQL_REPLACE) == SQLITE_DONE)
		rc = KVM_OK;
error2:
	(void) sqlite3_clear_bindings(sql->replace);
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_remove_sql(kvm *self, kvm_data *key)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL || key == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
#ifdef USE_TEXT_COLUMNS
	if (sqlite3_bind_text(sql->remove, 1, (const char *) key->data, key->size, SQLITE_STATIC) != SQLITE_OK)
		goto error1;
#else
	if (sqlite3_bind_blob(sql->remove, 1, (const void *) key->data, key->size, SQLITE_STATIC) != SQLITE_OK)
		goto error1;
#endif
	if (kvm_sql_step(sql, sql->remove, KVM_SQL_REMOVE) == SQLITE_DONE)
		rc = KVM_OK;
	(void) sqlite3_clear_bindings(sql->remove);
error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_truncate_sql(kvm *self)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_sql_step(sql, sql->truncate, KVM_SQL_TRUNCATE) == SQLITE_DONE)
		rc = KVM_OK;
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_begin_sql(kvm *self)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_sql_step(sql, sql->begin, KVM_SQL_BEGIN) == SQLITE_DONE)
		rc = KVM_OK;
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_commit_sql(kvm *self)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_sql_step(sql, sql->commit, KVM_SQL_COMMIT) == SQLITE_DONE)
		rc = KVM_OK;
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_rollback_sql(kvm *self)
{
	int rc;
	kvm_sql *sql;

	rc = KVM_ERROR;

	if (self == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	if (kvm_sql_step(sql, sql->rollback, KVM_SQL_ROLLBACK) == SQLITE_DONE)
		rc = KVM_OK;
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;
}

static int
kvm_walk_sql(kvm *self, int (*func)(kvm_data *, kvm_data *, void *), void *data)
{
	int rc, ret;
	kvm_sql *sql;
	kvm_data key, value;

	rc = KVM_ERROR;

	if (self == NULL || func == NULL)
		goto error0;

	sql = self->_kvm;

	PTHREAD_MUTEX_LOCK(&self->_mutex);
	do {
		switch (ret = kvm_sql_step(sql, sql->select_all, KVM_SQL_SELECT_ALL)) {
		case SQLITE_ROW:
			key.size = sqlite3_column_bytes(sql->select_all, 0);
			key.data = (unsigned char *) sqlite3_column_blob(sql->select_all, 0);

			value.size = sqlite3_column_bytes(sql->select_all, 1);
			value.data = (unsigned char *) sqlite3_column_blob(sql->select_all, 1);

			switch ((*func)(&key, &value, data)) {
			case 0:
				goto error1;
			case 1:
				break;
			case -1:
#ifdef USE_TEXT_COLUMNS
				if (sqlite3_bind_text(sql->remove, 1, (const char *) key.data, key.size, SQLITE_STATIC) != SQLITE_OK)
					goto error1;
#else
				if (sqlite3_bind_blob(sql->remove, 1, (const void *) key.data, key.size, SQLITE_STATIC) != SQLITE_OK)
					goto error1;
#endif
				ret = kvm_sql_step(sql, sql->remove, KVM_SQL_REMOVE);
				(void) sqlite3_clear_bindings(sql->remove);
				if (ret != SQLITE_DONE)
					goto error1;
				break;
			}
			break;
		case SQLITE_DONE:
			rc = KVM_OK;
			break;
		}
	} while (ret == SQLITE_ROW);

error1:
	PTHREAD_MUTEX_UNLOCK(&self->_mutex);
error0:
	return rc;

}

static const char *
kvm_filepath_sql(kvm *self)
{
	return ((kvm_sql *) self->_kvm)->path;
}

static int
kvm_prepare_sql(kvm_sql *sql, const char *stmt, int stmt_size, sqlite3_stmt **stmt_out, const char **stmt_tail)
{
	int rc;

	while ((rc = sqlite3_prepare_v2(sql->db, stmt, stmt_size, stmt_out, stmt_tail)) == SQLITE_BUSY) {
		if (0 < debug)
			syslog(LOG_WARN, "sqlite db %s busy: %s", sql->path, stmt);
#if defined(HAVE_PTHREAD_CREATE)
		pthreadSleep(1, 0);
#else
		sleep(1);
#endif
	}

	return rc;
}

static int
kvm_open_sql(kvm *self, const char *location, int mode)
{
	int rc;
	char *error;
	kvm_sql *sql;
	size_t length;
	int sql_open_flags;

	sql_open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

	self->close = kvm_close_sql;
	self->filepath = kvm_filepath_sql;
	self->fetch = kvm_get_sql;
	self->get = kvm_get_sql;
	self->put = kvm_put_sql;
	self->remove = kvm_remove_sql;
	self->truncate = kvm_truncate_sql;
	self->walk = kvm_walk_sql;
	self->sync = kvm_sync_stub;
	self->_mode = mode;

	self->begin = kvm_begin_sql;
	self->commit = kvm_commit_sql;
	self->rollback = kvm_rollback_sql;

	if (mode & KVM_MODE_READ_ONLY) {
		self->put = kvm_put_stub;
		self->remove = kvm_remove_stub;
		sql_open_flags = SQLITE_OPEN_READONLY;
	}

	length = strlen(location);
	if ((sql = calloc(1, sizeof (*sql) + length + 1)) == NULL)
		return KVM_ERROR;

	self->_kvm = sql;
	memset(sql, 0 , sizeof (sql));
	sql->path = (char *) &sql[1];
	(void) TextCopy(sql->path, length+1, location);

	if ((rc = sqlite3_open_v2(sql->path, &sql->db, sql_open_flags, NULL)) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s open error: %s", sql->path, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}

	switch ((rc = kvm_prepare_sql(sql, KVM_SQL_SELECT_ONE, -1, &sql->select_one, NULL))) {
	case SQLITE_OK:
		break;
	case SQLITE_ERROR:
		/* Create the table if it doesn't exist? */
		if (sqlite3_exec(sql->db, KVM_SQL_CREATE, NULL, NULL, &error) != SQLITE_OK) {
			syslog(LOG_ERR, "sql=%s error: %s", sql->path, error);
			sqlite3_free(error);
			return KVM_ERROR;
		}

		/* Try again to prepare the statement. */
		if (kvm_prepare_sql(sql, KVM_SQL_SELECT_ONE, -1, &sql->select_one, NULL) == SQLITE_OK)
			break;
		/*@fallthrough@*/
	default:
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_SELECT_ONE, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}

	/* Using the newer sqlite_prepare_v2() interface will handle
	 * SQLITE_SCHEMA errors automatically.
	 */
	if (kvm_prepare_sql(sql, KVM_SQL_SELECT_ALL, -1, &sql->select_all, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_SELECT_ALL, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}
	if (kvm_prepare_sql(sql, KVM_SQL_TRUNCATE, -1, &sql->truncate, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_TRUNCATE, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}
	if (kvm_prepare_sql(sql, KVM_SQL_REPLACE, -1, &sql->replace, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_REPLACE, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}
	if (kvm_prepare_sql(sql, KVM_SQL_REMOVE, -1, &sql->remove, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_REMOVE, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}
	if (kvm_prepare_sql(sql, KVM_SQL_BEGIN, -1, &sql->begin, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_BEGIN, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}
	if (kvm_prepare_sql(sql, KVM_SQL_COMMIT, -1, &sql->commit, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_COMMIT, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}
	if (kvm_prepare_sql(sql, KVM_SQL_ROLLBACK, -1, &sql->rollback, NULL) != SQLITE_OK) {
		syslog(LOG_ERR, "sql=%s statement error: %s : %s", sql->path, KVM_SQL_ROLLBACK, sqlite3_errmsg(sql->db));
		return KVM_ERROR;
	}

	return KVM_OK;
}

#endif

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct kvm_method {
	int prefix_length;
	const char *prefix;
	int (*open)(kvm *self, const char *location, int mode);
} kvm_method;

static kvm_method kvm_open_table[] = {
#ifdef HAVE_DB_H
	{ 0, "/", kvm_open_db },
	{ sizeof ("db")-1, "db", kvm_open_db },
#endif
#ifdef HAVE_SQLITE3_H
	{ sizeof ("sql")-1, "sql", kvm_open_sql },
#endif
	{ sizeof ("hash")-1, "hash", kvm_open_hash },
	{ sizeof ("text")-1, "text", kvm_open_file },
	{ sizeof ("file")-1, "file", kvm_open_file },
	{ sizeof ("socketmap")-1, "socketmap", kvm_open_socket },
	{ sizeof ("multicast")-1, "multicast", kvm_open_multicast },
	{ 0, NULL, NULL }
};

void
kvmDebug(int level)
{
	debug = level;
}

/**
 * @param table_name
 *
 * @param map_location
 *	A pointer to a C string specifying the type and location of
 *	the key-value map. The following are currently supported:
 *
 *		NULL			(assumes hash!)
 *		hash!
 *		text!/path/map.txt	(read-only)
 *		file!/path/map.txt	(may contain binary)
 *		/path/map.db		(historical)
 *		db!/path/map.db
 *		db!btree!/path/map.db
 *		sql!/path/database
 *		socketmap!host,port
 *		socketmap!/path/local/socket
 *		multicast!group,port!map
 *
 * @param mode
 *	A bit mask of KVM_MODE_ flags.
 *
 * @return
 *	A pointer to a kvm structure on success, otherwise NULL. Its
 *	the caller's responsiblity to call the kvm's close method
 *	once done.
 */
kvm *
kvmOpen(const char *table_name, const char *map_location, int mode)
{
	kvm *self;
	kvm_method *method;

	if ((self = kvmCreate(table_name, map_location, mode)) == NULL)
		return NULL;

	if (map_location == NULL)
		map_location = "hash";

	for (method = kvm_open_table; method->prefix != NULL; method++) {
		if (0 < TextInsensitiveStartsWith(map_location, method->prefix)
		&& !isalnum(map_location[method->prefix_length])) {
			if ((*method->open)(self, map_location + method->prefix_length + (0 < method->prefix_length), mode))
				break;
			return self;
		}
	}

	if (self->close != NULL)
		(*self->close)(self);
	else
		kvmClose(self);

	return NULL;
}

#ifdef TEST_KVMD
/***********************************************************************
 *** kvmd
 ***********************************************************************/

#include <stdio.h>

#ifdef __sun__
# define _POSIX_PTHREAD_SEMANTICS
#endif
#include <signal.h>

#include <com/snert/lib/util/getopt.h>
#include <com/snert/lib/sys/pthread.h>

static char usage[] =
"usage: kvmd [-dsv][-p port][-t timeout] map ...\n"
"\n"
"-d\t\tstart as a background daemon process\n"
"-p port\t\tthe socket-map port number or path, default " KVM_PORT_S "\n"
"-s\t\tremain single threaded for testing\n"
"-t timeout\tsocket timeout in seconds, default 60\n"
"-v\t\tverbose logging to the user log\n"
"\n"
"A map is a string of the form:\n"
"\n"
"  table-name" KVM_DELIM_S "[read-only" KVM_DELIM_S "]type" KVM_DELIM_S "[sub-type" KVM_DELIM_S "]location\n"
"\n"
"The following forms of type" KVM_DELIM_S "[sub-type" KVM_DELIM_S "]location are supported:\n"
"\n"
"  hash" KVM_DELIM_S "\n"
"  text" KVM_DELIM_S "/path/map.txt\n"
"  file" KVM_DELIM_S "/path/map.txt\n"
#ifdef HAVE_DB_H
"  db" KVM_DELIM_S "/path/map.db\n"
"  db" KVM_DELIM_S "btree" KVM_DELIM_S "/path/map.db\n"
#endif
#ifdef HAVE_SQLITE3_H
"  sql" KVM_DELIM_S "/path/database\n"
#endif
"  socketmap" KVM_DELIM_S "host[,port]\n"
"  socketmap" KVM_DELIM_S "/path/local/socket\n"
"  socketmap" KVM_DELIM_S "123.45.67.89:port\n"
"  socketmap" KVM_DELIM_S "[2001:0DB8::1234]:port\n"
"  multicast" KVM_DELIM_S "multicast-ip:port" KVM_DELIM_S "map\n"
"  multicast" KVM_DELIM_S "232.12.34.56:port" KVM_DELIM_S "map\n"
"  multicast" KVM_DELIM_S "[FF12::1234:5678]:port" KVM_DELIM_S "map\n"
"\n"
"The default port for socketmap and multicast locations is " KVM_PORT_S ".\n"
"\n"
LIBSNERT_COPYRIGHT "\n"
;

/* 232.173.190.239 , FF02::DEAD:BEEF */

kvm **maps;
int port = KVM_PORT;
int daemon_mode;
int single_thread;
long timeout = SOCKET_CONNECT_TIMEOUT;
const char *host = "0.0.0.0";

void
signalExit(int signum)
{
	signal(signum, SIG_IGN);
	exit(0);
}

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

kvm *
find_table(const char *table)
{
	kvm **map;

	for (map = maps; *map != NULL; map++) {
		if (strcmp(table, (*map)->_table) == 0)
			return *map;
	}

	return NULL;
}

int
request(Socket2 *client, char *addr)
{
	int rc;
	kvm *map;
	kvm_data query, key, value;

	if ((rc = kvm_recv(client, &query.data, &query.size)) == KVM_ERROR) {
		kvm_send(client, (unsigned char *) "PERM table read error", sizeof ("PERM table read error")-1);
		goto error0;
	}

	if (rc == KVM_EOF)
		goto error0;

	/* Check for sendmail socket map GET semantics. */
	if ((key.data = (unsigned char *) strchr((char *) query.data, ' ')) != NULL) {
		char number[20];
		int number_length;

		if (0 < debug)
			syslog(LOG_INFO, "%s FETCH \"%s\"...", addr, query.data);

		*key.data++ = '\0';
		key.size = query.size - (key.data - query.data);

		if ((map = find_table((char *) query.data)) == NULL) {
			kvm_send(client, (unsigned char *) "PERM invalid table", sizeof ("PERM invalid table")-1);
			goto error1;
		}

		switch (map->get(map, &key, &value)) {
		case KVM_ERROR:
			(void) kvm_send(client, (unsigned char *) "PERM", sizeof ("PERM")-1);
			break;
		case KVM_NOT_FOUND:
			(void) kvm_send(client, (unsigned char *) "NOTFOUND", sizeof ("NOTFOUND")-1);
			break;
		case KVM_OK:
			number_length = snprintf(number, sizeof (number), "%lu:", value.size + sizeof ("OK ")-1);

			if (socketWrite(client, (unsigned char *) number, number_length) != number_length)
				;
			else if (socketWrite(client, (unsigned char *) "OK ", sizeof ("OK ")-1) != sizeof ("OK ")-1)
				;
			else if (socketWrite(client, value.data, value.size) != value.size)
				;
			else if (socketWrite(client, (unsigned char *) ",", 1) != 1)
				;
			else if (0 < debug)
				syslog(LOG_INFO, "%s FETCH \"%s\" value=\"%s\"", addr, query.data, value.data);

			free(value.data);
			break;
		}

		goto error1;
	}

	if ((map = find_table((char *) query.data)) == NULL) {
		kvm_send(client, (unsigned char *) "PERM invalid table", sizeof ("PERM invalid table")-1);
		goto error1;
	}

	/* Otherwise handle extended socket map semantics. */
	free(query.data);

	if ((rc = kvm_recv(client, &query.data, &query.size)) != KVM_OK) {
		kvm_send(client, (unsigned char *) "PERM command read error", sizeof ("PERM command read error")-1);
		goto error0;
	}

	key.data = NULL;
	value.data = NULL;

	if (TextInsensitiveCompare("GET", (char *) query.data) == 0) {
		if (0 < debug)
			syslog(LOG_INFO, "%s GET %s", addr, map->_table);

		if ((rc = kvm_recv(client, &key.data, &key.size)) != KVM_OK) {
			kvm_send(client, (unsigned char *) "PERM key read error", sizeof ("PERM key read error")-1);
			goto error1;
		}

		switch (map->get(map, &key, &value)) {
		case KVM_ERROR:
			(void) kvm_send(client, (unsigned char *) "PERM", sizeof ("PERM")-1);
			syslog(LOG_ERR, "GET '%s' failed", key.data);
			break;
		case KVM_NOT_FOUND:
			(void) kvm_send(client, (unsigned char *) "NOTFOUND", sizeof ("NOTFOUND")-1);
			break;
		case KVM_OK:
			kvm_send(client, (unsigned char *) "OK", 2);
			(void) kvm_send(client, value.data, value.size);
			free(value.data);
			break;
		}

	} else if (TextInsensitiveCompare("PUT", (char *) query.data) == 0) {
		if (0 < debug)
			syslog(LOG_INFO, "%s PUT %s", addr, map->_table);

		if ((rc = kvm_recv(client, &key.data, &key.size)) != KVM_OK) {
			kvm_send(client, (unsigned char *) "PERM key read error", sizeof ("PERM key read error")-1);
			goto error1;
		}

		if ((rc = kvm_recv(client, &value.data, &value.size)) != KVM_OK) {
			kvm_send(client, (unsigned char *) "PERM value read error", sizeof ("PERM value read error")-1);
			goto error2;
		}

		if (map->put(map, &key, &value) == KVM_ERROR) {
			syslog(LOG_ERR, "PUT '%s' '%s' failed", key.data, value.data);
			(void) kvm_send(client, (unsigned char *) "PERM put failed", sizeof ("PERM put failed")-1);
		} else {
			kvm_send(client, (unsigned char *) "OK", 2);
		}
	} else if (TextInsensitiveCompare("REMOVE", (char *) query.data) == 0) {
		if (0 < debug)
			syslog(LOG_INFO, "%s REMOVE %s", addr, map->_table);

		if ((rc = kvm_recv(client, &key.data, &key.size)) != KVM_OK) {
			kvm_send(client, (unsigned char *) "PERM key read error", sizeof ("PERM key read error")-1);
			goto error1;
		}

		if (map->remove(map, &key) == KVM_ERROR) {
			syslog(LOG_ERR, "REMOVE '%s' failed", key.data);
			(void) kvm_send(client, (unsigned char *) "PERM remove failed", sizeof ("PERM remove failed")-1);
		} else {
			kvm_send(client, (unsigned char *) "OK", 2);
		}
	} else {
		if (0 < debug)
			syslog(LOG_INFO, "%s invalid %s \"%s\"", addr, map->_table, query.data);
		(void) kvm_send(client, (unsigned char *) "PERM invalid operation", sizeof ("PERM invalid operation")-1);
	}
error2:
	free(key.data);
error1:
	free(query.data);
error0:
	return rc == KVM_OK;
}

void *
process(void *data)
{
	char addr[256];
	Socket2 *client = data;

	socketSetNonBlocking(client, 1);
	socketSetTimeout(client, timeout);
	(void) socketAddressGetString(&client->address, 1, addr, sizeof (addr));

	while (request(client, addr))
		;

	socketClose(client);

#ifdef __WIN32__
	pthread_exit(NULL);
#endif
	return NULL;
}

void
close_maps(void)
{
	kvm **map;

	for (map = maps; *map != NULL; map++)
		(*map)->close(*map);
}

void
init_maps(int argc, char **argv)
{
	int argi, mode;
	char *table, *colon;

	if ((maps = malloc(argc + 1)) == NULL) {
		syslog(LOG_ERR, "%s (%d)", strerror(errno), errno);
		exit(71);
	}

	(void) atexit(close_maps);

	for (argi = 0; argi < argc; argi++) {
		mode = 0;
		table = argv[argi];

		if ((colon = strchr(table, KVM_DELIM)) == NULL) {
			(void) fprintf(stderr, usage);
			exit(64);
		}

		*colon++ = '\0';

		if (0 < TextInsensitiveStartsWith(colon, "read-only" KVM_DELIM_S)) {
			colon += sizeof ("read-only" KVM_DELIM_S)-1;
			mode |= KVM_MODE_READ_ONLY;
		}

		if ((maps[argi] = kvmOpen(table, colon, mode)) == NULL) {
			syslog(LOG_ERR, "kvmOpen(\"%s\", \"%s\", %x) failed: %s (%d)", table, colon, mode, strerror(errno), errno);
			exit(71);
		}
	}

	maps[argi] = NULL;
}

int
main(int argc, char **argv)
{
	int ch;
	SocketAddress *addr;
	Socket2 *server, *client;

	openlog("kvmd", LOG_PID, LOG_USER);

	while ((ch = getopt(argc, argv, "dsh:p:t:v")) != -1) {
		switch (ch) {
		case 'd':
			daemon_mode = 1;
			break;
		case 's':
			single_thread = 1;
			break;
		case 'p':
			if (*optarg == '/')
				host = optarg;
			else
				port = (int) strtol(optarg, NULL, 10);
			break;
		case 't':
			timeout = strtol(optarg, NULL, 10) * 1000;
			break;
		case 'v':
			LogSetProgramName("kvmd");
			LogOpen("(standard error)");
			LogSetLevel(LOG_DEBUG);
			socketSetDebug(1);
			kvmDebug(1);
			break;
		default:
			(void) fprintf(stderr, usage);
			exit(64);
		}
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, usage);
		exit(64);
	}

	if (socketInit()) {
		syslog(LOG_ERR, "socketInit() %s (%d)", strerror(errno), errno);
		exit(71);
	}

	init_maps(argc - optind, argv + optind);

#ifdef SIGPIPE
	(void) signal(SIGPIPE, SIG_IGN);
#endif
	(void) signal(SIGTERM, signalExit);
	(void) signal(SIGINT, signalExit);

	if ((addr = socketAddressCreate(host, port)) == NULL) {
		syslog(LOG_ERR, "socketAddressCreate() failed");
		exit(71);
	}

	if ((server = socketOpen(addr, 1)) == NULL) {
		syslog(LOG_ERR, "socketOpen() failed");
		exit(71);
	}

	socketSetTimeout(server, timeout);

	if (socketSetReuse(server, 1)) {
		syslog(LOG_ERR, "socketSetResuse() of socketmap server failed");
		exit(71);
	}

	if (socketServer(server, 10)) {
		syslog(LOG_ERR, "socketServer() of socketmap server failed");
		exit(71);
	}

	syslog(LOG_INFO, "kvmd/" _VERSION " " LIBSNERT_COPYRIGHT);

#ifdef HAVE_SETSID
	if (daemon_mode) {
		pid_t ppid;

		if ((ppid = fork()) < 0) {
			syslog(LOG_ERR, "process fork failed: %s (%d)", strerror(errno), errno);
			exit(1);
		}

		if (ppid != 0)
			exit(0);

		if (setsid() == -1) {
			syslog(LOG_ERR, "set process group ID failed: %s (%d)", strerror(errno), errno);
			exit(1);
		}
	}
#endif
	syslog(LOG_INFO, "listening on port %d", port);

	for (;;) {
		if ((client = socketAccept(server)) == NULL)
			continue;

		if (single_thread) {
			(void) process(client);
			continue;
		}

#if defined(HAVE_PTHREAD_CREATE)
{
		pthread_t thread;

		if (pthread_create(&thread, NULL, process, client)) {
			syslog(LOG_ERR, "failed to create thread: %s, (%d)", strerror(errno), errno);
			socketClose(client);
		}
}
#else
{
		pid_t child = fork();

		switch (child) {
		case 0:
			(void) process(client);
			exit(0);
		case -1:
			/* Thread failed to start, abandon the client. */
			syslog(LOG_ERR, "failed to create child: %s, (%d)", strerror(errno), errno);
			socketShutdown(client, SHUT_WR);
			break;
		default:
			syslog(LOG_DEBUG, "new child worker=%d\n", child);
		}

		/* Close the socket without calling shutdown(). */
		closesocket(client->fd);
		free(client);
}
#endif
	}

	/*@notreached@*/

	return 0;
}

#endif
/***********************************************************************
 *** END
 ***********************************************************************/

