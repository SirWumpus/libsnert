/*
 * Mutex.c
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
#define FCNTL_API		3
#define FLOCK_API		4
#define WIN32_API		5

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <com/snert/lib/sys/Mutex.h>

#ifdef DEBUG_MALLOC
# include <com/snert/lib/util/DebugMalloc.h>
#endif

#if ! defined(SERIALIZATION_API) || SERIALIZATION_API == UNKNOWN_API
#error "No mutex support defined."
#endif

#if SERIALIZATION_API == FCNTL_API
#include <unistd.h>
struct mutex {
	int fd;
	struct flock lock;
	struct flock unlock;
	char lockfile[1];
};
#endif

#if SERIALIZATION_API == FLOCK_API
#include <sys/file.h>
#include <unistd.h>

#ifndef LOCK_SH
/* Operations for flock(). */
#define LOCK_SH		1    /* Shared lock.  */
#define LOCK_EX		2    /* Exclusive lock.  */
#define LOCK_UN		8    /* Unlock.  */
#endif

struct mutex {
	int fd;
	pid_t pid;
	char lockfile[1];
};
#endif

#if SERIALIZATION_API == POSIX_API
#include <semaphore.h>
#include <unistd.h>

struct mutex {
	sem_t sem;
	char lockfile[1];
};
#endif

#if SERIALIZATION_API == SYSTEMV_API
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <unistd.h>

struct mutex {
	int id;
	struct sembuf lock;
	struct sembuf unlock;
	char lockfile[1];
};
#endif

#if SERIALIZATION_API == WIN32_API
#include <windows.h>
struct mutex {
	HANDLE handle;
};
#endif


void *
MutexCreate(const char *lockfile)
{
	struct mutex *mex;

	if (lockfile == NULL)
		return NULL;

#if SERIALIZATION_API == FCNTL_API
	if ((mex = malloc(sizeof *mex + strlen(lockfile))) != (struct mutex *) 0) {
		mex->lock.l_whence = SEEK_SET;		/* from current point */
		mex->lock.l_start = 0;			/* -"- */
		mex->lock.l_len = 0;			/* until end of file */
		mex->lock.l_type = F_WRLCK;		/* set exclusive/write lock */
		mex->lock.l_pid = 0;			/* pid not actually interesting */

		mex->unlock.l_whence = SEEK_SET;	/* from current point */
		mex->unlock.l_start = 0;		/* -"- */
		mex->unlock.l_len = 0;			/* until end of file */
		mex->unlock.l_type = F_UNLCK;		/* set exclusive/write lock */
		mex->unlock.l_pid = 0;			/* pid not actually interesting */

		strcpy(mex->lockfile, lockfile);

		(void) unlink(lockfile);

		if (0 <= (mex->fd = open(lockfile, O_CREAT|O_RDWR, 0600))) {
			return (void *) mex;
		}
		free(mex);
	}
#endif
#if SERIALIZATION_API == FLOCK_API
	if ((mex = malloc(sizeof *mex + strlen(lockfile))) != (struct mutex *) 0) {
		strcpy(mex->lockfile, lockfile);
		(void) unlink(lockfile);

		if (0 <= (mex->fd = open(lockfile, O_CREAT|O_RDWR, 0600))) {
			mex->pid = getpid();
			return (void *) mex;
		}
		free(mex);
	}
#endif
#if SERIALIZATION_API == POSIX_API
	if ((mex = malloc(sizeof *mex + strlen(lockfile))) != (struct mutex *) 0) {
		strcpy(mex->lockfile, lockfile);

		if (sem_init(&mex->sem, 1, 1) == 0) {
# ifdef ALWAYS_CREATE_MUTEX_FILE
			int fd;

			if (0 <= (fd = open(lockfile, O_CREAT|O_RDWR, 0600)))
				(void) close(fd);
# endif
			return mex;
		}
		free(mex);
	}
#endif
#if SERIALIZATION_API == SYSTEMV_API
{
	union semun ick = { 0 };

	if ((mex = malloc(sizeof *mex + strlen(lockfile))) != (struct mutex *) 0) {
		mex->lock.sem_op = 1;
		mex->lock.sem_num = 0;
		mex->lock.sem_flg = SEM_UNDO;

		mex->unlock.sem_op = -1;
		mex->unlock.sem_num = 0;
		mex->unlock.sem_flg = SEM_UNDO;

		strcpy(mex->lockfile, lockfile);

		mex->id = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|S_IRUSR|S_IWUSR);
		if (mex->id < 0 && errno == EEXIST)
			mex->id = semget(IPC_PRIVATE, 1, IPC_EXCL|S_IRUSR|S_IWUSR);
		if (0 <= mex->id) {
			if (semctl(mex->id, 0, SETVAL, ick) == 0) {
# ifdef ALWAYS_CREATE_MUTEX_FILE
				FILE *fp;

				/* Save the semaphore id for leakage clean-up. */
				if ((fp = fopen(lockfile, "w")) != (FILE *) 0) {
					(void) fprintf(fp, "%d\n", mex->id);
					(void) fclose(fp);
				}
# endif
				return (void *) mex;
			}
			/* ick is ignored here by IPC_RMID so says the man */
			(void) semctl(mex->id, 0, IPC_RMID, ick);
		}
		free(mex);
	}
}
#endif
#if SERIALIZATION_API == WIN32_API
	if ((mex = malloc(sizeof *mex)) != (struct mutex *) 0) {
    		mex->handle = CreateMutex(NULL, FALSE, lockfile);

		if (mex->handle != (HANDLE) 0)
			return (void *) mex;

		free(mex);
	}
