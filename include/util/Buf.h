/*
 * Buf.h
 *
 * Copyright 2001, 2008 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_Buf_h__
#define __com_snert_lib_util_Buf_h__	1

#ifndef BUFSIZ
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_FREEFN_T
typedef void (*FreeFn)(void *);
#endif

typedef struct {
	FreeFn free;
	unsigned char *bytes;
	size_t size;
	size_t length;

	/* Not used. Available for public use. */
	size_t offset;
} Buf;

/**
 * Create a new and initialised Buf object.
 *
 * @param capacity
 *	The initial capacity for the Buf object.
 *
 * @return
 *	A Buf object; otherwise null on error.
 */
extern Buf *BufCreate(size_t capacity);

/**
 * Create a new and initialised Buf object.
 *
 * @param string
 *	A previously allocated C string that Buf object will take
 *	responsibility of.
 *
 * @return
 *	A Buf object; otherwise null on error.
 */
extern Buf *BufAssignString(char *);

/**
 * Create a new and initialised BUf object.
 *
 * @param string
 *	A C string from which to initialise the Buf object.
 *	The bytes are copied from the source string.
 *
 * @return
 *	A Buf object; otherwise null on error.
 */
extern Buf *BufCopyString(const char *);

/**
 * Create a new and initialised Buf object.
 *
 * @param buf
 *	A Buf object from which to initialise the a new Buf object.
 *	The bytes are copied from the source buffer.
 *
 * @param offset
 *	An offset into Buf from which to copy bytes.
 *
 * @param length
 *	The number of bytes from Buf to copy.
 *
 * @return
 *	A Buf object; otherwise null on error.
 */
extern Buf *BufCopyBuf(Buf *, size_t offset, size_t length);

/**
 * Create a new and initialised Buf object.
 *
 * @param source
 *	A buffer of bytes from which to initialise the Buf object.
 *	The bytes are copied from the source buffer.
 *
 * @param offset
 *	An offset into the buffer from which to copy bytes.
 *
 * @param length
 *	The number of bytes from the buffer offset to copy.
 *
 * @return
 *	A Buf object; otherwise null on error.
 */
extern Buf *BufCopyBytes(unsigned char *source, size_t offset, size_t length);

extern void BufDestroy(void *);

/*
 * Convert a Buf object into byte string.
 * The original Buf is destroyed in the process.
 */
extern unsigned char *BufAsBytes(Buf *);

/*
 * Access fields.
 */
extern size_t BufLength(Buf *);
extern size_t BufCapacity(Buf *);
extern unsigned char *BufBytes(Buf *);
extern void BufSetLength(Buf *, size_t);

/*
 * Extract bytes.
 */
extern char *BufToString(Buf *);
extern int BufGetByte(Buf *, size_t offset);
extern unsigned char *BufGetBytes(Buf *, size_t offset, size_t length);

/*
 * Replace bytes.
 */
extern void BufSetByte(Buf *, size_t, int);
extern void BufSetBytes(Buf *, size_t, unsigned char *,  size_t offset, size_t length);

/*
 * Compare
 */
extern int BufCompare(Buf *, Buf *);
extern int BufCompareBuf(Buf *, size_t, Buf *, size_t, size_t);

/*
 * Append. Return 0 for success or -1 on error.
 */
extern int BufAddBuf(Buf *, Buf *, size_t offset, size_t length);
extern int BufAddByte(Buf *, int);
extern int BufAddBytes(Buf *, unsigned char *, size_t length);
extern int BufAddString(Buf *, const char *);
extern int BufAddReadLine(Buf *, int, long);
extern int BufAddInputLine(Buf *, FILE *, long);
extern int BufAddSigned(Buf *, long value, int base);
extern int BufAddUnsigned(Buf *, unsigned long value, int base);

extern int BufInsertBytes(Buf *, size_t target, unsigned char *, size_t source, size_t length);

/*
 * In-place modifications
 */
extern void BufReverse(Buf *, size_t offset, size_t length);
extern void BufToLower(Buf *, size_t offset, size_t length);
extern void BufToUpper(Buf *, size_t offset, size_t length);
extern void BufTrim(Buf *a);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Buf_h__ */
