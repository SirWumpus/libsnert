/*
 * DebugMallocTest.c
 *
 * Based on Electric Fence's eftest.c
 */

#ifndef SIGNAL_MEMORY
#define SIGNAL_MEMORY		SIGSEGV
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <com/snert/lib/util/DebugMalloc.h>

typedef enum { PASS, FAIL, SIGNAL } TestRc;

struct diagnostic {
	TestRc (*test)(void);
	TestRc expectedStatus;
	const char *explanation;
};

static char *allocation;
static volatile int fault_found = 0;
static const char newline[] = "\r\n";
static const char elipses[] = " ... ";

static void
fault_handler(int signum)
{
	fault_found = 1;
}

static TestRc
find_fault(const struct diagnostic *diag)
{
	TestRc status;
	const char *result;

	fault_found = 0;

	write(STDERR_FILENO, diag->explanation, strlen(diag->explanation));
	write(STDERR_FILENO, elipses, sizeof (elipses)-1);

	status = (*diag->test)();

	if (fault_found)
		status = SIGNAL;

	result = status == diag->expectedStatus ? "OK" : "FAIL";
	write(STDERR_FILENO, result, strlen(result));
	write(STDERR_FILENO, &newline, sizeof (newline)-1);

	return status;
}

static TestRc
testSizes(void)
{
	return sizeof (unsigned long) < sizeof (void *);
}

static TestRc
size_intptr(void)
{
	return sizeof (intptr_t) < sizeof (void *);
}

static TestRc
allocateMemory(void)
{
	allocation = (char *) malloc(1);

	return allocation == NULL;

}

static TestRc
freeMemory(void)
{
	free(allocation);

	return PASS;
}

static TestRc
write0(void)
{
	*allocation = 1;

	return PASS;
}

static TestRc
write_over(void)
{
	allocation[1] = '>';

	return PASS;
}

static TestRc
write_under(void)
{
	allocation[-1] = '<';

	return PASS;
}

static TestRc
corruptPointer(void)
{
	allocation += sizeof (void *);

	return PASS;
}

static struct diagnostic diagnostics[] = {
	{
		testSizes, PASS,
		"sizeof (int) == sizeof (void *)"
	},
	{
		size_intptr, PASS,
		"sizeof (intptr_t) == sizeof (void *)"
	},
	{
		allocateMemory, PASS,
		"#1 Allocation single byte of memory to play with."
	},
	{
		write0, PASS,
		"#1 Write valid memory"
	},
	{
		freeMemory, PASS,
		"#1 Free memory"
	},
	{
		allocateMemory, PASS,
		"#2 Allocation a new single byte of memory to play with."
	},
	{
		write0, PASS,
		"#2 Write valid memory"
	},
	{
		write_over, PASS,
		"#2 Over write invalid memory."
	},
	{
		freeMemory, SIGNAL,
		"#2 Free over written memory."
	},
	{
		allocateMemory, PASS,
		"#3 Allocation a new single byte of memory to play with."
	},
	{
		write_under, PASS,
		"#3 Under write invalid memory."
	},
	{
		freeMemory, SIGNAL,
		"#3 Free under written memory."
	},
	{
		allocateMemory, PASS,
		"#4 Allocation a new single byte of memory to play with."
	},
	{
		freeMemory, PASS,
		"#4 Free memory."
	},
	{
		freeMemory, SIGNAL,
		"#4 Double free memory."
	},
	{
		allocateMemory, PASS,
		"#5 Allocation a new single byte of memory to play with."
	},
	{
		corruptPointer, PASS,
		"#5 Corrupt the allocated memory pointer."
	},
	{
		freeMemory, SIGNAL,
		"#5 Free corrupted memory pointer."
	},
	{
		NULL, PASS, NULL
	}
};

static const char failedTest[] = "Unexpected result returned for:\n";

int
main(int argc, char * * argv)
{
	static const struct diagnostic *diag;

	memory_exit = 0;
	signal(memory_signal, fault_handler);

	for (diag = diagnostics; diag->explanation != NULL; diag++) {
		TestRc status = find_fault(diag);

		if (status != diag->expectedStatus) {
			/* Don't use stdio to print here, because stdio
			 * uses malloc() and we've just proven that malloc()
			 * is broken. Also, use _exit() instead of exit(),
			 * because _exit() doesn't flush stdio.
			 */
			write(STDERR_FILENO, failedTest, sizeof(failedTest) - 1);
			write(STDERR_FILENO, diag->explanation, strlen(diag->explanation));
			write(STDERR_FILENO, &newline, 1);
			_exit(EXIT_FAILURE);
		}
	}

	return EXIT_SUCCESS;
}
