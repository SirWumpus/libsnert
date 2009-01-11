/*
 * taglengths.c
 *
 * Count number and length of HTML tags.
 *
 * Copyright 2008 by Anthony Howe. All rights reserved.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct html_tag {
	struct html_tag *next;
	size_t frequency;
	size_t max_length;
	size_t sum_length;
	char *word;
} HtmlTag;

void
tagListFree(HtmlTag *list)
{
	HtmlTag *next;

	for ( ; list != NULL; list = next) {
		next = list->next;
		free(list->word);
		free(list);
	}
}

HtmlTag *
tagListFind(HtmlTag *list, const char *word)
{
	for ( ; list != NULL; list = list->next) {
		if (strcasecmp(list->word, word) == 0)
			break;
	}

	return list;
}

void
tagListPush(HtmlTag **head, HtmlTag *entry)
{
	entry->next = *head;
	*head = entry;
}

void
htmltags(const char *filename)
{
	FILE *fp;
	char word[256];
	HtmlTag *head, *tag;
	int ch, index, is_comment;
	size_t length, line_no, word_lineno, byte_size, nontag_size, tag_size;

	if ((fp = fopen(filename, "rb")) == NULL)
		return;

	index = 0;
	length = 0;
	head = NULL;
	line_no = 1;
	is_comment = 0;
	byte_size = 0;
	tag_size = 0;
	nontag_size = 0;


	printf("Filename\n");
	printf("-------------------------------\n");
	printf("%s\n\n", filename);

	printf(" Line   Len Tag\n");
	printf("-------------------------------\n");

	while ((ch = fgetc(fp)) != EOF) {
		byte_size++;

		if (ch == '\n')
			line_no++;

		if (length == 0 && ch == '<') {
			if ((ch = fgetc(fp)) == EOF)
				break;

			byte_size++;

			if (isspace(ch))
				continue;

			length++;
			word[index++] = ch;
			word_lineno = line_no;
		} else if (is_comment == 1) {
			if (ch == '-')
				is_comment++;
			length++;
		} else if (is_comment == 2) {
			if (ch == '-')
				is_comment++;
			else
				is_comment = 1;
			length++;
		} else if (ch == '>') {
			HtmlTag *tag;

			if (0 < is_comment && is_comment != 3) {
				is_comment = 1;
				length++;
				continue;
			}

			if (0 < index) {
				word[index] = '\0';
				index = 0;
			}

			printf("%5lu %5lu %s\n", (unsigned long) word_lineno, (unsigned long) length, word);
			fflush(stdout);

			if ((tag = tagListFind(head, word)) != NULL) {
				tag->frequency++;
				tag->sum_length += length;
				if (tag->max_length < length)
					tag->max_length = length;
				tag_size += length + 2;
				length = 0;
				continue;
			}

			if ((tag = malloc(sizeof (*tag))) == NULL)
				break;

			if ((tag->word = strdup(word)) == NULL) {
				free(tag);
				break;
			}

			tag->frequency = 1;
			tag->sum_length = length;
			tag->max_length = length;

			tagListPush(&head, tag);
			tag_size += length + 2;
			length = 0;
		} else if (0 < index && index < sizeof (word)-1) {
			length++;

			if (index == 3 && word[0] == '!' && word[1] == '-' && word[2] == '-') {
				word[index] = '\0';
				index = 0;
				is_comment = 1;
				continue;
			}

			if (isspace(ch)) {
				word[index] = '\0';
				index = 0;
				continue;
			}

			word[index++] = ch;
		} else if (0 < length) {
			length++;
		} else {
			nontag_size++;
		}
	}

	printf("-------------------------------\n");
	printf("%5lu       total lines\n", (unsigned long) line_no);
	printf("%5lu       total bytes\n", (unsigned long) byte_size);
	printf("%5lu       tag bytes\n", (unsigned long) tag_size);
	printf("%5lu       non-tag bytes\n\n", (unsigned long) nontag_size);

	printf(" Freq   Sum   Avg   Max Tag\n");
	printf("-------------------------------\n");

	for (tag = head; tag != NULL; tag = tag->next) {
		printf("%5lu %5lu %5lu %5lu %s\n", (unsigned long) tag->frequency, (unsigned long) tag->sum_length, tag->sum_length / tag->frequency, tag->max_length, tag->word);
	}

	tagListFree(head);
	fclose(fp);
}


static const char usage[] =
"usage: htmltags file1 file2 ...\n"
;

int
main(int argc, char **argv)
{
	int i;

	if (argc <= 1) {
		fprintf(stderr, usage);
		return 1;
	}

	for (i = 1; i < argc; i++) {
		htmltags(argv[i]);
	}

	return 0;
}
