/*
 * flock.c
 *
 * Copyright 2004, 2006 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#if defined(ENABLE_ALT_FLOCK) || !defined(HAVE_FLOCK)

#include <errno.h>
#include <com/snert/lib/io/file.h>
#undef flock

int
alt_flock(int fd, int lock)
{
# if defined(HAVE_FCNTL)
#  include <fcntl.h>
	struct flock lock_state;

	switch (lock & ~LOCK_NB) {
	case LOCK_SH: lock_state.l_type = F_RDLCK; break;
	case LOCK_EX: lock_state.l_type = F_WRLCK; break;
	case LOCK_UN: lock_state.l_type = F_UNLCK; break;
	}

	lock_state.l_whence = SEEK_SET;
	lock_state.l_start = 0;
	lock_state.l_len = 0;		/* until end of file */
	lock_state.l_pid = 0;

	for (errno = 0; fcntl(fd, (lock & LOCK_NB) ? F_SETLK : F_SETLKW, &lock_state) != 0; ) {
		if (errno != EINTR)
			return -1;
	}

	return 0;
# elif defined(HAVE_LOCKING)
#  include <sys/locking.h>
	switch (lock) {
	case LOCK_SH: 		lock = LK_RLCK;  break;
	case LOCK_SH|LOCK_NB:	lock = LK_NBRLCK;  break;
	case LOCK_EX: 		lock = LK_LOCK; break;
	case LOCK_EX|LOCK_NB: 	lock = LK_NBLCK; break;
	case LOCK_UN: 		lock = LK_UNLCK; break;
	}

	return locking(fd, lock, 0);
# elif defined(HAVE_LOCKF)
	switch (lock) {
	case LOCK_SH: 		return 0;
	case LOCK_SH|LOCK_NB:	return 0;
	case LOCK_EX: 		lock = F_LOCK; break;
	case LOCK_EX|LOCK_NB: 	lock = F_TLOCK; break;
	case LOCK_UN: 		lock = F_ULOCK; break;
	}

	return lockf(fd, lock, 0);
# elif defined(__MINGW32__)
#  include <sys/locking.h>

	switch (lock) {
	case LOCK_SH: 		lock = _LK_RLCK;  break;
	case LOCK_SH|LOCK_NB:	lock = _LK_NBRLCK;  break;
	case LOCK_EX: 		lock = _LK_LOCK; break;
	case LOCK_EX|LOCK_NB: 	lock = _LK_NBLCK; break;
	case LOCK_UN: 		lock = _LK_UNLCK; break;
	}

	return _locking(fd, lock, 0);
# else
#  error "No supported file locking method.";
# endif
}

#endif /* !defined(HAVE_FLOCK) */
