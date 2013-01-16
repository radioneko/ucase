#include <stdio.h>
#include <string.h>
#include <unicode/uchar.h>
#include <ctype.h>

static unsigned cmp;

unsigned ucase(unsigned c)
{
#	include "/tmp/x"
	return c;
}

#if 0
unsigned utf8_casefold_str(const char *in, unsigned len, char *out, unsigned out_size)
{
	const unsigned char *src = (const unsigned char *in);
	unsigned char *dst = (unsigned char*)out;
	while (len) {
		unsigned ic, oc;
		if (src[0] < 0x80) {
			*dst = tolower(*src);
			dst++;
			src++;
			len--;
			continue;
		} if ((src[0] & 0xE0) == 0xC0) {
			ic = ((src[0] & 0x1F) << 6) | (src[1] & 0x3F);
#include "/tmp/u_0080_07FF.h"
			len -= 2;
			src += 2;
		} else if ((src[0] & 0xF0) == 0xE0) {
			ic = ((src[0] & 0x0F) << 12) | ((src[1] & 0x3F) << 6) | (src[2] & 0x3F);
#include "/tmp/u_0800_FFFF.h"
			src += 3;
			len -= 3;
		} else /*if ((src[0] & 0xF8) == 0xF0)*/ {
			ic = ((src[0] & 0x0F) << 18) | ((src[1] & 0x3F) << 12) | ((src[2] & 0x3F) << 6) | (src[3] & 0x3F);
#include "/tmp/u_10000_1FFFFF.h"
		}
		if (oc <= 0x7FF) {
			dst[0] = 0xC0 | (oc >> 6);
			dst[1] = 0x80 | (oc & 0x3F);
			dst += 2;
		} else if (oc <= 0xFFFF) {
			dst[0] = 0xE0 | (oc >> 12);
			dst[1] = 0x80 | ((oc >> 6) & 0x3F);
			dst[2] = 0x80 | (oc & 0x3F);
			dst += 3;
		} else /*if (oc <= 0x1FFFFF)*/ {
			dst[0] = 0xF0 | (oc >> 18);
			dst[1] = 0x80 | ((oc >> 12) & 0x3F);
			dst[2] = 0x80 | ((oc >> 6) & 0x3F);
			dst[3] = 0x80 | (oc & 0x3F);
			dst += 4;
		}
	}
	return (char*)dst - out;
}
#endif

unsigned utf8_casefold_char(const char *in)
{
	const unsigned char *src = (const unsigned char *)in;
	unsigned ic, oc;
	if (src[0] < 0x80) {
		ic = src[0];
#include "/tmp/u_0000_007F.h"
	} else if ((src[0] & 0xE0) == 0xC0) {
		ic = ((src[0] & 0x1F) << 6) | (src[1] & 0x3F);
#include "/tmp/u_0080_07FF.h"
	} else if ((src[0] & 0xF0) == 0xE0) {
		ic = ((src[0] & 0x0F) << 12) | ((src[1] & 0x3F) << 6) | (src[2] & 0x3F);
#include "/tmp/u_0800_FFFF.h"
	} else /*if ((src[0] & 0xF8) == 0xF0)*/ {
		ic = ((src[0] & 0x0F) << 18) | ((src[1] & 0x3F) << 12) | ((src[2] & 0x3F) << 6) | (src[3] & 0x3F);
#include "/tmp/u_10000_1FFFFF.h"
	}
	return oc;
}

static void
u8_enc(char *out, unsigned oc)
{
	unsigned char *dst = (unsigned char*)out;
	if (oc <= 0x7F) {
		dst[0] = oc;
	} else if (oc <= 0x7FF) {
		dst[0] = 0xC0 | (oc >> 6);
		dst[1] = 0x80 | (oc & 0x3F);
	} else if (oc <= 0xFFFF) {
		dst[0] = 0xE0 | (oc >> 12);
		dst[1] = 0x80 | ((oc >> 6) & 0x3F);
		dst[2] = 0x80 | (oc & 0x3F);
	} else /*if (oc <= 0x1FFFFF)*/ {
		dst[0] = 0xF0 | (oc >> 18);
		dst[1] = 0x80 | ((oc >> 12) & 0x3F);
		dst[2] = 0x80 | ((oc >> 6) & 0x3F);
		dst[3] = 0x80 | (oc & 0x3F);
	}
}

int main(int argc, char **argv)
{
	unsigned i, err = 0;
	unsigned st[10];
	memset(st, 0, sizeof(st));
	for (i = 0; i < 0x1FFFFF; i++) {
#if 1
		cmp = 0;
		char u8[4];
		unsigned my = ucase(i);
		unsigned icu = u_foldCase(i, U_FOLD_CASE_DEFAULT);
		unsigned u8f;
		u8_enc(u8, i);
		u8f = utf8_casefold_char(u8);
		if (my != icu) {
			printf("Error in symbol U+%04X:\n"
					"  my:  U+%04X\n"
					"  icu: U+%04X\n", i, my, icu);
			err++;
		}
		if (u8f != icu) {
			printf("Error in UTF-8 symbol U+%04X:\n"
					"  my:  U+%04X\n"
					"  icu: U+%04X\n", i, u8f, icu);
		}
#else
//		err += u_foldCase(i, U_FOLD_CASE_DEFAULT); //ucase(i);
		err += ucase(i);
#endif
	}
	if (err)
		printf("Total %u errors detected\n", err);
	else
		printf("Case conversion is correct\n");
/*	for (i = 1; i < 10; i++)
		fprintf(stderr, "%u - %u\n", i, st[i]);*/
	return 0;
}