#endif
	return (void *) 0;
}

#ifdef NOT
/* UGH - this technique works for temporary files and shared memory,
 * but NOT semaphores.
 */

/* This is the Unix trick to pre-release a mutex while its still
 * in use by a process. The mutex can still be used until the
 * process terminates and inherited by the child processes (I
 * think). This technique is not necessarily portable across all
 * platforms or mutex APIs.
 */
void
MutexPreRelease(void *mp)
{
	if (mp == (void *) 0)
		return;

#if SERIALIZATION_API == SYSTEMV_API
{
	union semun ick = { 0 };
	(void) semctl(((struct mutex *) mp)->id, 0, IPC_RMID, ick);
}
#endif
}
#endif

void
MutexDestroy(void *mp)
{
	if (mp == (void *) 0)
		return;

#if SERIALIZATION_API == FCNTL_API
	(void) close(((struct mutex *) mp)->fd);
	(void) unlink(((struct mutex *) mp)->lockfile);
	free(mp);
#endif
#if SERIALIZATION_API == FLOCK_API
	(void) close(((struct mutex *) mp)->fd);
	(void) unlink(((struct mutex *) mp)->lockfile);
	free(mp);
#endif
#if SERIALIZATION_API == POSIX_API
	errno = 0;
	do {
		(void) sem_destroy(&((struct mutex *) mp)->sem);
	} while (errno == EBUSY);
	(void) unlink(((struct mutex *) mp)->lockfile);
	free(mp);
#endif
#if SERIALIZATION_API == SYSTEMV_API
{
	union semun ick = { 0 };
	(void) semctl(((struct mutex *) mp)->id, 0, IPC_RMID, ick);
	(void) unlink(((struct mutex *) mp)->lockfile);
	free(mp);
}
#endif
#if SERIALIZATION_API == WIN32_API
	CloseHandle(((struct mutex *) mp)->handle);
	free(mp);
#endif
}


int
MutexPermission(void *mp, int mode, int user, int group)
{
	if (mp == (void *) 0)
		return -1;

#if SERIALIZATION_API == FCNTL_API
	if (chmod(((struct mutex *) mp)->lockfile, mode) < 0)
		return -1;
	if (chown(((struct mutex *) mp)->lockfile, user, group) < 0)
		return -1;
#endif
#if SERIALIZATION_API == FLOCK_API
	if (chmod(((struct mutex *) mp)->lockfile, mode) < 0)
		return -1;
	if (chown(((struct mutex *) mp)->lockfile, user, group) < 0)
		return -1;
#endif
#if SERIALIZATION_API == POSIX_API
	if (chmod(((struct mutex *) mp)->lockfile, mode) < 0)
		return -1;
	if (chown(((struct mutex *) mp)->lockfile, user, group) < 0)
		return -1;
#endif
#if SERIALIZATION_API == SYSTEMV_API
{
	union semun ick;
        struct semid_ds sembuf;

	ick.buf = &sembuf;
        if (semctl(((struct mutex *) mp)->id, 0, IPC_STAT, ick) != 0)
                return -1;
        sembuf.sem_perm.uid = user;
        sembuf.sem_perm.gid = group;
        sembuf.sem_perm.mode = mode;
        if (semctl(((struct mutex *) mp)->id, 0, IPC_SET, ick) != 0)
                return -1;

	if (chmod(((struct mutex *) mp)->lockfile, mode) < 0)
		return -1;
	if (chown(((struct mutex *) mp)->lockfile, user, group) < 0)
		return -1;
}
#endif
#if SERIALIZATION_API == WIN32_API
	/* Do nothing. */
#endif

	return 0;
}


