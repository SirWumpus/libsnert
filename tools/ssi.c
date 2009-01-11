/*
 * ssi.c
 *
 * Copyright 2004 by Anthony Howe.  All rights reserved.
 */

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>

#include <com/snert/lib/version.h>
#include <com/snert/lib/io/posix.h>

/***********************************************************************
 *** Global Variables
 ***********************************************************************/

static long debug;
static int isCGI;
static int isNPH;
static int enableExec;
static char buf[512];
static char command[101];
static char attribute[101];
static char errmsg[101] = "an error occurred while processing this directive";
static char sizefmt[101] = "bytes";
static char timefmt[101] = "%c";
static char *thisFile;

typedef char *(*CommandFunction)(FILE *);

struct command {
	char *command;
	CommandFunction function;
};

char *usage = 
"\033[1musage: ssi [-cen] file\033[0m\n"
"\n"
"-c\t\tis a CGI, write Content-Type header\n"
"-e\t\tenable the exec directive\n"
"-n\t\tis a non-parsed header CGI, implies -c\n"
"\n"
"\033[1mssi/1.0 Copyright 2004 by Anthony Howe. All rights reserved.\033[0m\n"
;

/***********************************************************************
 *** Filter
 ***********************************************************************/

/*
 * Return 0 on success, otherwise -1 for an error.
 */
int
filterChildOutput(char *buffer, long *length, int *state)
{
	int n;
	long len = *length;
	
	for (i = 0; i < length; i++) {
		switch (*state) {
		case 0:
			/* This assumes that the physical buffer length
			 * is at least greater than 5 bytes.
			 */
			sscanf(buffer, len, "HTTP/%*[^\r\n]%n", &n);
			if (0 <= n && n < 5) {
				*state = 100;
				return 0;
			}
			
			/* Found the start of the HTTP response. */
			*state = 1;
			continue;
		case 1:
			/* Look for end of header line. */
			i += strcspn(buffer+i, "\r\n");

			if (buffer[i] == '\r') {
				*state = 2;
			} else if (buffer[i] == '\n') {
				*state = 3;
			}
			continue;
		case 2:
			if (buffer[i] == '\n') {
				*state = 3;
			} else {
				*state = 1;
			}
			continue;
		case 3:
			/* Look for blank line separating headers and content. */
			if (buffer[i] == '\r') {
				*state = 4;
			} else if (buffer[i] == '\n') {
				*state = 5;
			} else (
				*state = 1;
			}
			continue;
		case 4:
			if (buffer[i] == '\n') {
				*state = 5;
			} else {
				*state = 1;
			}
			continue;
		case 5:			
			/* Strip remaining response headers. */
			memmove(buffer, buffer+i, len-i);
			*length = len-i;
			*state = 100;
			return 1;
		case 100:
			return 0;
		}		
	}		

	/* Strip HTTP response and headers. */
	*length = 0;
#endif
	
	return 0;
}

char *
strjoinv(char *a, va_list args)
{
	int i;
	size_t length;
	char *out, *s, *t;
	
	for (length = 1, i = 0; args[i] != NULL; i++) {
		length += strlen((char *) args[i]);
	}
	
	if ((out = malloc(length)) == NULL)
		return NULL;
		
	for (t = out, i = 0; args[i] != NULL; i++) {
		for (s = (char *) args[i]; *s != '\0'; s++, t++)
			*t = *s;
	}
	*t = '\0';
	
	return out;
}

char *
strjoin(char *a, ...)
{
	char *out;
	va_list args;
	
	va_start(args, fmt);
	out = strjoinv(a, args);
	va_end(args);

	return out;	
}

/**
 * @param tp
 *	A pointer to pointer of char. Decoded bytes from the source
 *	string are copied to this destination. The destination buffer
 *	must be as large as the source. The copied string is '\0'
 *	terminated and the pointer passed back points to next byte
 *	after the terminating '\0'.
 *
 * @param sp
 * 	A pointer to pointer of char. The URL encoded bytes are copied
 *	from this source buffer to the destination. The copying stops
 *	after an equals-sign, ampersand, or on a terminating '\0' and
 *	this pointer is passed back.
 */
