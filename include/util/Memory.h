/*
 * Memory.h
 *
 * Copyright 2001, 2004 by Anthony Howe.  All rights reserved.
 *
 * A generic memory manager that can be used to allocate memory
 * from larger memory blocks allocated from the heap or shared
 * memory. This is a fairly simple technique and is patterned
 * after K&R2 page 185.
 */

#ifndef __com_snert_lib_util_Memory_h__
#define __com_snert_lib_util_Memory_h__		1

#ifdef __cplusplus
extern "C" {
#endif

extern void *MemoryCreate(void *block, long size);
extern void MemoryDestroy(void *header);

extern void *MemoryAllocate(void *header, long size);
extern void *MemoryReallocate(void *header, void *chunk, long size);
extern void *MemoryResize(void *header, void *chunk, long size);

extern int MemoryFree(void *header, void *chunk);
extern void MemorySet(void *chunk, int value);
extern long MemorySizeOf(void *chunk);

extern long MemoryAvailable(void *header);
extern long MemoryVerifySize(void *header);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_Memory_h__ */
