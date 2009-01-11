/*
 * rarlist.c
 *
 * .rar file contents lister.
 *
 * Based on the .rar file format as described by
 *
 * http://datacompression.info/ArchiveFormats/RAR202.txt
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

#include <com/snert/lib/version.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
# include <stdint.h>
# endif
#endif

/* When defined, the little-endian data stream is read in a machine
 * neutral manner. Note that the struct __attribute__((packed)) can
 * be omitted in when machine neutral. If undefined, then the
 * assumption is that the host machine is also little-endian.
 */
#define LITTLE_ENDIAN_STREAM			1

#define RAR_FLAG_REMOVE			0x4000
#define RAR_FLAG_ADD_SIZE		0x8000

#define RAR_TYPE_MARKER			0x72
#define RAR_TYPE_ARCHIVE		0x73
#define RAR_TYPE_FILE			0x74
#define RAR_TYPE_COMMENT		0x75
#define RAR_TYPE_EXTRA			0x76
#define RAR_TYPE_SUBBLOCK		0x77
#define RAR_TYPE_RECOVERY		0x78
#define RAR_TYPE_END			0x7b

typedef struct {
	uint16_t head_crc;
	uint8_t	 head_type;
	uint16_t head_flags;
	uint16_t head_size;
} __attribute__((packed)) RarHeader;

typedef struct {
	uint16_t head_crc;
	uint8_t	 head_type;
	uint16_t head_flags;
	uint16_t head_size;
	uint16_t add_size;
} __attribute__((packed)) RarAddHeader;

/*
 *  	MS DOS Time
 * 	0..4 	5..10 	11..15
 * 	second	minute 	hour
 *
 * 	MS DOS Date
 * 	0..4		5..8		9..15
 * 	day (1 - 31) 	month (1 - 12) 	years from 1980
 */

#define MSDOS_DATE_Y(d)		(((d) >> 9  & 0x007f) + 1980)	/* 1980 .. 2108 */
#define MSDOS_DATE_M(d)		( (d) >> 5  & 0x000f)		/* 1 .. 12 */
#define MSDOS_DATE_D(d)		( (d)       & 0x001f)       	/* 1 .. 31 */

#define MSDOS_TIME_H(t)		( (t) >> 11 & 0x001f)		/* 0 .. 23 */
#define MSDOS_TIME_M(t)		( (t) >> 5  & 0x003f)		/* 0 .. 59 */
#define MSDOS_TIME_S(t)		(((t)       & 0x001f) << 1)	/* 2 second units */

typedef struct {
	uint16_t head_crc;
	uint8_t	 head_type;
	uint16_t head_flags;
	uint16_t head_size;
	uint32_t pack_size;
	uint32_t unp_size;
	uint8_t  host_os;
	uint32_t file_crc;
	uint16_t msdos_time;
	uint16_t msdos_date;
	uint8_t  unp_ver;
	uint8_t  method;
	uint16_t name_size;
	uint32_t attr;
} __attribute__((packed)) RarFileHeader;

static uint8_t rar_marker[] = { 0x52, 0x61, 0x72, 0x21, 0x1a, 0x07, 0x00 };

static uint8_t rar_end_marker[] = { 0xc4, 0x3d, 0x7b, 0x00, 0x40, 0x07, 0x00 };

static const char usage[] =
"usage: rarlist file1.rar ... fileN.rar\n"
;


typedef struct {
	uint64_t value;
	int shift;
} LittleEndian;

void
littleEndianReset(LittleEndian *word)
{
	word->value = 0;
	word->shift = 0;
}

int
littleEndianAddByte(LittleEndian *word, unsigned byte)
{
	word->value |= (byte & 0xFF) << word->shift;
	word->shift += CHAR_BIT;
	return word->shift;
}

uint64_t
littleEndianReadUint(FILE *fp, int bit_length)
{
	LittleEndian word;

	littleEndianReset(&word);
	while (littleEndianAddByte(&word, fgetc(fp)) < bit_length)
		;
	return word.value;
}

uint64_t
littleEndianReadUint64(FILE *fp)
{
	return littleEndianReadUint(fp, 64);
}

uint32_t
littleEndianReadUint32(FILE *fp)
{
	return (uint32_t) littleEndianReadUint(fp, 32);
}

uint16_t
littleEndianReadUint16(FILE *fp)
{
	return (uint16_t) littleEndianReadUint(fp, 16);
}

int
rarGetHeader(FILE *fp, RarHeader *hdr)
{
	hdr->head_crc = littleEndianReadUint16(fp);
	hdr->head_type = fgetc(fp);
	hdr->head_flags = littleEndianReadUint16(fp);
	hdr->head_size = littleEndianReadUint16(fp);

	return feof(fp) || ferror(fp);
}

