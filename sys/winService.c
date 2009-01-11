/*
 * winService.c
 *
 * Window Service Support API
 *
 * Copyright 2008, 2009 by Anthony Howe. All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#ifdef __WIN32__

#include <com/snert/lib/version.h>

#include <stdlib.h>
#include <string.h>

#include <com/snert/lib/net/server.h>
#include <com/snert/lib/sys/winService.h>
#include <com/snert/lib/util/Text.h>

#include <windows.h>

/***********************************************************************
 ***
 ***********************************************************************/

int
winServiceIsInstalled(const char *name)
{
	SC_HANDLE manager;
	SC_HANDLE service;

	manager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
	if (manager == NULL)
		return 0;

	if ((service = OpenService(manager, name, SERVICE_ALL_ACCESS)) == NULL)
		return 0;

	CloseServiceHandle(service);

	return 1;
}

int
winServiceInstall(int install, const char *name, const char *brief)
{
	int rc;
	long length;
	SC_HANDLE manager;
	SC_HANDLE service;
	char service_path[256];

	if (name == NULL)
		return -1;

	manager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
	if (manager == NULL)
		return -1;

	if (!install) {
		if ((service = OpenService(manager, name, SERVICE_ALL_ACCESS)) != NULL) {
			rc = -!DeleteService(service);
			CloseServiceHandle(service);
			return rc;
		}

		return -1;
	}

	/* Get the absolute path of this executable and set the working
	 * directory to correspond to it so that we can find the options
	 * configuration file along side the executable, when running as
	 * a service. (I hate using the registry.)
	 */
	if ((length = GetModuleFileName(NULL, service_path, sizeof (service_path))) == 0 || length == sizeof (service_path))
		return -1;

	service = CreateService(
		manager,			// SCManager database
		name,				// name of service
		name,				// name to display
		SERVICE_ALL_ACCESS,		// desired access
		SERVICE_WIN32_OWN_PROCESS,	// service type
		SERVICE_AUTO_START,		// start type
		SERVICE_ERROR_NORMAL,		// error control type
		service_path,			// service's binary
		NULL,				// no load ordering group
		NULL,				// no tag identifier
		"Tcpip\0\0",			// dependencies
		NULL,				// LocalSystem account
		NULL				// no password
	);

	if (service == NULL)
		return -1;

	if (brief != NULL) {
		SERVICE_DESCRIPTION description = { (char *) brief };
		(void) ChangeServiceConfig2(service, SERVICE_CONFIG_DESCRIPTION, &description);
	}

	CloseServiceHandle(service);

	return 0;
}

static ServerSignals *serviceSignals;
static SERVICE_STATUS_HANDLE serviceStatus;

/*
 * Called from a different thread.
 *
 * @return
 *	If the function handles the control signal, it should return TRUE.
 *	If it returns FALSE, the next handler function in the list of handlers
 *	for this process is used.
 */
static BOOL WINAPI
HandlerRoutine(DWORD ctrl)
{
	switch (ctrl) {
	case CTRL_SHUTDOWN_EVENT:
		if (serviceSignals != NULL)
			SetEvent(serviceSignals->signal_thread_event);
		break;

	case CTRL_LOGOFF_EVENT:
		return TRUE;

	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
		if (serviceSignals != NULL) {
			SetEvent(serviceSignals->signal_thread_event);
			return TRUE;
		}
	}

	return FALSE;
}

/*
 * Called from within Service Control Manager distpatch thread.
 */
static DWORD WINAPI
serviceControl(DWORD code, DWORD eventType, LPVOID eventData, LPVOID userData)
{
	SERVICE_STATUS status;

	switch (code) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		/* Don't know where application is spinning, but if its
		 * important they should have registered one or more
		 * shutdown hooks. Begin normal exit sequence. We will
		 * end up in our ExitHandler() when the application has
		 * finished.
		 */
		if (serviceSignals != NULL)
			SetEvent(serviceSignals->signal_thread_event);

		status.dwCheckPoint = 0;
		status.dwWaitHint = 2000;
		status.dwWin32ExitCode = NO_ERROR;
		status.dwServiceSpecificExitCode = 0;
		status.dwCurrentState = SERVICE_STOPPED;
		status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
		status.dwControlsAccepted = SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN;
		SetServiceStatus(serviceStatus, &status);
		break;
	default:
		return ERROR_CALL_NOT_IMPLEMENTED;
	}

	return NO_ERROR;
}

static VOID WINAPI
ServiceMain(DWORD argc, char **argv)
{
	SERVICE_STATUS status;

	/* Parse options passed from the Windows Service properties dialog.
	 * argv[0] is the service key name.
	 */
	serverOptions(argc, argv);

	serviceStatus = RegisterServiceCtrlHandlerEx(argv[0], serviceControl, NULL);
	if (serviceStatus == 0)
		return;

	(void) SetConsoleCtrlHandler(HandlerRoutine, TRUE);

	status.dwCheckPoint = 0;
	status.dwWaitHint = 2000;
	status.dwWin32ExitCode = NO_ERROR;
	status.dwServiceSpecificExitCode = 0;
	status.dwCurrentState = SERVICE_RUNNING;
	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwControlsAccepted = SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN;
	SetServiceStatus(serviceStatus, &status);

	openlog(argv[0], LOG_PID, LOG_MAIL);
	setlogmask(LOG_UPTO(LOG_DEBUG));

	(void) serverMain();
}

void
winServiceSetSignals(ServerSignals *signals)
{
	serviceSignals = signals;
}

int
winServiceStart(const char *name, int argc, char **argv)
{
	SC_HANDLE manager;
	SC_HANDLE service;
	SERVICE_TABLE_ENTRY dispatchTable[2];

	dispatchTable[0].lpServiceName = (char *) name;
	dispatchTable[0].lpServiceProc = ServiceMain;
	dispatchTable[1].lpServiceName = NULL;
	dispatchTable[1].lpServiceProc = NULL;

	if (!StartServiceCtrlDispatcher(dispatchTable)) {
		if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
			return -1;

		manager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
		if (manager == NULL)
			return -1;

		service = OpenService(manager, name, SERVICE_ALL_ACCESS);
		if (service == NULL)
			return -1;

		if (0 < argv && argv != NULL)
			argv[0] = (char *) name;

		if (!StartService(service, argc, (LPCTSTR *) argv))
			return -1;

		CloseServiceHandle(service);
	}

	return 0;
}

#endif /* __WIN32__ */
