#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static char usage[] = "usage: inplace 'shell command' file ...\n";

int
process(const char *cmd, const char *file) 
{
	char *tname;
	size_t length;
	struct stat sb;
	FILE *read_pipe, *tfp;
	int ex, fd, tfd, tsize;
	char buffer[_POSIX_PIPE_BUF];

	ex = -1;	
	
	/* Does the file exist and is writable? */
	if (stat(file, &sb) < 0 || !S_ISREG(sb.st_mode) 
	|| (sb.st_mode & (S_IRUSR|S_IWUSR)) != (S_IRUSR|S_IWUSR)) {
		warnx("%s not r/w file", file);
		goto error0;
	}

	/* Make temporary file name template. */
	tsize = snprintf(NULL, 0, "%s.XXXXXX", file)+1;
	if ((tname = malloc(tsize)) == NULL) {
		warn(NULL);
		goto error1;
	}
	(void) snprintf(tname, tsize, "%s.XXXXXX", file);	
				
	/* Open the file and redirect to our stdin. */
	if ((fd = open(file, O_RDONLY)) < 0) {
		warn("%s", file);
		goto error1;
	}
	if (dup2(fd, STDIN_FILENO) != STDIN_FILENO) {
		warn("%s", file);
		goto error2;
	}

	/* Open temporary file. */
	if ((tfd = mkstemp(tname)) < 0) {
		warn("%s", file);
		goto error2;
	}
	if ((tfp = fdopen(tfd, "w")) == NULL) {
		(void) close(tfd);
		warn("%s", file);
		goto error2;
	}
	
	/* Create half-duplex pipe, child will inherit our redirected stdin. */
	(void) fprintf(stderr, "%s < %s\n", cmd, file);
	if ((read_pipe = popen(cmd, "r")) == NULL) {
		warn("%s <%s", cmd, file);
		goto error3;
	}

	/* Read command result into temporary file. */		
	while (0 < (length = fread(buffer, 1, sizeof (buffer), read_pipe)))  {
		if (fwrite(buffer, 1, length, tfp) != length) {
			warn("%s", file);
			goto error4;
		}
	}

	/* Replace the original source file by the modified copy. */			
	if (rename(tname, file) < 0) {
		warn("rename(%s, %s)", tname, file);
		goto error4;
	}
	ex = 0;
error4:
	(void) pclose(read_pipe);
error3:
	(void) fclose(tfp);
error2:
	(void) close(fd);
error1:
	free(tname);
error0:
	return ex;
}

int
main(int argc, char **argv)
{
	int argi, ex;

	if (argc < 3) {
		(void) fputs(usage, stderr);
		return EXIT_FAILURE;
	}

	ex = EXIT_SUCCESS;
	for (argi = 2; argi < argc; argi++) {
		if (process(argv[1], argv[argi])) {
			ex = EXIT_FAILURE;
		}
	}
		
	return ex;
}
