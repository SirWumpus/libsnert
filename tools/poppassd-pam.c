/*
 * poppassd.c
 *
 * A PAM based Eudora password server.
 * 
 * Anthony Howe <achowe@snert.com>
 *
 * Derived from version by Pawel Krawczyk <kravietz@echelon.pl>
 *
 *
 * # FreeBSD 4.9 does NOT have a working pam_unix.so that can 
 * # update account passwords. You may get errors like:
 * #
 * #	poppassd[15261]: unable to resolve symbol: pam_sm_chauthtok
 * #
 * # Instead use the /usr/ports/mail/poppassd, which doesn't use PAM.
 * #
 * poppassd    auth        required        pam_unix.so             try_first_pass 
 * poppassd    password    required        pam_unix.so 
 */

#ifndef MAX_LINE_LENGTH
#define MAX_LINE_LENGTH		512
#endif

#ifndef MAX_USER_LENGTH
#define MAX_USER_LENGTH		"64"
#endif

#ifndef MAX_PASS_LENGTH
#define MAX_PASS_LENGTH		"128"
#endif

/***********************************************************************
 *** No configuration below this point.
 ***********************************************************************/

/* Steve Dorner's description of the simple protocol:
 *
 * The server's responses should be like an FTP server's responses; 
 * 1xx for in progress, 2xx for success, 3xx for more information
 * needed, 4xx for temporary failure, and 5xx for permanent failure.  
 * Putting it all together, here's a sample conversation:
 *
 *   S: 200 hello\r\n
 *   E: user yourloginname\r\n
 *   S: 300 please send your password now\r\n
 *   E: pass yourcurrentpassword\r\n
 *   S: 200 My, that was tasty\r\n
 *   E: newpass yournewpassword\r\n
 *   S: 200 Happy to oblige\r\n
 *   E: quit\r\n
 *   S: 200 Bye-bye\r\n
 *   S: <closes connection>
 *   E: <closes connection>
 */
 
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <security/pam_appl.h>

#define POP_OLDPASS 0
#define POP_NEWPASS 1
#define POP_SKIPASS 2

static short int pop_state;

static char line[MAX_LINE_LENGTH];
static char user[MAX_LINE_LENGTH];
static char oldpass[MAX_LINE_LENGTH];
static char newpass[MAX_LINE_LENGTH];

void
WriteToClient(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	vfprintf(stdout, fmt, ap);
	fputs("\r\n", stdout);
	fflush(stdout);

	va_end(ap);
}

long
TextInputLine(FILE *fp, char *line, long size)
{
	long i;

	for (i = 0, --size; i < size; ++i) {
		line[i] = (char) fgetc(fp);

		if (feof(fp) || ferror(fp))
			return -1;

		if (line[i] == '\n') {
			line[i] = '\0';
			if (0 < i && line[i-1] == '\r')
				line[--i] = '\0';
			break;
		}
	}

	line[i] = '\0';

	return i;
}

void
TextLowerWord(char *buffer)
{
	for ( ; isalpha(*buffer); buffer++)
		*buffer = tolower(*buffer);
}

int 
conv(int num_msg, const struct pam_message **msg, struct pam_response **response, void *appdata_ptr)
{
	int i;
	struct pam_response *r;

	if (num_msg <= 0)
		return(PAM_CONV_ERR);

	r = malloc(sizeof(struct pam_response) * num_msg);

	if (r == NULL)
		return(PAM_CONV_ERR);

	for (i = 0; i < num_msg; i++) {
		if (msg[i]->msg_style == PAM_ERROR_MSG) {
			WriteToClient("500 PAM error: %s", msg[i]->msg);
			syslog(LOG_ERR, "PAM error: %s", msg[i]->msg);

			/* If there is an error, we don't want to be sending
			 * in anything more, so set pop_state to invalid
			 */
			pop_state = POP_SKIPASS;
		}

		r[i].resp_retcode = 0;
		
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
			switch (pop_state) {
			case POP_OLDPASS:
				r[i].resp = strdup(oldpass);  
				break;
			case POP_NEWPASS:
				r[i].resp = strdup(newpass); 
				break;
			case POP_SKIPASS:
				r[i].resp = NULL;
				break;
			default: 
				syslog(LOG_ERR, "PAM error: invalid switch state=%d", pop_state);
			}
		} else {
			r[i].resp = strdup("");
		}
	}

	*response = r;
	
	return PAM_SUCCESS;
}
	
int
main(int argc, char **argv)
{
	int rc;
	pam_handle_t *pamh;
	struct pam_conv pamc;
	long length, maxUserLength, maxPassLength;

	pop_state = POP_OLDPASS;

	pamc.conv = conv;
	maxUserLength = strtol(MAX_USER_LENGTH, NULL, 10);
	maxPassLength = strtol(MAX_PASS_LENGTH, NULL, 10);     

	openlog("poppassd", LOG_PID, LOG_LOCAL4);

	WriteToClient("200 poppassd");     
	length = TextInputLine(stdin, line, sizeof (line));

	if (maxUserLength + 5 < length) {
		WriteToClient("500 username too long, max %s", MAX_USER_LENGTH);
		exit(1);
	}
     	
     	TextLowerWord(line);     	
	sscanf(line, "user %" MAX_USER_LENGTH "s", user);
	if (strlen(user) <= 0) {
		WriteToClient("500 username required");
		exit(1);
	}

	if (pam_start("poppassd", user, &pamc, &pamh) != PAM_SUCCESS) {
		WriteToClient("500 invalid username");
		exit(1);
	}

	WriteToClient("200 enter current password");
	length = TextInputLine(stdin, line, sizeof (line));	
	if (maxPassLength + 5 < length) {
		WriteToClient("500 password too long, max %s", MAX_PASS_LENGTH);
		exit(1);
	}
	
     	TextLowerWord(line);     	
	sscanf(line, "pass %" MAX_PASS_LENGTH "s", oldpass);
	if (strlen(oldpass) <= 0) {
		WriteToClient("500 password required");
		exit(1);
	}

	if (pam_authenticate(pamh, 0) != PAM_SUCCESS) {
		syslog(LOG_ERR, "invalid password, user=%s pass=%s", user, oldpass);
		WriteToClient("500 username and/or password incorrect");
		exit(1);
	}

	WriteToClient("200 enter new password");
	length = TextInputLine(stdin, line, sizeof (line));	
	if (maxPassLength + 8 < length) {
		WriteToClient("500 password too long, max %s", MAX_PASS_LENGTH);
		exit(1);
	}

     	TextLowerWord(line);     	
	sscanf(line, "newpass %" MAX_PASS_LENGTH "s", newpass);
	if (strlen(oldpass) <= 0) {
		WriteToClient("500 password required");
		exit(1);
	}

	pop_state = POP_NEWPASS;

	if ((rc = pam_chauthtok(pamh, 0)) != PAM_SUCCESS) {
		syslog(LOG_ERR, "failed to change password, user=%s pass=%s newpass=%s rc=%d", user, oldpass, newpass, rc);
		WriteToClient("500 password not changed");
		exit(1);
	}
	
	syslog(LOG_ERR, "password changed for user=%s", user);
	WriteToClient("200 password updated");

	length = TextInputLine(stdin, line, sizeof (line));
	TextLowerWord(line);	
	if (strncmp(line, "quit", 4) != 0) {
		WriteToClient("500 unknown command");
		exit(1);
	}
	
	WriteToClient("200 bye");
	pam_end(pamh, 0);
	closelog();
	
	exit(0);
}

