/*
 * TextMap.c
 *
 * Copyright 1993, 2005 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/util/Text.h>

/***********************************************************************
 ***
 ***********************************************************************/

typedef struct {
	int octet;
	const char *mb;
} mapping;

static mapping map_carat[] = {
	{ 0x00, "^@" }, 	{ 0x01, "^A" }, 	{ 0x02, "^B" },
	{ 0x03, "^C" }, 	{ 0x04, "^D" }, 	{ 0x05, "^E" },
	{ 0x06, "^F" }, 	{ 0x07, "^G" }, 	{ 0x08, "^H" },
	{ 0x09, "^I" }, 	{ 0x0A, "^J" }, 	{ 0x0B, "^K" },
	{ 0x0C, "^L" }, 	{ 0x0D, "^M" }, 	{ 0x0E, "^N" },
	{ 0x0F, "^O" }, 	{ 0x10, "^P" }, 	{ 0x11, "^Q" },
	{ 0x12, "^R" }, 	{ 0x13, "^S" }, 	{ 0x14, "^T" },
	{ 0x15, "^U" }, 	{ 0x16, "^V" }, 	{ 0x17, "^W" },
	{ 0x18, "^X" }, 	{ 0x19, "^Y" }, 	{ 0x1A, "^Z" },
	{ 0x1B, "^[" }, 	{ 0x1C, "^\\" },	{ 0x1D, "^]" },
	{ 0x1E, "^^" }, 	{ 0x1F, "^_" }, 	{ 0x7F, "^?" },
	{ 0, NULL }
};

static mapping map_control[] = {
	{ 0x00, "<NUL>" }, 	{ 0x01, "<SOH>" }, 	{ 0x02, "<STX>" },
	{ 0x03, "<ETX>" }, 	{ 0x04, "<EOT>" }, 	{ 0x05, "<ENQ>" },
	{ 0x06, "<ACK>" }, 	{ 0x07, "<BEL>" }, 	{ 0x08, "<BS>" },
	{ 0x09, "<HT>" }, 	{ 0x0A, "<LF>" }, 	{ 0x0B, "<VT>" },
	{ 0x0C, "<FF>" }, 	{ 0x0D, "<CR>" }, 	{ 0x0E, "<SO>" },
	{ 0x0F, "<SI>" }, 	{ 0x10, "<DLE>" }, 	{ 0x11, "<DC1>" },
	{ 0x12, "<DC2>" }, 	{ 0x13, "<DC3>" }, 	{ 0x14, "<DC4>" },
	{ 0x15, "<NAK>" }, 	{ 0x16, "<SYN>" }, 	{ 0x17, "<ETB>" },
	{ 0x18, "<CAN>" }, 	{ 0x19, "<EM>" }, 	{ 0x1A, "<SUB>" },
	{ 0x1B, "<ESC>" }, 	{ 0x1C, "<FS>" }, 	{ 0x1D, "<GS>" },
	{ 0x1E, "<RS>" }, 	{ 0x1F, "<US>" }, 	{ 0x7F, "<DEL>" },
	{ 0, NULL }
};

static mapping map_escape[] = {
	{ 0x00, "\\x00" }, 	{ 0x01, "\\x01" }, 	{ 0x02, "\\x02" },
	{ 0x03, "\\x03" }, 	{ 0x04, "\\x04" }, 	{ 0x05, "\\x05" },
	{ 0x06, "\\x06" }, 	{ 0x07, "\\a" }, 	{ 0x08, "\\b" },
	{ 0x09, "\\t" }, 	{ 0x0A, "\\n" }, 	{ 0x0B, "\\v" },
	{ 0x0C, "\\f" }, 	{ 0x0D, "\\r" }, 	{ 0x0E, "\\x0E" },
	{ 0x0F, "\\x0F" }, 	{ 0x10, "\\x10" }, 	{ 0x11, "\\x11" },
	{ 0x12, "\\x12" }, 	{ 0x13, "\\x13" }, 	{ 0x14, "\\x14" },
	{ 0x15, "\\x15" }, 	{ 0x16, "\\x16" }, 	{ 0x17, "\\x17" },
	{ 0x18, "\\x18" }, 	{ 0x19, "\\x19" }, 	{ 0x1A, "\\x1A" },
	{ 0x1B, "\\e" }, 	{ 0x1C, "\\x1C" }, 	{ 0x1D, "\\x1D" },
	{ 0x1E, "\\x1E" }, 	{ 0x1F, "\\x1F" },	{ 0x7F, "\\x7F" },
	{ 0, NULL }
};

/*
 * Convert non-printable ASCII control characters into a printable
 * sequence.
 *
 * @return
 *	A pointer to a static C string; otherwise NULL if the octet
 *	is not an ASCII control character.
 */
static const char *
convert(int octet, mapping *table)
{
	if (0 <= octet && octet < 0x20)
		return table[octet].mb;

	if (octet == 0x7F)
		return table[0x20].mb;

	if (octet == '\\' && table == map_escape)
		return "\\\\";

	return NULL;
}

const char *
TextEscape(int octet)
{
	return convert(octet, map_escape);
}

const char *
TextCarat(int octet)
{
	return convert(octet, map_carat);
}

const char *
TextControl(int octet)
{
	return convert(octet, map_control);
}
