/*
 * Shared.h
 *
 * Copyright 2001, 2004 by Anthony Howe.  All rights reserved.
 *
 * Ralf S. Engelschall's MM Library, while nice, appears to be incomplete:
 * no POSIX semaphores, permission issues for SysV, and constant need to
 * lock/unlock sections before doing a memory allocation or free. The last
 * concerns me greatly, because some specialised routines really want
 * monitor-like behaviour, only one user in the entire routine at a time.
 */

#ifndef __com_snert_lib_sys_Shared_h__
#define __com_snert_lib_sys_Shared_h__	1

#ifdef __cplusplus
extern "C" {
#endif

extern void *SharedCreate(unsigned long size);
extern int SharedPermission(void *block, int mode, int user, int group);
extern void SharedDestroy(void *block);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_Shared_h__ */
