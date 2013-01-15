#CC:=clang
all: perf test

%: %.c /tmp/x
	$(CC) -o $@ -Wall -O2 -march=native -mtune=native -g $< -Wl,--as-needed -lrt -licuuc
