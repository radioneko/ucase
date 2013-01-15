CFLAGS=-Wall -g -fno-inline -O0

cf: cf.o avl.o
	$(CXX) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CFLAGS) -c -o $@ $<

g: cf
	gdb ./cf
