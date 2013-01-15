all: perf test

%: %.c
	$(CC) -o $@ -Wall -O2 -march=native -mtune=native -g $< -Wl,--as-needed -lrt -licuuc