int
rarGetFileHeader(FILE *fp, RarFileHeader *hdr)
{
	hdr->pack_size = littleEndianReadUint32(fp);
	hdr->unp_size = littleEndianReadUint32(fp);
	hdr->host_os = fgetc(fp);
	hdr->file_crc = littleEndianReadUint32(fp);
	hdr->msdos_time = littleEndianReadUint16(fp);
	hdr->msdos_date = littleEndianReadUint16(fp);
	hdr->unp_ver = fgetc(fp);
	hdr->method = fgetc(fp);
	hdr->name_size = littleEndianReadUint16(fp);
	hdr->attr = littleEndianReadUint32(fp);

	return feof(fp) || ferror(fp);
}

void
rarDumpFileHeader(FILE *fp, RarFileHeader *hdr)
{
	fprintf(fp, "head_crc\t%hx\n", hdr->head_crc);
	fprintf(fp, "head_type\t\t%x\n", hdr->head_type);
	fprintf(fp, "head_flags\t\t%hx\n", hdr->head_flags);
	fprintf(fp, "head_size\t\t%hu\n", hdr->head_size);

	fprintf(fp, "packed\t\t%lu\n", (long) hdr->pack_size);
	fprintf(fp, "size\t\t%lu\n", (long) hdr->unp_size);
	fprintf(fp, "host_os\t\t%u\n", hdr->host_os);
	fprintf(fp, "file_crc\t\t%lx\n", (long) hdr->file_crc);
	fprintf(fp, "time\t\t%hx\n", hdr->msdos_time);
	fprintf(fp, "date\t\t%hx\n", hdr->msdos_date);
	fprintf(fp, "unp_ver\t\t%u\n", hdr->unp_ver);
	fprintf(fp, "method\t\t%u\n", hdr->method);
	fprintf(fp, "name_size\t%hu\n", hdr->name_size);
	fprintf(fp, "attr\t%lx\n", (long) hdr->attr);
}

void
rarlist(const char *filename)
{
	int ch;
	FILE *fp;
	size_t size;
	RarFileHeader hdr;

	if ((fp = fopen(filename, "rb")) == NULL)
		return;

	printf("%s:\n", filename);

	/* Find RAR marker header. */
	while (!feof(fp)) {
		if (fread(&hdr, sizeof (RarHeader), 1, fp) != 1) {
			if (ferror(fp))
				printf("header read error\n");
			goto error0;
		}

		if (memcmp(&hdr, rar_marker, sizeof (rar_marker)) == 0)
			break;
	}


	while (!feof(fp)) {
		if (rarGetHeader(fp, (RarHeader *) &hdr)) {
			if (ferror(fp))
				printf("header read error\n");
			goto error0;
		}

		if (hdr.head_type != RAR_TYPE_FILE) {
			if (fseek(fp, hdr.head_size - sizeof (RarHeader), SEEK_CUR)) {
				printf("seek error\n");
				goto error0;
			}

			if (hdr.head_flags & RAR_FLAG_ADD_SIZE) {
				size = littleEndianReadUint16(fp);

				if (fseek(fp, size, SEEK_CUR)) {
					printf("seek error\n");
					goto error0;
				}
			}
			continue;
		}

		if (rarGetFileHeader(fp, &hdr)) {
			if (ferror(fp))
				printf("header read error\n");
			goto error0;
		}

		printf(
			"%7u %7u %u-%.2u-%.2u %.2u:%.2u:%.2u ",
			hdr.pack_size,
			hdr.unp_size,
			MSDOS_DATE_Y(hdr.msdos_date),
			MSDOS_DATE_M(hdr.msdos_date),
			MSDOS_DATE_D(hdr.msdos_date),
			MSDOS_TIME_H(hdr.msdos_time),
			MSDOS_TIME_M(hdr.msdos_time),
			MSDOS_TIME_S(hdr.msdos_time)
		);

		/* Extract file name. */
		for (size = hdr.name_size; 0 < size; size--) {
			ch = fgetc(fp);
			if (isprint(ch))
				fputc(ch, stdout);
			else
				fputc('?', stdout);
		}

		fputc('\t', stdout);

		if (fseek(fp, hdr.head_size - sizeof (hdr) - hdr.name_size, SEEK_CUR)) {
			printf("seek error\n");
			goto error0;
		}

		if (fseek(fp, hdr.pack_size, SEEK_CUR)) {
			printf("seek error\n");
			goto error0;
		}

		fputc('\n', stdout);
	}
error0:
	(void) fclose(fp);
}

int
main(int argc, char **argv)
{
	int i;

	if (argc <= 1) {
		fprintf(stderr, usage);
		return 1;
	}

	for (i = 1; i < argc; i++) {
		rarlist(argv[i]);
	}

	return 0;
}
