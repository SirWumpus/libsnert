/*
 * Pushback.h
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_io_Pushback_h__
#define __com_snert_lib_io_Pushback_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <com/snert/lib/type/Object.h>

typedef /*@abstract@*/ struct pushback {
	OBJECT_OBJECT;
	
	/**
	 * Skip the next N bytes.
	 *
	 * @param n
	 *	Number of bytes of input to skip.
	 *
	 * @return
	 *	The actual number of bytes skipped.
	 */
	long (*skip)(long n);
	
	/**
	 * Return number of bytes available without blocking for input.
	 *
	 * @return
	 *	Number of bytes readily available.
	 */
	long (*available)(void);

	/**
	 * Get the next byte from the stream.
	 * 
	 * @return
	 *	A byte value (0 to 255), otherwise -1 for EOF or error.
	 */
	int (*get)(void);

	/**
	 * Push back one byte to the input stream (last in, frist out). 
	 *
	 * @param byte
	 *	A byte to be inserted ahead of all other input.
	 *
	 * @return
	 *	Zero on success, otherwise -1 on error.
	 */
	int (*unget)(int byte);

	/**
	 * Read from the stream into a buffer upto length bytes.
	 *
	 * @param buffer
	 *	The buffer in which to place input bytes.
	 *
	 * @param length
	 *	The length of the buffer to fill.
	 *
	 * @return
	 *	Actually number of bytes read, otherwise -1 on EOF
	 *	or error.
	 */
	long (*read)(/*@out@*/ unsigned char *buffer, long length);

	/**
	 * Push back these length bytes to the input stream (last in, first out).
	 * Note these bytes are copied into an internal buffer ahead of any
	 * other bytes already pushed back.
	 *
	 * @param bytes
	 *	Buffer of bytes to inserted ahead of all other input.
	 *
	 * @param length
	 *	Length of bytes to pushback.
	 */
	int (*unread)(unsigned char *bytes, long length);

	/**
	 * Allows a push back input stream, typically one that was reading
	 * from a byte buffer source, to change the source to a different
	 * byte buffer source. This allows for simple chaining of byte 
	 * buffers without copying the data.
	 *
	 * If the previous source was an open file, it will be closed.
	 * Bytes already in the push back internal buffer are retained
	 * and read ahead of the byte buffer source.
	 *
	 * @param bytes
	 *	Buffer of bytes to inserted ahead of all other input.
	 *
	 * @param length
	 *	Length of bytes to pushback.
	 */
	void (*setSourceBytes)(unsigned char *bytes, long length);

	void (*setSourceFile)(FILE *fp);
	void (*setSourceFd)(int fd);

	/*
	 * Private 
	 */
	unsigned char *_sourceBase;
	long _sourceLength;
	long _sourceIndex;
	FILE *_sourceFp;
	int _sourceFd;
	
	unsigned char *_holdBase;
	long _holdCapacity;
	long _holdLength;
	long _holdIndex;
} *Pushback;

/*@-exportlocal@*/

extern /*@only@*//*@null@*/ Pushback PushbackCreateFromBytes(unsigned char *buffer, long length);
extern /*@only@*//*@null@*/ Pushback PushbackCreateFromFile(FILE *fp);
extern /*@only@*//*@null@*/ Pushback PushbackCreateFromFd(int fd);

extern void PushbackSetSourceBytes(Pushback self, unsigned char *bytes, long length);
extern void PushbackSetSourceFile(Pushback self, FILE *fp);
extern void PushbackSetSourceFd(Pushback self, int fd);

extern void PushbackDestroy(/*@only@*//*@null@*/ void *self);

extern int PushbackSkip(Pushback self);
extern long PushbackAvailable(Pushback self);

extern int PushbackGet(Pushback self);
extern int PushbackUnget(Pushback self, int byte);

extern long PushbackRead(Pushback self, unsigned char *buffer, long length);
extern int PushbackUnread(Pushback self, unsigned char *bytes, long length);

/*@=exportlocal@*/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_io_Pushback_h__ */