int
MutexLock(void *mp)
{
	if (mp == (void *) 0)
		return -1;

#if SERIALIZATION_API == FCNTL_API
{
	struct mutex * volatile mex = mp;

	for (errno = 0; fcntl(mex->fd, F_SETLKW, &mex->lock) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
}
#endif
#if SERIALIZATION_API == FLOCK_API
{
	struct mutex * volatile mex = mp;

	if (mex->pid != getpid()) {
		if ((mex->fd = open(mex->lockfile, O_RDWR, 0600)) < 0)
			return -1;
		mex->pid = getpid();
	}

	for (errno = 0; flock(mex->fd, LOCK_EX) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
}
#endif
#if SERIALIZATION_API == POSIX_API
	for (errno = 0; sem_wait(&((struct mutex * volatile) mp)->sem) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
#endif
#if SERIALIZATION_API == SYSTEMV_API
{
	struct mutex * volatile mex = mp;

	for (errno = 0; semop(mex->id, &mex->lock, 1) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
}
#endif
#if SERIALIZATION_API == WIN32_API
	switch (WaitForSingleObject(((volatile struct mutex * volatile) mp)->handle, INFINITE)) {
	case WAIT_TIMEOUT:
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		return 0;
	case WAIT_FAILED:
		return -1;
	}
#endif

	return 0;
}


int
MutexUnlock(void *mp)
{
	if (mp == (void *) 0)
		return -1;

#if SERIALIZATION_API == POSIX_API
	for (errno = 0; sem_post(&((struct mutex * volatile) mp)->sem) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
#endif
#if SERIALIZATION_API == FCNTL_API
{
	struct mutex * volatile mex = mp;

	for (errno = 0; fcntl(mex->fd, F_SETLKW, &mex->unlock) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
}
#endif
#if SERIALIZATION_API == FLOCK_API
{
	struct mutex * volatile mex = mp;

	if (mex->pid != getpid()) {
		if ((mex->fd = open(mex->lockfile, O_WRONLY, 0600)) < 0)
			return -1;
		mex->pid = getpid();
	}

	for (errno = 0; flock(mex->fd, LOCK_UN) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
}
#endif
#if SERIALIZATION_API == SYSTEMV_API
{
	struct mutex * volatile mex = mp;

	for (errno = 0; semop(mex->id, &mex->unlock, 1) != 0; ) {
		if (errno != EINTR)
			return -1;
	}
}
#endif
#if SERIALIZATION_API == WIN32_API
	if (ReleaseMutex(((struct mutex * volatile) mp)->handle) == 0)
		return -1;
#endif

	return 0;
}

/***********************************************************************
 *** pthread_mutex_* routine cover functions.
 ***********************************************************************/

#if ! defined(__WIN32__) && ! defined(HAVE_PTHREAD_MUTEX_INIT)

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	char tmp[L_tmpnam+1];

	/* Yes I know its not a good idea to use tmpnam(), but this
	 * is only intended as a work around for systems that don't
	 * have a pthread_mutex API.
	 */
	if ((*(volatile pthread_mutex_t *) mutex = MutexCreate(tmpnam(tmp))) == NULL)
		return -1;

	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
	if (*(volatile pthread_mutex_t *) mutex == PTHREAD_MUTEX_INITIALIZER) {
		if (pthread_mutex_init(mutex, NULL))
			return -1;
	}

	return MutexLock(*mutex);
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	return MutexUnlock(*mutex);
}

int
pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	MutexDestroy(*mutex);
	return 0;
}

#endif /* defined(__WIN32__) */

/***********************************************************************
 *** END
 ***********************************************************************/

