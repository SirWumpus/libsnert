/*
 * free_clear.c
 *
 * Copyright 2015 by Anthony Howe.  All rights reserved.
 */
 
#include <stdlib.h>
 
/*
 * @param ptr
 *	ptr is declared as a "void *" for compatibility with
 *	pthread_cleanup_push(), but is treated as a "void **" so
 *	that the pointer variable can be set to NULL once freed.
 */
void
free_clear(void *mem)
{
	free(*(void **)mem);
	*(void **)mem = NULL;
}
