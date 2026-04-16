#TODO: get header info from the compiler

#all: TODO

string_handling.o: string_handling.h string_handling.c
	$(CC) $(CFLAGS) -c string_handling.c -o $@ $(LDFLAGS)

hashmap.o: hashmap.h hashmap.c
	$(CC) $(CFLAGS) -c hashmap.c -o $@ $(LDFLAGS)

test_hashmap: hashmap.h hashmap.o string_handling.o test_hashmap.c
	$(CC) $(CFLAGS) -o $@ string_handling.o hashmap.o test_hashmap.c $(LDFLAGS)

.PHONY=run_test_hashmap
run_test_hashmap: test_hashmap
	./test_hashmap

.PHONY=test
test: run_test_hashmap

.PHONY=clean
clean: rm -f hashmap.o test_hashmap
