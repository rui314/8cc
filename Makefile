CFLAGS=-Wall -std=gnu99 -g -I.
OBJS=lex.o string.o util.o gen.o parse.o list.o debug.o dict.o cpp.o
TESTS := $(patsubst %.c,%.bin,$(wildcard test/*.c))

8cc: 8cc.h main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ main.o $(OBJS)

$(OBJS) utiltest.o main.o: 8cc.h

utiltest: 8cc.h utiltest.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ utiltest.o $(OBJS)

test: utiltest $(TESTS) sample/nqueen
	@echo
	./utiltest
	@for test in $(TESTS); do \
	    ./$$test;             \
	done
	./test.sh

test/%.s: test/%.c 8cc
	./8cc < $< > $@

test/%.bin: test/%.s test/util/util.o 8cc
	@$(CC) $(CFLAGS) -o $@ $< test/util/util.o

sample/nqueen: 8cc sample/nqueen.c
	./8cc < sample/nqueen.c > sample/nqueen.s
	$(CC) $(CFLAGS) -o sample/nqueen sample/nqueen.s

.PHONY: clean test
clean:
	rm -f 8cc *.o tmp.* test/*.s test/*.o sample/*.o utiltest sample/nqueen.s sample/nqueen
	rm -f $(TESTS)
