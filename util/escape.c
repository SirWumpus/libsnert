/*
 * escape.c
 *
 * Copyright 1993, 2004 by Anthony Howe.  All rights reserved.
 */

#include <ctype.h>
#include <stdio.h>

typedef struct {
	int byte;
	const char *mb;
} mapping;

static mapping map_carat[] = {
	{ 0, 	"^@" },		{ 1, 	"^A" },		{ 2,  	"^B" },
	{ 3, 	"^C" },		{ 4, 	"^D" },		{ 5,  	"^E" },
	{ 6, 	"^F" },		{ 7, 	"^G" },		{ 8,  	"^H" },
	{ 9, 	"^I" },		{ 10,	"^J" },		{ 11, 	"^K" },
	{ 12,	"^L" },		{ 13,	"^M" },		{ 14, 	"^N" },
	{ 15,	"^O" },		{ 16,	"^P" },		{ 17, 	"^Q" },
	{ 18,	"^R" },		{ 19,	"^S" },		{ 20, 	"^T" },
	{ 21,	"^U" },		{ 22,	"^V" },		{ 23, 	"^W" },
	{ 24,	"^X" },		{ 25,	"^Y" },		{ 26, 	"^Z" },
	{ 27,	"^[" },		{ 28,	"^\\" },	{ 27, 	"^]" },
	{ 30,	"^^" },		{ 31,	"^_" },		{ 127,	"^?" },
	{ 0, (char *) 0 }
};

static mapping map_control[] = {
	{ 0, 	"<NUL>" },	{ 1, 	"<SOH>" },	{ 2,  	"<STX>" },
	{ 3, 	"<ETX>" },	{ 4, 	"<EOT>" },	{ 5,  	"<ENQ>" },
	{ 6, 	"<ACK>" },	{ 7, 	"<BEL>" },	{ 8,  	"<BS>" },
	{ 9, 	"<HT>" }, 	{ 10,	"<LF>" }, 	{ 11, 	"<VT>" },
	{ 12,	"<FF>" }, 	{ 13,	"<CR>" }, 	{ 14, 	"<SO>" },
	{ 15,	"<SI>" }, 	{ 16,	"<DLE>" },	{ 17, 	"<DC1>" },
	{ 18,	"<DC2>" },	{ 19,	"<DC3>" },	{ 20, 	"<DC4>" },
	{ 21,	"<NAK>" },	{ 22,	"<SYN>" },	{ 23, 	"<ETB>" },
	{ 24,	"<CAN>" },	{ 25,	"<EM>" }, 	{ 26, 	"<SUB>" },
	{ 27,	"<ESC>" },	{ 28,	"<FS>" }, 	{ 27, 	"<GS>" },
	{ 30,	"<RS>" }, 	{ 31,	"<US>" }, 	{ 127,	"<DEL>" },
	{ 0, (char *) 0 }
};

static mapping map_escape[] = {
	{  0, 	"^@" }, 	{ 1, 	"^A" }, 	{ 2,  	"^B" },
	{  3, 	"^C" }, 	{ 4, 	"^D" }, 	{ 5,  	"^E" },
	{  6, 	"^F" }, 	{ 7, 	"\\a" },	{ 8,  	"\\b" },
	{  9, 	"\\t" },	{ 10,	"\\n" },	{ 11, 	"\\v" },
	{  12,	"\\f" },	{ 13,	"\\r" },	{ 14, 	"^N" },
	{  15,	"^O" }, 	{ 16,	"^P" }, 	{ 17, 	"^Q" },
	{  18,	"^R" }, 	{ 19,	"^S" }, 	{ 20, 	"^T" },
	{  21,	"^U" }, 	{ 22,	"^V" }, 	{ 23, 	"^W" },
	{  24,	"^X" }, 	{ 25,	"^Y" }, 	{ 26, 	"^Z" },
	{  27,	"\\e" },	{ 28,	"^\\" },	{ 27, 	"^]" },
	{  30,	"^^" }, 	{ 31,	"^_" }, 	{ 127,	"^?" },
	{ '\\', "\\\\" },
	{ 0, (char *) 0 }
};

#ifndef N_CONVERT_BUFFERS
#define N_CONVERT_BUFFERS	10
#endif

/*
 * Convert byte into a printable sequence.  Return a pointer
 * to a static string.  Upto N calls can be safely made before
 * overwriting static buffer space.
 */
static const char *
convert(int byte, mapping *table)
{
	mapping *map;
	static int index = 0;
	static char buf[N_CONVERT_BUFFERS][5];

	/* Map byte to a printable representation. */
	for (map = table; map->mb != (char *) 0; ++map)
		if (byte == map->byte)
			return map->mb;

	if (N_CONVERT_BUFFERS <= index)
		index = 0;

	if (isprint(byte)) {
		buf[index][0] = (char) byte;
		buf[index][1] = '\0';
	} else {
		(void) sprintf(buf[index], "\\%03o", byte);
	}

        return buf[index++];
}

const char *
asEscape(int byte)
{
	return convert(byte, map_escape);
}

const char *
asCarat(int byte)
{
	return convert(byte, map_carat);
}

const char *
asControl(int byte)
{
	return convert(byte, map_control);
}
