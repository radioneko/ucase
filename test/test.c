#include <stdio.h>
#include <string.h>
#include <unicode/uchar.h>

static unsigned cmp;

unsigned ucase(unsigned c)
{
#	include "/tmp/x"
	return c;
}

int main(int argc, char **argv)
{
	unsigned i, err = 0;
	unsigned st[10];
	memset(st, 0, sizeof(st));
	for (i = 0; i < (unsigned)-1; i++) {
		cmp = 0;
		unsigned my = ucase(i);
		unsigned icu = u_foldCase(i, U_FOLD_CASE_DEFAULT);
		if (my != icu) {
			printf("Error in symbol U+%04X:\n"
					"  my:  U+%04X\n"
					"  icu: U+%04X\n", i, my, icu);
			err++;
		}
	}
	if (err)
		printf("Total %u errors detected\n", err);
	else
		printf("Case conversion is correct\n");
/*	for (i = 1; i < 10; i++)
		fprintf(stderr, "%u - %u\n", i, st[i]);*/
	return 0;
}
