/*
 * ziplist.c
 *
 * .zip file contents lister.
 *
 * Based on the .zip file format as described by
 *
 * http://www.pkware.com/documents/casestudies/APPNOTE.TXT
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

#define ZIP_LOCAL_FILE_HEADER_SIG		0x04034b50L
#define ZIP_DATA_DESCRIPTOR_SIG			0x08074b50L
#define ZIP_ARCHIVE_EXTRA_DATA_SIG		0x08064b50L
#define ZIP_DIRECTORY_FILE_HEADER_SIG		0x02014b50L
#define ZIP_DIRECTORY_DIGITAL_SIG		0x05054b50L
#define ZIP_DIRECTORY_ZIP64_RECORD_SIG		0x06064b50L
#define ZIP_DIRECTORY_ZIP64_LOCATOR_SIG		0x07064b50L
#define ZIP_DIRECTORY_END_RECORD_SIG		0x06054b50L

#define ZIP_EXTRA_ZIP64			0x0001

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
	uint32_t signature;
} __attribute__((packed)) ZipSignature;

typedef struct {
	uint32_t signature;
	uint16_t version;
	uint16_t flags;
	uint16_t compression_method;
	uint16_t msdos_time;
	uint16_t msdos_date;
	uint32_t crc;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint16_t filename_length;
	uint16_t extra_length;
} __attribute__((packed)) ZipLocalFileHeader;

typedef struct {
	uint32_t signature;
	uint32_t crc32;
	uint64_t compressed_size;
	uint64_t uncompressed_size;
} __attribute__((packed)) ZipDataDescriptor2;

typedef struct {
	uint32_t signature;
	uint32_t crc32;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
} __attribute__((packed)) ZipDataDescriptor1;

typedef struct {
	uint32_t crc32;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
} __attribute__((packed)) ZipDataDescriptor0;

typedef struct {
	uint16_t header_id;
	uint16_t data_size;
} __attribute__((packed)) ZipExtraBlock;

typedef struct {
	uint16_t header_id;
	uint16_t data_size;
	uint64_t uncompressed_size;
	uint64_t compressed_size;
	uint64_t offset_local_header;
	uint32_t disk_number;
} __attribute__((packed)) ZipExtraZip64;


static const char usage[] =
"usage: ziplist file1.zip ... fileN.zip\n"
;


#ifdef LITTLE_ENDIAN_STREAM
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
zipGetLocalFileHeader(FILE *fp, ZipLocalFileHeader *hdr)
{
	hdr->signature = littleEndianReadUint32(fp);
	hdr->version = littleEndianReadUint16(fp);
	hdr->flags = littleEndianReadUint16(fp);
	hdr->compression_method = littleEndianReadUint16(fp);
	hdr->msdos_time = littleEndianReadUint16(fp);
	hdr->msdos_date = littleEndianReadUint16(fp);
	hdr->crc = littleEndianReadUint32(fp);
	hdr->compressed_size = littleEndianReadUint32(fp);
	hdr->uncompressed_size = littleEndianReadUint32(fp);
	hdr->filename_length = littleEndianReadUint16(fp);
	hdr->extra_length = littleEndianReadUint16(fp);

	return feof(fp) || ferror(fp);
}

int
zipGetDataDescriptor1(FILE *fp, ZipDataDescriptor1 *hdr)
{
	hdr->signature = littleEndianReadUint32(fp);
	hdr->crc32 = littleEndianReadUint32(fp);
	hdr->compressed_size = littleEndianReadUint32(fp);
	hdr->uncompressed_size = littleEndianReadUint32(fp);

	return feof(fp) || ferror(fp);
}
#endif

#ifdef ENABLE_ZIP64_SUPPORT
ZipExtraBlock *
zipSearchExtraField(ZipExtraBlock *block, size_t size, uint16_t header_id)
{
	for ( ; 0 < size; size -= block->data_size, (unsigned char *) block += block->data_size) {
		if (block->header_id == header_id)
			return block;
	}

	return NULL;
}
#endif

uint32_t
zipNextSignature(FILE *fp)
{
	uint32_t sig;

	while (!feof(fp)) {
#ifndef LITTLE_ENDIAN_STREAM
		if (fread(&sig, sizeof(sig), 1, fp) != 1)
#else
		sig = littleEndianReadUint32(fp);
		if (feof(fp) || ferror(fp))
#endif
			return -1;

		switch (sig) {
		case ZIP_LOCAL_FILE_HEADER_SIG:
		case ZIP_DATA_DESCRIPTOR_SIG:
		case ZIP_ARCHIVE_EXTRA_DATA_SIG:
		case ZIP_DIRECTORY_FILE_HEADER_SIG:
		case ZIP_DIRECTORY_DIGITAL_SIG:
		case ZIP_DIRECTORY_ZIP64_RECORD_SIG:
		case ZIP_DIRECTORY_ZIP64_LOCATOR_SIG:
		case ZIP_DIRECTORY_END_RECORD_SIG:
			return sig;
		}
	}

	return 0;
}

int
zipNextData(FILE *fp, uint32_t *compressed, uint32_t *uncompressed)
{
	ZipDataDescriptor1 data;

	while ((data.signature = zipNextSignature(fp)) != 0) {
		if (ZIP_DATA_DESCRIPTOR_SIG) {
			(void) fseek(fp, - sizeof (ZipSignature), SEEK_CUR);
#ifndef LITTLE_ENDIAN_STREAM
			if (fread(&data, sizeof (ZipDataDescriptor1), 1, fp) == 1) {
#else
			if (zipGetDataDescriptor1(fp, &data) == 0) {
#endif
				*uncompressed = data.uncompressed_size;
				*compressed = data.compressed_size;
				break;
			}
		}
		if (ZIP_LOCAL_FILE_HEADER_SIG)
			break;

		if (feof(fp))
			return -1;
	}

	(void) fseek(fp, - sizeof (ZipSignature), SEEK_CUR);

	return 0;
}

int
zipNextFile(FILE *fp)
{
	while (zipNextSignature(fp) != ZIP_LOCAL_FILE_HEADER_SIG) {
		if (feof(fp))
			return -1;
	}

	(void) fseek(fp, - sizeof (ZipSignature), SEEK_CUR);

	return 0;
}

void
zipDumpFileHeader(FILE *fp, ZipLocalFileHeader *hdr)
{
	fprintf(fp, "signature\t%lx\n", (long) hdr->signature);
	fprintf(fp, "version\t\t%hu\n", hdr->version);
	fprintf(fp, "flags\t\t%hx\n", hdr->flags);
	fprintf(fp, "method\t\t%lx\n", (long) hdr->compression_method);
	fprintf(fp, "time\t\t%hx\n", hdr->msdos_time);
	fprintf(fp, "date\t\t%hx\n", hdr->msdos_date);
	fprintf(fp, "crc\t\t%lx\n", (long) hdr->crc);
	fprintf(fp, "packed\t\t%lu\n", (long) hdr->compressed_size);
	fprintf(fp, "size\t\t%lu\n", (long) hdr->uncompressed_size);
	fprintf(fp, "file length\t%hu\n", hdr->filename_length);
	fprintf(fp, "extra length\t%hu\n", hdr->extra_length);
}

void
ziplist(const char *filename)
{
	int ch;
	FILE *fp;
	size_t size;
	ZipLocalFileHeader hdr;

	if ((fp = fopen(filename, "rb")) == NULL)
		return;

	printf("%s:\n", filename);

	while (!feof(fp)) {
		/* Get local file header. */
