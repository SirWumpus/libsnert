/*
 * Trival err.h
 */

#ifndef __err_h__
#define __err_h__

#ifdef __cplusplus
extern "C" {
#endif

#include <errno.h>
#include <string.h>
#include <stdarg.h>

#define warn(...)	warnc(errno, __VA_ARGS__)
#define warnx(...)	{ fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); }
#define warnc(code, ...)	{ \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, ": %s\n", strerror(code)); \
}

#define vwarn(fmt, args)  vwarnc(errno, fmt, args)
#define vwarnx(fmt, args) { vfprintf(stderr, fmt, args); fputc('\n', stderr); }
#define vwarnc(code, fmt, args)  { \
	vfprintf(stderr, fmt, args); \
	fprintf(stderr, "%s%s\n", (fmt) ? ": " : "", strerror(code)); \
}

#define err(ex, ...)  errc(ex, errno, __VA_ARGS__)
#define errx(ex, ...) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(ex); }
#define errc(ex, code, ...)  { \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, ": %s\n", strerror(code)); \
	exit(ex); \
}

#define verr(ex, fmt, args)  verrc(ex, errno, fmt args)
#define verrx(ex, fmt, args) { vfprintf(stderr, fmt, args); fputc('\n', stderr); exit(ex); }
#define verrc(ex, code, fmt, args) { \
	vfprintf(stderr, fmt, args); \
	fprintf(stderr, "%s%s\n", (fmt) ? ": " : "", strerror(code)); \
	exit(ex); \
}

#ifdef  __cplusplus
}
#endif

#endif /* __err_h__ */