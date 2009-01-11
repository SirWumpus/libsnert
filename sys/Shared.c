/*
 * Shared.c
 *
 * Copyright 2001, 2004 by Anthony Howe.  All rights reserved.
 *
 * Ralf S. Engelschall's MM Library, while nice, appears to be incomplete:
 * no POSIX semaphores, permission issues for SysV, and constant need to
 * lock/unlock sections before doing a memory allocation or free. The last
 * concerns me greatly, because some specialised routines really want
 * monitor-like behaviour, only one user in the entire routine at a time.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#define UNKNOWN_API		0
#define SYSTEMV_API		1
#define POSIX_API		2
#define MALLOC_API		3
#define WIN32_API		3
#define MMAP_ANON		4

#include <string.h>
#include <sys/types.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/sys/Shared.h>

#if ! defined(SHARED_MEMORY_API) || SHARED_MEMORY_API == UNKNOWN_API
#error "No shared memory support defined."
#endif

#if SHARED_MEMORY_API == MMAP_ANON
#include <fcntl.h>
#include <sys/mman.h>
struct shared {
	unsigned long size;
};
#endif

#if SHARED_MEMORY_API == POSIX_API
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
struct shared {
	unsigned long size;
};
#endif

#if SHARED_MEMORY_API == SYSTEMV_API
#include <sys/shm.h>
struct shared {
	unsigned long size;
	int id;
};
#endif

#if SHARED_MEMORY_API == MALLOC_API
/* Assuming threads instead of processes. */
#include <stdlib.h>
struct shared {
	unsigned long size;
};
#endif

void *
SharedCreate(unsigned long size)
{
	size += sizeof (struct shared);

#if SHARED_MEMORY_API == MMAP_ANON
{
	struct shared *share;

	share = (struct shared *) mmap(
		(void *) 0, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, (off_t) 0
	);

	if (share == (struct shared *) MAP_FAILED)
		return (void *) 0;

	(void) memset(share + 1, 0, size - sizeof (struct shared));
	share->size = size;

	return (void *) &share[1];
}
#endif
#if SHARED_MEMORY_API == POSIX_API
{
	int fd;
	struct shared *share;

	/* For some reason, using shm_open() and mmap() on Solaris 5.7,
	 * causes a SIGBUS when the shared memory address returned from
	 * mmap() is read or written, despite the fact that we don't get
	 * any errors from shm_open() and mmap().
	 *
	 * Since POSIX_API mmap() works with file objects and most systems
	 * will probably come with a /dev/zero, we can use this instead,
	 * which works happily on Solaris 5.7.
	 */
	if ((fd = open("/dev/zero", O_RDWR, 0600)) < 0)
		return (void *) 0;

	share = (struct shared *) mmap(
		(void *) 0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t) 0
	);
	(void) close(fd);

	if (share == (struct shared *) MAP_FAILED)
		return (void *) 0;

	(void) memset(share + 1, 0, size - sizeof (struct shared));
	share->size = size;

	return (void *) &share[1];
}
#endif
#if SHARED_MEMORY_API == SYSTEMV_API
{
	int id;
	struct shared *share;

	if ((id = shmget(IPC_PRIVATE, size, IPC_CREAT|SHM_R|SHM_W)) < 0)
		return (void *) 0;

	if ((share = shmat(id, (char *) 0, 0)) == (struct shared *) -1) {
		(void) shmctl(id, IPC_RMID, NULL);
		return (void *) 0;
	}

	(void) memset(share + 1, 0, size - sizeof (struct shared));
	share->size = size;
	share->id = id;

	return (void *) &share[1];
}
#endif
#if SHARED_MEMORY_API == MALLOC_API
{
	struct shared *share;

	/* Assuming threads instead of processes. */
	if ((share = malloc(size)) != (struct shared *) 0) {
		share->size = size;
		return (void *) &share[1];
	}
}
#endif
	return (void *) 0;
}

int
SharedPermission(void *block, int mode, int user, int group)
{
#if SHARED_MEMORY_API == MMAP_ANON
	/* Do nothing. */
#endif
#if SHARED_MEMORY_API == POSIX_API
	/* Do nothing. */
#endif
#if SHARED_MEMORY_API == SYSTEMV_API
{
	struct shared *share;
	struct shmid_ds shmbuf;

	if (block == (void *) 0)
		return -1;

	share = &((struct shared *) block)[-1];

        if (shmctl(share->id, IPC_STAT, &shmbuf) != 0)
                return -1;
        shmbuf.shm_perm.uid = user;
        shmbuf.shm_perm.gid = group;
        shmbuf.shm_perm.mode = mode;
        if (shmctl(share->id, IPC_SET, &shmbuf) != 0)
                return -1;
}
#endif
#if SHARED_MEMORY_API == MALLOC_API
	/* Do nothing. */
#endif

	return 0;
}

void
SharedDestroy(void *block)
{
	struct shared *share;

	if (block == (void *) 0)
		return;

	share = &((struct shared *) block)[-1];

#if SHARED_MEMORY_API == MMAP_ANON
	(void) munmap((caddr_t) share, share->size);
#endif
#if SHARED_MEMORY_API == POSIX_API
	(void) munmap((caddr_t) share, share->size);
#endif
#if SHARED_MEMORY_API == SYSTEMV_API
{
	int id = share->id;
	(void) shmdt(share);
	(void) shmctl(id, IPC_RMID, 0);
}
#endif
#if SHARED_MEMORY_API == MALLOC_API
	/* Assuming threads instead of processes. */
	free(share);
#endif
}

unsigned long
SharedGetSize(void *block)
{
	struct shared *share;

	if (block == (void *) 0)
		return 0;

	share = &((struct shared *) block)[-1];

	return share->size;
}

