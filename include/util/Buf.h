/*
 * Buf.h
 *
 * Copyright 2001, 2012 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_Buf_h__
#define __com_snert_lib_util_Buf_h__	1

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAVE_FREEFN_T
typedef void (*FreeFn)(void *);
#endif

typedef struct {
	FreeFn free;
	size_t size;
	size_t length;
	size_t offset; 		/* Not used by Buf*(). For public use. */
	unsigned char *bytes;
} Buf;

/**
 * @param buf
 *	A Buf structure to be initialised.
 *
 * @param size
 *	The initial capacity for the Buf.
 *
 * @return
 *	Zero on success, otherwise -1 on error.
 */
extern int BufInit(Buf *buf, size_t size);

/**
 * @param _buf
 *	A Buf structure, previously initialised by BufInit(),
 *	to be cleaned up.
 */
extern void BufFini(void *_buf);

/**
 * @param size
 *	The initial capacity for the Buf.
 *
 * @return
 *	A pointer to a Buf; otherwise null on error. Its the
 *	caller's responsibility to BufFree() this pointer when done.
 */
extern Buf *BufCreate(size_t capacity);

/**
 * @param _buf
 *	A Buf structure, previously initialised by BufInit(),
 *	to be freed.
 */
#define BufDestroy BufFree
extern void BufFree(void *);

/*
 * Convert a Buf object into byte string.
 * The original Buf is destroyed in the process.
 */
extern unsigned char *BufAsBytes(Buf *);

/*
 * Access fields.
 */
#define BufCapacity BufSize
extern int BufSetSize(Buf *, size_t);
extern int BufSetLength(Buf *, size_t);

#ifdef BUF_FIELD_FUNCTIONS
extern size_t BufSize(Buf *);
extern size_t BufLength(Buf *);
extern unsigned char *BufBytes(Buf *);
extern void BufSetOffset(Buf *, size_t);
#else
#define BufBytes(buf)			(buf)->bytes
#define BufSize(buf)			(buf)->size
#define BufLength(buf)			(buf)->length
#define BufOffset(buf)			(buf)->offset
#define BufSetOffset(buf, off)		(buf)->offset = (off)
#endif

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
extern int BufAddInputLine(Buf *, FILE *, long); /*** DEPRICATED ***/
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
