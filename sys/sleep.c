/*
 * sleep.c
 *
 * POSIX sleep() cover function
 *
 * Copyright 2005 by Anthony Howe.  All rights reserved.
 */

#ifdef __WIN32__
#include <windows.h>

unsigned int
sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}
#endif
