/*
 * JavaTime.h
 *
 * Copyright 2001, 2004 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_util_JavaTime_h__
#define __com_snert_lib_util_JavaTime_h__	1

typedef struct JavaTime {
	unsigned long seconds;
	unsigned milliseconds;
} *JavaTime;

typedef unsigned char *JavaLong;

#ifdef __cplusplus
extern "C" {
#endif

extern JavaTime JavaTimeCreate(unsigned long seconds);
extern void JavaTimeSet(JavaTime jt, unsigned long seconds);
extern JavaLong JavaTimeToJavaLong(JavaTime jt);
extern JavaTime JavaTimeFromJavaLong(JavaLong bytes8);
extern int JavaTimeFillJavaLong(JavaTime jt, JavaLong bytes8);
extern int JavaTimeCompare(void *, void *);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_util_JavaTime_h__ */
