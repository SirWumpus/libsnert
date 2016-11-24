#include <err.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

char filemsg[] = "File \"%s\" ";
const char *bits[] = {
	"0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
	"1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111"
};	
	
int
dump(const char *fn)
{
	FILE *fp;
	int octet, i;
	char ascii[7];
	unsigned count;
	
	if (fn == NULL) {
		fp = stdin;
	} else if ((fp = fopen(fn, "rb")) == NULL) {
		warn(filemsg, fn);
		return 1;
	}

	ascii[6] = '\0';
	for (i = count = 0; (octet = fgetc(fp)) != EOF; count++, i++) {
		if (i == 0)
			(void) printf("%08x: ", count);
		ascii[i] = isprint(octet) ? (char)octet : '.';
		(void) printf("%s %s ", bits[(octet & 0xF0) >> 4], bits[octet & 0x0F]);		
		if (i == 5) {
			(void) printf("%s\n", ascii);
			i = -1;
		}
	}
	if (0 < i) {
		ascii[i] = '\0';
		for ( ; i < 6; i++)
			(void) printf("          ");
		(void) printf("%s\n", ascii);
	}
	
	if (fn != NULL)
		(void) fclose(fp);
	
	return 0;		
}
	
int
main(int argc, char **argv)
{
	int ex = EXIT_SUCCESS;
	
        if (0 < argc) {
		while (0 < --argc) {
                        if (dump(*++argv))
                                ex = EXIT_FAILURE;
                }
        } else {
                dump(NULL);
        }

        return ex;
}
