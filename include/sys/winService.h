/*
 * winService.h
 *
 * Window Service Support API
 *
 * Copyright 2008, 2009 by Anthony Howe. All rights reserved.
 */

#ifndef __com_snert_lib_sys_winService_h__
#define __com_snert_lib_sys_winService_h__	1

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ***
 ***********************************************************************/

#include <com/snert/lib/net/server.h>

/**
 * Defined by the application and is called by Windows via ServiceMain()
 * handler setup by winServiceStart(). Can be called by main() when
 * starting in application console (non-daemon) mode.
 */
extern int serverMain(void);

/**
 * Defined by the application and is called by Windows via ServiceMain()
 * handler setup by winServiceStart(). Can be called by main() when
 * starting in application console (non-daemon) mode.
 */
extern void serverOptions(int argc, char **argv);

extern int winServiceIsInstalled(const char *name);
extern int winServiceInstall(int install, const char *name, const char *brief);
extern int winServiceStart(const char *name, int argc, char **argv);
extern void winServiceSetSignals(ServerSignals *signals);

/***********************************************************************
 ***
 ***********************************************************************/

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_sys_winService_h__ */

