/*
 * mkpath.c
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <com/snert/lib/io/file.h>
#include <com/snert/lib/util/Buf.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/type/Vector.h>

#if defined(__BORLANDC__)
# include <dir.h>
#endif

/*
 * Make a directory path.
 *
 * @param path
 *	The absolute or relative directory path to create.
 *
 * @return
 *	Zero on success, otherwise -1 on error.
 */
int
mkpath(const char *path)
{
	Buf *dir;
	char *segment;
	struct stat sb;
	Vector segments;
	int i, rc = -1;

	/* Check if complete path already exists. */
	if (stat(path, &sb) == 0)
		return -(!S_ISDIR(sb.st_mode));

	/* Otherwise break it down and create missing segements. */
	if ((segments = TextSplit((const char *) path, "\\/", 0)) == NULL)
		goto error0;

	if ((dir = BufCreate(100)) == NULL)
		goto error1;

	if (*path == '/' || *path == '\\')
		BufAddByte(dir, '/');

	for (i = 0; i < VectorLength(segments); ++i) {
		segment = (char *) VectorGet(segments, i);
		if (segment == NULL)
			goto error2;

		BufAddString(dir, segment);
		BufAddByte(dir, '\0');

		if (stat((char *) BufBytes(dir), &sb) < 0) {
#if defined(__WIN32__)
			if (mkdir((char *) BufBytes(dir)))
#else
			if (mkdir((char *) BufBytes(dir), S_IRWXU|S_IRWXG|S_IRWXO))
#endif
				goto error2;
		} else if (!S_ISDIR(sb.st_mode)) {
			goto error2;
		}

		BufSetLength(dir, BufLength(dir)-1);
		BufAddByte(dir, '/');
	}

	rc = 0;
error2:
	BufDestroy(dir);
error1:
	VectorDestroy(segments);
error0:
	return rc;
}

