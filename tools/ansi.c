/*
 * ansi.c
 *
 * Copyright 2003, 2013 by Anthony Howe.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char usage[] =
"\033[1musage: ansi word ...\033[0m\n"

"\n"
"\033[4mSpecial characters:\033[0m\n"
"bell\t\tASCII bell\n"
"cr\t\tcarriage return\n"
"esc\t\tASCII escape\n"
"lf\t\tline feed\n"
"sp\t\tspace\n"
"tab\t\ttab\n"
"vt\t\tvertical tab\n"

"\n"
"\033[4mANSI cursor motion:\033[0m\n"
"down [N]\tmove down 1 or N lines\n"
"goto ROW COL\tmove cursor to 1-based row and column\n"
"home\t\tmove cursor to top of the screen (1, 1)\n"
"left [N]\tmove left 1 or N lines\n"
"pop\t\tretsore cursor position & attributes\n"
"push\t\tsave cursor position & attributes\n"
"restore\t\trestore cursor position\n"
"right [N]\tmove right 1 or N lines\n"
"save\t\tsave cursor position\n"
"tab [N]\t\tmove 1 or N tab stops\n"
"up [N]\t\tmove up 1 or N lines\n"

"\n"
"\033[4mANSI edit & scrolling:\033[0m\n"
"delete [N]\tdelete 1 or N lines\n"
"insert [N]\tinsert 1 or N lines\n"
"scroll down\tscroll screen down one line\n"
"scroll up\tscroll screen up one line\n"
"scroll display\tswitch to scrolling entire display\n"
"scroll R1 R2\tswitch to scrolling between R1 and R2\n"
"erase down\terase from cursor to bottom of screen\n"
"erase left\terase from cursor to left margin\n"
"erase line\terase the current line\n"
"erase right\terase from cursor to right margin\n"
"erase screen\terase the screen\n"
"erase up\terase from cursor to top of screen\n"

"\n"
"\033[4mANSI video attributes:\033[0m\n"
"blink\t\tblink\n"
"bold\t\tbold or bright\n"
"bright\t\tbold or bright\n"
"dim\t\tdim (not always implemented)\n"
"hide\t\thidden\n"
"normal\t\tnormal display\n"
"reverse\t\treverse video\n"
"standout\tstandout video (not always implemented)\n"
"underline\tunderline\n"

"black\t\tforeground black\n"
"red\t\tforeground red\n"
"green\t\tforeground green\n"
"yellow\t\tforeground yellow\n"
"blue\t\tforeground blue\n"
"magenta\t\tforeground magenta\n"
"cyan\t\tforeground cyan\n"
"white\t\tforeground white\n"

"BLACK\t\tbackground black\n"
"RED\t\tbackground red\n"
"GREEN\t\tbackground green\n"
"YELLOW\t\tbackground yellow\n"
"BLUE\t\tbackground blue\n"
"MAGENTA\t\tbackground magenta\n"
"CYAN\t\tbackground cyan\n"
"WHITE\t\tbackground white\n"

"font default\tswitch to default font\n"
"font other\tswitch to other font\n"

"\n"
"\033[4mANSI miscellaneous:\033[0m\n"
"log start\tstart sending text to printer\n"
"log stop\tstop sending text to printer\n"
"print screen\tprint the screen\n"
"print line\tprint the current line\n"
"reset\t\treset terminal\n"
"tab on\t\tset tab at current cursor position\n"
"tab off\t\tremove tab at current cursor position\n"
"tab clear\tclear all tabs\n"
"wrap on\t\tenable line wrap\n"
"wrap off\tdisable line wrap\n"
"where\t\tquery cursor position\n"

"\n"
"\033[4mNotes:\033[0m\n"
"All other words are printed to the screen. Words beginning with\n"
"backslash (\\) are treated as a literal word. Some ANSI terminal\n"
"emulators do not support all possible escape sequences.\n"
"\n"
"\033[1mansi/1.1 Copyright 2003, 2013 by Anthony Howe. All rights reserved.\033[0m\n"
;

int
ansi(char *once, char *many, char *value)
{
	char *stop;
	long number;

	if (value == (char *) 0) {
		printf(once);
		return 0;
	}

	number = strtol(value, &stop, 10);
	if (*stop != '\0' || number < 1) {
		printf(once);
		return 0;
	}

	printf(many, number);

	return 1;
}

char *single[][2] = {
	{ "\r", 	"cr" },
	{ "\n", 	"lf" },
	{ "\n", 	"nl" },
	{ "\v", 	"vt" },
	{ " ", 		"sp" },
	{ "\t", 	"tab" },
	{ "\r\n", 	"crlf" },
	{ "\007", 	"bell" },
	{ "\033", 	"esc" },

	{ "\033c", 	"reset" },
	{ "\0337",	"push" },
	{ "\0338",	"pop" },
	{ "\033[s",	"save" },
	{ "\033[u",	"restore" },

	{ "\033[H", 	"home" },

	{ "\033[0m", 	"normal" },
	{ "\033[1m", 	"bold" },
	{ "\033[1m", 	"bright" },
	{ "\033[2m", 	"dim" },	
	{ "\033[3m", 	"standout" },	
	{ "\033[4m", 	"underline" },
	{ "\033[5m", 	"blink" },
	{ "\033[7m", 	"reverse" },
	{ "\033[8m", 	"hide" },

	{ "\033[22m", 	"bold-off" },
	{ "\033[23m", 	"standout-off" },
	{ "\033[24m", 	"underline-off" },
	{ "\033[25m", 	"blink-off" },
	{ "\033[27m", 	"reverse-off" },
	{ "\033[28m", 	"show" },

	{ "\033[30m", 	"black" },
	{ "\033[31m", 	"red" },
	{ "\033[32m", 	"green" },
	{ "\033[33m", 	"yellow" },
	{ "\033[34m", 	"blue" },
	{ "\033[35m", 	"magenta" },
	{ "\033[36m", 	"cyan" },
	{ "\033[37m", 	"white" },

	{ "\033[40m", 	"BLACK" },
	{ "\033[41m", 	"RED" },
	{ "\033[42m", 	"GREEN" },
	{ "\033[43m", 	"YELLOW" },
	{ "\033[44m", 	"BLUE" },
	{ "\033[45m", 	"MAGENTA" },
	{ "\033[46m", 	"CYAN" },
	{ "\033[47m", 	"WHITE" },

	{ 0, 0 }
};

void
atExit()
{
	system("stty -cbreak");
}

int
main(int argc, char **argv)
{
	long a, b;
	char *stop, *word;
	int i, last_literal_word;

	if (argc < 2) {
		fprintf(stderr, usage);
		return 2;
	}

	last_literal_word = -1;
	
	atexit(atExit);
	system("stty cbreak");
	
	for (i = 1; i < argc; i++) {
		word = argv[i];

		if (word[0] == '\\') {
			word++;
		} else if (strcmp(word, "goto") == 0) {
			if (argc <= i + 2)
				return 1;

			a = strtol(argv[i+1], &stop, 10);
			if (*stop != '\0' || a < 1)
				return 1;

			b = strtol(argv[i+2], &stop, 10);
			if (*stop != '\0' || b < 1)
				return 1;

			printf("\033[%ld;%ldH", a, b);
			i += 2;
			continue;
		} else if (strcmp(word, "up") == 0) {
			i += ansi("\033[A", "\033[%ldA", argv[i+1]);
			continue;
		} else if (strcmp(word, "down") == 0) {
			i += ansi("\033[B", "\033[%ldB", argv[i+1]);
			continue;
		} else if (strcmp(word, "right") == 0) {
			i += ansi("\033[C", "\033[%ldC", argv[i+1]);
			continue;
		} else if (strcmp(word, "left") == 0) {
			i += ansi("\033[D", "\033[%ldD", argv[i+1]);
			continue;
		} else if (strcmp(word, "insert") == 0) {
			i += ansi("\033[L", "\033[%ldL", argv[i+1]);
			continue;
		} else if (strcmp(word, "delete") == 0) {
			i += ansi("\033[M", "\033[%ldM", argv[i+1]);
			continue;
		} else if (strcmp(word, "erase") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "down") == 0)
				printf("\033[J");
			else if (strcmp(word, "up") == 0)
				printf("\033[1J");
			else if (strcmp(word, "screen") == 0)
				printf("\033[2J");
			else if (strcmp(word, "right") == 0)
				printf("\033[K");
			else if (strcmp(word, "left") == 0)
				printf("\033[1K");
			else if (strcmp(word, "line") == 0)
				printf("\033[2K");
			else
				return 1;
			continue;
		} else if (strcmp(word, "wrap") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "on") == 0)
				printf("\033[7h");
			else if (strcmp(word, "off") == 0)
				printf("\033[7l");
			else
				return 1;
			continue;
		} else if (strcmp(word, "print") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "screen") == 0)
				printf("\033[i");
			else if (strcmp(word, "line") == 0)
				printf("\033[1i");
			else
				return 1;
			continue;
		} else if (strcmp(word, "log") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "start") == 0)
				printf("\033[5i");
			else if (strcmp(word, "stop") == 0)
				printf("\033[4i");
			else
				return 1;
			continue;
		} else if (strcmp(word, "font") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "default") == 0)
				printf("\033(");
			else if (strcmp(word, "other") == 0)
				printf("\033)");
			else
				return 1;
			continue;
		} else if (strcmp(word, "tab") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "on") == 0)
				printf("\033H");
			else if (strcmp(word, "off") == 0)
				printf("\033[g");
			else if (strcmp(word, "clear") == 0)
				printf("\033[3g");
			else if (ansi("\t", "\033[%ldI", word) == 0)
		    		i--;

			continue;
		} else if (strcmp(word, "next") == 0) {
			i += ansi("\033[I", "\033[%ldI", argv[i+1]);
			continue;
		} else if (strcmp(word, "scroll") == 0) {
			if (argc <= ++i)
				return 1;

			word = argv[i];

			if (strcmp(word, "down") == 0)
				printf("\033D");
			else if (strcmp(word, "up") == 0)
				printf("\033U");
			else if (strcmp(word, "display") == 0)
				printf("\033[r");
			else {
				a = strtol(word, &stop, 10);
				if (*stop != '\0' || a < 1)
					return 1;

				if (argc <= ++i)
					return 1;

				b = strtol(argv[i], &stop, 10);
				if (*stop != '\0' || b < 1)
					return 1;

				if (b <= a)
					return 1;

				printf("\033[%ld;%ldr", a, b);
			}
			continue;
		} else if (strcmp(word, "where") == 0) {
			int row, col;
			
			printf("\033[6n");
			scanf("\033[%d;%dR", &row, &col);
			printf("(%d, %d)", row, col);
			fflush(stdout);			
			continue;
		} else if (strcmp(word, "-?") == 0 || strcmp(word, "--help") == 0 || strcmp(word, "help") == 0) {
			fprintf(stderr, usage);
			return 2;
		} else {
			char *(*p)[2];

			for (p = single; (*p)[0] != (char *) 0; p++) {
				if (strcmp(word, (*p)[1]) == 0) {
					printf((*p)[0]);
					break;
				}
			}

			if ((*p)[0] != (char *) 0)
				continue;
		}

		if (last_literal_word + 1 == i)
			fputc(' ', stdout);
		last_literal_word = i;
		fputs(word, stdout);
	}

	fflush(stdout);

	return 0;
}