void
cgiUrlDecode(char **tp, char **sp)
{
	int hex;
	char *t, *s;

	for (t = *tp, s = *sp; *s != '\0'; t++, s++) {
		switch (*s) {
		case '=':
		case '&':
			s++;
			break;
		case '+':
			*t = ' ';
			continue;
		case '%':
			if (sscanf(s+1, "%2x", &hex) == 1) {
				*t = (char) hex;
				s += 2;
				continue;
			}
			/*@fallthrough@*/
		default:
			*t = *s;
			continue;
		}
		break;
	}
	
	/* Terminate decoded string. */
	*t = '\0';
	
	/* Pass back the next unprocessed location.
	 * For the source '\0' byte, we stop on that.
	 */
	*tp = t+1;
	*sp = s;
}

/**
 * @param urlencoded
 *	A URL encoded string such as the query string portion of an HTTP
 *	request or HTML form data ie. application/x-www-form-urlencoded.
 *
 * @return
 *	A pointer to array 2 of pointer to char. The first column of the
 *	table are the field names and the second column their associated
 *	values. The array is NULL terminated. The array pointer returned
 *	must be released with a single call to free().
 */
char *(*cgiParseForm(char *urlencoded))[2]
{
	int nfields, i;
	char *s, *t, *(*out)[2];
	
	if (urlencoded == NULL)
		return NULL;
	
	nfields = 1;
	for (s = urlencoded; *s != '\0'; s++) {
		if (*s == '&')
			nfields++;
	}
		
	if ((out = malloc((nfields + 1) * sizeof (*out) + strlen(urlencoded) + 1)) == NULL)
		return NULL;
		
	s = urlencoded;
	t = (char *) &out[nfields+1];

	for (i = 0; i < nfields; i++) {
		out[i][0] = t;
		cgiUrlDecode(&t, &s);

		out[i][1] = t;
		cgiUrlDecode(&t, &s);
	}

	out[i][0] = NULL;	
	out[i][1] = NULL;

	return out;	
}

#ifdef __WIN32__
/***********************************************************************
 *** Windows
 ***********************************************************************/

#include <windows.h>

#if defined(__BORLANDC__)
# include <io.h>
# include <dir.h>
# include <sys/locking.h>
extern long getpid(void);
#endif

int
ThreadCreate(void *(*fn)(void *), void *data)
{
	DWORD id;
	
	return -(CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) fn, data, 0, &id) == 0);
}

# define SHUT_RD	SD_RECEIVE
# define SHUT_WR	SD_SEND
# define SHUT_RDWR	SD_BOTH

SOCKET server;

typedef struct {
	WORKSPACE;
	HANDLE proxyInput;
} Connection;	

char *
GetErrorMessage(DWORD code)
{
	int length;
	char *error;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR) &error, 0, NULL
	);

	for (length = strlen(error)-1; 0 <= length && isspace(error[length]); length--)
		error[length] = '\0';

	return error;
}

/*
 * A thread to proxy client socket input to a child CGI as standard input.
 */
void *
ForwardInput(void *data)
{
	DWORD nbytes;
	unsigned char ch;
	Connection *conn = (Connection *) data;
	
	for ( ; 0 < recv(conn->client, &ch, 1, 0); conn->bytesIn++)
		WriteFile(conn->proxyInput, &ch, 1, &nbytes, NULL);

	return NULL;
}

/*
 * Start a child CGI process with redirected I/O from the client via
 * two asynchronous proxy threads (this one and a new one), until the
 * child CGI closes its output stream.
 *
 * In theory, this function should have been able to duplicate inheritable
 * copies of the client socket and pass them as standard input and output
 * to the child CGI through the STARTUPINFO structure, but this technique
 * never worked for some Windows specific reason I could not figure out.
 *
 * One advantage about proxy streams, is that they can later be used to 
 * implement bandwidth throttling and filters.
 */
