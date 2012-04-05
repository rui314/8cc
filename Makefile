CFLAGS=-Wall -std=gnu99 -g -I.
OBJS=cpp.o debug.o dict.o gen.o lex.o list.o parse.o string.o util.o
SELF=debug.s dict.s gen.s lex.s list.s main.s string.s util.s
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

test/%.bin: test/%.s test/util/testmain.o 8cc
	@$(CC) $(CFLAGS) -o $@ $< test/util/testmain.o

$(SELF): 8cc
	./8cc < $(@:s=c) > $@
	gcc -c $@

gen2: $(SELF)
	rm -f 8cc utiltest
	$(MAKE) 8cc

gen2test: gen2
	$(MAKE) test

sample/nqueen: 8cc sample/nqueen.c
	./8cc < sample/nqueen.c > sample/nqueen.s
	$(CC) $(CFLAGS) -o sample/nqueen sample/nqueen.s

clean:
	rm -f 8cc *.o *.s tmp.* test/*.s test/*.o sample/*.o
	rm -f utiltest sample/nqueen.s sample/nqueen gen1 gen2
	rm -f $(TESTS)

all: 8cc

.PHONY: clean test all
