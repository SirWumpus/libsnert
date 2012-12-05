/* -*- mode: c; c-file-style: "k&r" -*-

   natsort.c -- Example strnatcmp application.

   Copyright (C) 2000 by Martin Pool <mbp@humbug.org.au>

   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
      claim that you wrote the original software. If you use this software
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.
   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original software.
   3. This notice may not be removed or altered from any source distribution.

  Originally from http://sourcefrog.net/projects/natsort/
*/

/* Partial change history:
 *
 * 2003-03-18: Add --reverse option, from Alessandro Pisani.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

extern ssize_t getline(char **linep, size_t *np, FILE *fp);
extern ssize_t getdelim(char **linep, size_t *np, int delim, FILE *fp);

extern int strnatcmp(const char *s1, const char *s2);
extern int strnatcasecmp(const char *s1, const char *s2);

#if defined(__GNUC__)
#  define UNUSED __attribute__((__unused__))
#endif

static int fold_case = 0, verbose = 0, reverse = 0;

static void trace_result(char const *a, char const *b, int ret)
{
     char const *op;

     if (ret < 0)
	  op = "<";
     else if (ret > 0)
	  op = ">";
     else
	  op = "==";

     fprintf(stderr, "\tstrncatcmp: \"%s\" %s \"%s\"\n",
	     a, op, b);
}



static int compare_strings(const void *a, const void *b)
{
     char const *pa = *(char const **)a, *pb = *(char const **)b;
     int ret;

     if (fold_case)
	  ret = strnatcasecmp(pa, pb);
     else
	  ret = strnatcmp(pa, pb);

	 if (reverse)
	  ret *= -1;

	 if (verbose)
	  trace_result(pa, pb, ret);

     return ret;
}

static char usage[] =
"usage: natsort [-irv] <input >output\n"
"\n"
"-i\t\tignore case\n"
"-r\t\treverse sort order\n"
"-v\t\tverbose debug info\n"
"\n"
;

int
main(int argc, char **argv)
{
     int nlines = 0;
     char *line;
     char **list = 0;
     int linelen = 0, i;
     int c;
     size_t bufsize;

     /* process arguments */
     while ((c = getopt(argc, argv, "irv")) != -1) {
	  switch (c) {
	  case 'i':
	       fold_case = 1;
	       break;
	  case 'r':
	  	   reverse = 1;
		   break;
	  case 'v':
	       verbose = 1;
	       break;
	  default:
	       fprintf(stderr, usage);
	       return 2;
	  }
     }

     /* read lines into an array */
     while (1) {
	  line = NULL;
	  bufsize = 0;
	  if ((linelen = getline(&line, &bufsize, stdin)) <= 0)
	       break;
	  if (line[linelen-1] == '\n')
	       line[--linelen] = 0;
	  nlines++;
	  list = (char **) realloc(list, nlines * sizeof list[0]);
	  if (!list) {
	       perror("allocate list");
	       return 1;
	  }
	  list[nlines-1] = line;
     }

     if (ferror(stdin)) {
	  perror("input");
	  return 1;
     }
     fclose(stdin);

     /* quicksort */
     qsort(list, nlines, sizeof list[0], compare_strings);

     /* and output */
     for (i = 0; i < nlines; i++) {
	  puts(list[i]);
     }
     if (ferror(stdout)) {
	  perror("output");
	  return 1;
     }

     return 0;
}