int
cgi(Connection *conn, char *cgi, char **env)
{
	int fd, state;
	STARTUPINFO si;
	DWORD nbytes, status;
	SECURITY_ATTRIBUTES sa;
	PROCESS_INFORMATION pi;
	unsigned char buffer[BUFSIZ];
	char *ptr, *error, script[1024];
	HANDLE parentIn, parentOut, parentInPrivate, parentOutPrivate;
	
	/* Check for unix-style #! script invocation. */
	if (0 <= (fd = open(cgi, O_RDONLY))) {
		nbytes = read(fd, script, sizeof (script)/2);
		close(fd);
		
		if (0 < nbytes && script[0] == '#' && script[1] == '!') {
			int newline;
			script[nbytes] = '\0';
			newline = strcspn(script, "\r\n");
			if (script[newline] != '\0') {
				script[newline] = ' ';
				snprintf(script + newline + 1, sizeof (script) - newline - 1, cgi);
				cgi = script+2;
			}
		}
	}
		
	ZeroMemory(&si, sizeof (si));
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;

	sa.nLength= sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	/* Create parent -> child pipe. */
	if(!CreatePipe(&si.hStdInput, &parentOut, &sa, 0)) {
		status = GetLastError();
		error = GetErrorMessage(status);
		appLog("{%.5u} failed to create parent -> child pipe: %s (%ld)", conn->requestNumber, error, status);
		LocalFree(error);
		goto error0;
	}

	/* Convert the parent -> child write handle to a private one. */
	if (!DuplicateHandle(GetCurrentProcess(), parentOut, GetCurrentProcess(), &parentOutPrivate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		status = GetLastError();
		error = GetErrorMessage(status);
		appLog("{%.5u} failed to duplicate handle: %s (%ld)", conn->requestNumber, error, status);
		LocalFree(error);
		goto error1;
	}
	
	/* Close our inheritable handle before creating the child. */	
	CloseHandle(parentOut);
	
	/* Juggle handle for error exit handling. */
	parentOut = parentOutPrivate;
	
	/* Create child -> parent pipe. */
	if(!CreatePipe(&parentIn, &si.hStdOutput, &sa, 0)) {
		status = GetLastError();
		error = GetErrorMessage(status);
		appLog("{%.5u} failed to create child -> parent pipe: %s (%ld)", conn->requestNumber, error, status);
		LocalFree(error);
		goto error1;
	}

	/* Convert the child -> parent read handle to a private one. */
	if (!DuplicateHandle(GetCurrentProcess(), parentIn, GetCurrentProcess(), &parentInPrivate, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		status = GetLastError();
		error = GetErrorMessage(status);
		appLog("{%.5u} failed to duplicate handle: %s (%ld)", conn->requestNumber, error, status);
		LocalFree(error);
		goto error2;
	}

	/* Close our inheritable handle before creating the child. */	
	CloseHandle(parentIn);

	/* Juggle handle for error exit handling. */
	parentIn = parentInPrivate;

	/* Convert path separators from unix to windows. */
	for (ptr = cgi; *ptr != '\0'; ptr++)
		if (*ptr == '/')
			*ptr = '\\';

	appLog("{%.5u} create process \"%s\"", conn->requestNumber, cgi);
	if (CreateProcess(NULL, cgi, NULL, NULL, TRUE, 0, env[0], NULL, &si, &pi) == 0) {
		status = GetLastError();
		error = GetErrorMessage(status);
		appLog("{%.5u} failed to create process \"%s\": %s (%ld)", conn->requestNumber, cgi, error, status);
		LocalFree(error);
		goto error2;
	}

	/* Start a proxy thread for client-to-child input stream. */
	conn->proxyInput = parentOutPrivate;

	if (ThreadCreate(ForwardInput, conn))
		goto error2;

	/* Close our copies of the child's handles. If you don't do this now
	 * for some reason, then no data is passed. Windows is so stupid.
	 */
	CloseHandle(si.hStdOutput);		
	CloseHandle(si.hStdInput);	
	
	/* Proxy child-to-client output stream. */
	state = 0;
	while (ReadFile(parentInPrivate, buffer, sizeof (buffer), &nbytes, NULL)) {
		long bytesOut;

		if (filterChildOutput(buffer, &nbytes, &state))
			break;

		if ((bytesOut = send(conn->client, buffer, nbytes, 0)) < (long) nbytes)
			break;
		conn->bytesOut += bytesOut;
	}

	/* Either an error occured or the pipe was broken. */
	CloseHandle(parentInPrivate);
	CloseHandle(parentOutPrivate);

	appLog("{%.5u} waiting for end of \"%s\"", conn->requestNumber, cgi);

	WaitForSingleObject(pi.hProcess, INFINITE);		
	GetExitCodeProcess(pi.hProcess, &status);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	appLog("{%.5u} return code for \"%s\": %ld", conn->requestNumber, cgi, status);

	return 0;
error2:
	/* Clean-up child -> parent pipe. */
	CloseHandle(parentIn);
	CloseHandle(si.hStdOutput);
error1:
	/* Clean-up parent -> child pipe. */
	CloseHandle(si.hStdInput);
	CloseHandle(parentOut);
error0:	
	return -1;
}

typedef HANDLE pthread_mutex_t;
typedef long pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER	0

int 
pthread_mutex_init(pthread_mutex_t *handle, const pthread_mutexattr_t *attr)
{
	if ((*(volatile pthread_mutex_t *) handle = CreateMutex(NULL, FALSE, NULL)) == (HANDLE) 0)
		return -1;
		
	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *handle)
{
	if (*(volatile pthread_mutex_t *)handle == PTHREAD_MUTEX_INITIALIZER) {
		if (pthread_mutex_init(handle, NULL))
			return -1;
	}

	switch (WaitForSingleObject(*(volatile pthread_mutex_t *) handle, INFINITE)) {
	case WAIT_TIMEOUT:
	case WAIT_OBJECT_0:
	case WAIT_ABANDONED:
		return 0;
	case WAIT_FAILED:
		return -1;
	}

	return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *handle)
{
	if (ReleaseMutex(*(volatile pthread_mutex_t *) handle) == 0)
		return -1;
			
	return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *handle)
{
	if (*(volatile pthread_mutex_t *)handle != (HANDLE) 0)
		CloseHandle(*(volatile pthread_mutex_t *) handle);

	return 0;	
}

#else
/***********************************************************************
 *** POSIX
 ***********************************************************************/

#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#if defined(__CYGWIN__)
# include <io.h>
#endif

typedef int SOCKET;

#define INVALID_SOCKET	(-1)
#define closesocket	close

SOCKET server;

typedef struct {
	WORKSPACE;
	int proxyInput;
} Connection;	

int
ThreadCreate(void *(*fn)(void *), void *data)
{
	int rc;
	pthread_t thread;

	rc = pthread_create(&thread, (pthread_attr_t *) 0, fn, data);
#ifndef NDEBUG
	appLog("[%d] ThreadCreate(fn=%lx, data=%lx) rc=%d thread=%lx", getpid(), (long) fn, (long) data, rc, thread);
#endif	
	return rc;
}

/*
 * A thread to proxy client socket input to a child CGI as standard input.
 */
void *
ForwardInput(void *data)
{
	unsigned char ch;
	Connection *conn = (Connection *) data;
	
	for ( ; 0 < recv(conn->client, &ch, 1, 0); conn->bytesIn++)
		write(conn->proxyInput, &ch, 1);

	return NULL;
}

/*
 * This version mimicks the Windows version. One reason to do this is
 * just to show how simple it is to do using the POSIX API. The other
 * is that with proxy I/O streams, its then possible to implement
 * bandwidth throttling and filters. Also it simplifies debugging
 * when bother versions share similar design logic.
 */
int
cgi(Connection *conn, char *cgi, char **env)
{
	pid_t child;
	ssize_t nbytes;
	int status, state;
	unsigned char buffer[BUFSIZ];
	int inputPipe[2], outputPipe[2];
		
	if (pipe(inputPipe))
		goto error0;
		
	if (pipe(outputPipe))
		goto error1;

	/* Start a proxy thread for client-to-child input stream. */
	conn->proxyInput = inputPipe[1];

	if (ThreadCreate(ForwardInput, conn))
		goto error2;

	if ((child = fork()) == -1)
		goto error2;
	
	if (0 < child) {
		/* Close our copies of the child's standard input and output. */
		close(inputPipe[0]);
		close(outputPipe[1]);
		
		/* Proxy child-to-client output stream. */
		state = 0;
		while (0 < (nbytes = read(outputPipe[0], &buffer, sizeof (buffer)))) {
			ssize_t bytesOut;
			
			if (filterChildOutput(buffer, &nbytes, &state))
				break;
			
			if ((bytesOut = send(conn->client, buffer, nbytes, 0)) < nbytes)
				break;
			conn->bytesOut += bytesOut;
		}
		
		/* Either an error occured or the pipe was broken. */
		appLog("{%.5u} waiting for end of \"%s\"", conn->requestNumber, cgi);
		
		waitpid(child, &status, 0);		
		close(outputPipe[0]);
		close(inputPipe[1]);
		
		appLog("{%.5u} return code for \"%s\": %ld", conn->requestNumber, cgi, status);
		
		return status;
	}

	/* The child doesn't need a copy of the server port. */
	close(server);

	/* Redirect standard I/O for the child CGI. */
	dup2(inputPipe[0], 0);
	dup2(outputPipe[1], 1);

	/* We don't need to escape shell meta characters, since the 
	 * earlier stat() would have failed to find a file that did
	 * contain them. Of course anyone stupid enough to use shell
	 * meta characters for an executable filename should be shot.
	 */
	appLog("{%.5u} create process \"%s\"", conn->requestNumber, cgi);

	return execle(cgi, cgi, (void *) 0, env);
error2:
	close(outputPipe[0]);
	close(outputPipe[1]);
error1:
	close(inputPipe[0]);
	close(inputPipe[1]);
error0:
	return -1;
}
#endif 

/***********************************************************************
 *** Routines
 ***********************************************************************/

char *
cmdEcho(FILE *fp)
{
	time_t now;
	char *value;
	struct stat sb;
	
	if (fscanf(fp, "var = \"%511[^\"]\"", buf) != 1)
		return "echo syntax error";

	time(&now);

	if (strcmp(buf, "DOCUMENT_NAME") == 0) {
		value = thisFile;
	} else if (strcmp(buf, "DATE_GMT") == 0) {
		if (debug)
			fprintf(stderr, "%s\n", timefmt);
		(void) strftime(buf, sizeof (buf), timefmt, gmtime(&now));
		value = buf;		
	} else if (strcmp(buf, "DATE_LOCAL") == 0) {
		if (debug)
			fprintf(stderr, "%s\n", timefmt);
		(void) strftime(buf, sizeof (buf), timefmt, localtime(&now));
		value = buf;		
	} else if (strcmp(buf, "LAST_MODIFIED") == 0) {
		if (stat(thisFile, &sb)) {
			printf("[failed to get file time for \"%s\": %s (%d)]", thisFile, strerror(errno), errno);
			return NULL;
		}

		(void) strftime(buf, sizeof (buf), timefmt, localtime(&sb.st_mtime));
		value = buf;
	} else if ((value = getenv(buf)) == NULL) {
		value = "";
	}
	
	printf("%s", value);
	
	return NULL;
}

char *
cmdInclude(FILE *fp)
{
	size_t n;
	struct stat sb;
	char *copy, *str;
	
	if (fscanf(fp, "file = \"%511[^\"]\"", buf) == 1) {
		str = strdup(getenv("SCRIPT_NAME"));
		*strrchr(str, '/') = '\0';
		
		copy = strjoin(str, buf, NULL);
		free(str);
		
		if (stat(copy, &sb) != 0) {
			printf("[failed to find include file: %s]", buf);
			free(copy);
			return NULL;
		}						
	} else if (fscanf(fp, "virtual = \"%511[^\"]\"", buf) == 1) {
		if ((copy = strdup(buf)) == NULL)
			return "failed to allocate memory";
		
		while (*copy == '/') {
			errno = 0;
			if (stat(copy, &sb) == 0)
				break;
			*strrchr(copy, '/') = '\0';
		}
		
		if (*copy == '\0') {
			printf("[failed to include virtual path: %s]", buf);
			free(copy);
			return NULL;
		}
		
		n = strlen(copy);
		
		str = strjoin("PATH_INFO=", buf+n, NULL); 
		putenv(str);
		free(str);
		
		str = strjoin("PATH_TRANSLATED=", getenv("DOCUMENT_ROOT"), buf+n, NULL); 
		putenv(str);
		free(str);
	} else {
		return "include syntax error";
	}
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	return NULL;
}

char *
cmdFsize(FILE *fp)
{
	struct stat sb;
	
	if (fscanf(fp, "file = \"%511[^\"]\"", buf) != 1)
		return "fsize syntax error";
	
	if (stat(buf, &sb)) {
		printf("[failed to get file size for \"%s\": %s (%d)]", buf, strerror(errno), errno);
		return NULL;
	}
		
	printf("%lu", (unsigned long) sb.st_size);
	
	return NULL;
}

char *
cmdFlastmod(FILE *fp)
{
	struct stat sb;
	
	if (fscanf(fp, "file = \"%511[^\"]\"", buf) != 1)
		return "fsize syntax error";
	
	if (stat(buf, &sb)) {
		printf("[failed to get file time for \"%s\": %s (%d)]", buf, strerror(errno), errno);
		return NULL;
	}
		
	(void) strftime(buf, sizeof (buf), timefmt, localtime(&sb.st_mtime));
	printf("%s", buf);
	
	return NULL;
}

char *
cmdExec(FILE *fp)
{
	if (!enableExec)
		return "exec directive disabled";
		
	return NULL;
}

char *
cmdConfig(FILE *fp)
{
	int n;
	
	if (fscanf(fp, "%100[^= ] = ", attribute) != 1)
		return "config syntax error";

	if (strcmp(attribute, "errmsg") == 0)
		n = fscanf(fp, "\"%100[^\"]\"", errmsg);
	else if (strcmp(attribute, "sizefmt") == 0)
		n = fscanf(fp, "\"%100[^\"]\"", sizefmt);
	else if (strcmp(attribute, "timefmt") == 0)
		n = fscanf(fp, "\"%100[^\"]\"", timefmt);

	if (n != 1)
		return "invalid config attribute";
			
	return NULL;
}

static struct command cmdTable[] = {
	{ "echo", cmdEcho },
	{ "include", cmdInclude },
	{ "fsize", cmdFsize },
	{ "flastmod", cmdFlastmod },
	{ "exec", cmdExec },
	{ "config", cmdConfig },
	{ 0, 0 }
};

int
main(int argc, char **argv)
{
	char *pt;
	FILE *fp;
	long offset;
	struct command *s;
	int argi, ch, state;
	
	if ((pt = strrchr(argv[0], '/')) == NULL
	&&  (pt = strrchr(argv[0], '\\')) == NULL)
		pt = argv[0];

	isNPH = strncmp(pt+1, "nph-", 4) == 0;
	isCGI = strstr(pt+1, ".cgi") != NULL;

	for (argi = 1; argi < argc; argi++) {
		if (argv[argi][0] != '-')
			break;
			
		switch (argv[argi][1]) {
		case 'c':
			isCGI = 1;
			break;
		case 'e':
			enableExec = 1;
			break;
		case 'n':
			isNPH = 1;
			break;
		case 'v':
			debug = strtol(argv[argi][2] == '\0' ? argv[++argi] : &argv[argi][2], NULL, 10);
			break;
		default:
			fprintf(stderr, "invalid option -%c\n%s", argv[argi][1], usage);
			return 2;
		}
	}

	if (argi + 1 != argc) {
		fprintf(stderr, "%s", usage);
		return 2;
	}
		
	if ((fp = fopen(thisFile = argv[argi], "r")) == NULL) {
		fprintf(stderr, "ssi %s: %s (%d)\n", argv[1], strerror(errno), errno);
		return 1;
	}
	
	if (isNPH) {
		pt = getenv("SERVER_PROTOCOL");
		printf("HTTP/%s 200 OK\r\n", pt == NULL ? "1.0" : pt);
		isCGI = 1;
	}
	
	if (isCGI)
		printf("Content-Type: text/plain; charset=US-ASCII\r\n\r\n");

	for (;;) {
		offset = ftell(fp);
		if (fscanf(fp, "<!--#%100s ", command) == 1) {
			for (s = cmdTable; s->command != NULL; s++) {
				if (strcmp(s->command, command) == 0) {
					pt = (*s->function)(fp);
					if (pt != NULL)
						printf("[%s]", pt);
					break;
				}
			}
			
			if (s->command == NULL)
				printf("[invalid directive: %s]", command);
			
			for (state = 0; (ch = fgetc(fp)) != EOF; ) {
				switch (state) {
				case 0:
					if (ch == '-')
						state = 1;
					continue;
				case 1:
					if (ch == '-')
						state = 2;
					continue;
				case 2:
					if (ch == '>')
						break;
					state = 0;
					continue;
				}
				break;
			}
		
			continue;
		}
		
		if (offset < ftell(fp))
			fseek(fp, offset, SEEK_SET);

		if ((ch = fgetc(fp)) == EOF)
			break;

		fputc(ch, stdout);
	}
		
	(void) fclose(fp);
	
	return 0;	
}