#ifndef LITTLE_ENDIAN_STREAM
		if (fread(&hdr, sizeof (hdr), 1, fp) != 1) {
#else
		if (zipGetLocalFileHeader(fp, &hdr)) {
#endif
			if (ferror(fp))
				printf("header read error\n");
			goto error0;
		}
		if (hdr.signature != ZIP_LOCAL_FILE_HEADER_SIG)
			continue;

		printf(
			"%7u %7u %u-%.2u-%.2u %.2u:%.2u:%.2u ",
			hdr.compressed_size,
			hdr.uncompressed_size,
			MSDOS_DATE_Y(hdr.msdos_date),
			MSDOS_DATE_M(hdr.msdos_date),
			MSDOS_DATE_D(hdr.msdos_date),
			MSDOS_TIME_H(hdr.msdos_time),
			MSDOS_TIME_M(hdr.msdos_time),
			MSDOS_TIME_S(hdr.msdos_time)
		);

		/* Extract file name. */
		for (size = hdr.filename_length; 0 < size; size--) {
			ch = fgetc(fp);
			if (isprint(ch))
				fputc(ch, stdout);
			else
				fputc('?', stdout);
		}

		fputc('\t', stdout);

#ifdef ENABLE_ZIP64_SUPPORT
		if (0 < hdr.extra_length
		&& (hdr.compressed_size == 0xFFFFFFFF || hdr.uncompressed_size == 0xFFFFFFFF)) {
			/* Find ZIP64 extra block. */
			unsigned char *buffer;
			ZipExtraZip64 *zip64;

			if ((buffer = malloc(hdr.extra_length)) == NULL) {
				printf("memory error\n")
				free(buffer);
				goto error0;
			}

			if (fread(buffer, hdr.extra_length, 1, fp) != 1) {
				printf("extra field read error\n");
				free(buffer);
				goto error0;
			}

			zip64 = zipSearchExtraField(
				(ZipExtraBlock *) buffer, hdr.extra_length, ZIP_EXTRA_ZIP64
			);

			if (zip64 != NULL) {
				while (0 < zip64->compressed_size) {
					if (fseek(fp, hdr.extra_length, SEEK_CUR)) {
						printf("seek error\n");
						goto error0;
					}
				}
			}

			free(buffer);
		} else
#endif
		if (fseek(fp, hdr.extra_length, SEEK_CUR)) {
			printf("seek error\n");
			goto error0;
		}

		if (hdr.compressed_size == 0) {
			if (hdr.flags & 0x0008) {
				(void) zipNextData(fp, &hdr.compressed_size, &hdr.uncompressed_size);
				printf("%lu %lu", (unsigned long) hdr.compressed_size, (unsigned long) hdr.uncompressed_size);
			}
			(void) zipNextFile(fp);
		} else if (fseek(fp, hdr.compressed_size, SEEK_CUR)) {
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
		ziplist(argv[i]);
	}

	return 0;
}
