#TODO: get header info from the compiler

#all: TODO
CFLAGS+=-std=c23 -static -lc -Wall -Wextra -Wno-unused-parameter -Wno-unused-function#for now, delete when state completion

hashmap.o: hashmap.h hashmap.c
	$(CC) $(CFLAGS) -c hashmap.c -o $@ $(LDFLAGS)

nic: nic.c
	$(CC) $(CFLAGS) nic.c -o $@ $(LDFLAGS)

.PHONY=clean
clean: rm -f preprocessor
