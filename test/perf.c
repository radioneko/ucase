#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <unicode/uchar.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

/** Returns difference between stop and start in microseconds */
unsigned
clock_diff(const struct timespec *start, const struct timespec *stop)
{
	unsigned ms;
	if (stop->tv_nsec < start->tv_nsec) {
		ms = (stop->tv_sec - start->tv_sec - 1) * 1000000 + (1000000000 + stop->tv_nsec - start->tv_nsec) / 1000;
	} else {
		ms = (stop->tv_sec - start->tv_sec) * 1000000 + (stop->tv_nsec - start->tv_nsec) / 1000;
	}
	return ms;
}

void*
read_file(const char *fname, unsigned *len)
{
	void *out;
	struct stat st;
	if (stat(fname, &st) != 0)
		return NULL;
	int fd = open(fname, O_RDONLY);
	if (fd == -1)
		return NULL;
	out = malloc(st.st_size);
	read(fd, out, st.st_size);
	close(fd);
	*len = st.st_size;
	return out;
}

void
write_file(const char *fname, const void *data, unsigned len)
{
	int fd = open(fname, O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if (fd != -1) {
		write(fd, data, len);
		close(fd);
	}
}

unsigned my_fold(unsigned c)
{
	if (c < 'A') return c;
#	include "/tmp/x"
}

void*
fold_u16_my(const void *p, unsigned len)
{
	unsigned i;
	const uint16_t *in = (const uint16_t*)p;
	uint16_t *out = (uint16_t*)malloc(len);
	len >>= 1;
	for (i = 0; i < len; i++) {
		out[i] = my_fold(in[i]);
	}
	return out;
}

void*
fold_u16_icu(const void *p, unsigned len)
{
	unsigned i;
	const uint16_t *in = (const uint16_t*)p;
	uint16_t *out = (uint16_t*)malloc(len);
	len >>= 1;
	for (i = 0; i < len; i++) {
		out[i] = u_foldCase(in[i], U_FOLD_CASE_DEFAULT);
	}
	return out;
}

int main(int argc, char **argv)
{
	struct timespec t[2];
	unsigned ms;
	unsigned len = 0;
	void *in = read_file("in.dat", &len), *out[2];

	if (!len) {
		printf("Can't open in.dat\n");
		return 1;
	}

	{
		unsigned i;
		uint8_t s = 0, *p = (uint8_t*)in;
		for (i = 0; i < len; i++)
			s += p[i];
		printf("Warm up: %u, len = %u\n", s, len);
	}

	clock_gettime(CLOCK_MONOTONIC, t);
	out[0] = fold_u16_my(in, len);
	clock_gettime(CLOCK_MONOTONIC, t + 1);
	ms = clock_diff(t, t + 1);
	printf("my_fold  took %u.%06u\n", ms / 1000000, ms % 1000000);

	clock_gettime(CLOCK_MONOTONIC, t);
	out[1] = fold_u16_icu(in, len);
	clock_gettime(CLOCK_MONOTONIC, t + 1);
	ms = clock_diff(t, t + 1);
	printf("icu_fold took %u.%06u\n", ms / 1000000, ms % 1000000);

	if (memcmp(out[0], out[1], len) == 0) {
		printf("Results are the same\n");
	} else {
		printf("Results mismatch\n");
		write_file("out.icu", out[1], len);
		write_file("out.my", out[0], len);
	}
	return 0;
}
