CFLAGS=-Wall -std=gnu99 -g -I.
OBJS=cpp.o debug.o dict.o gen.o lex.o list.o parse.o string.o util.o
SELF=cpp.s debug.s dict.s gen.s lex.s list.s parse.s string.s util.s main.s
TESTS := $(patsubst %.c,%.bin,$(wildcard test/*.c))

8cc: 8cc.h main.o $(OBJS)
	$(CC) -o $@ main.o $(OBJS)

$(OBJS) utiltest.o main.o: 8cc.h

utiltest: 8cc.h utiltest.o $(OBJS)
	$(CC) -o $@ utiltest.o $(OBJS)

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
	$(CC) -o $@ $< test/util/testmain.o

$(SELF): 8cc
	./8cc < $(@:s=c) > $@
	$(CC) -c $@

next: $(SELF)
	rm -f 8cc utiltest
	$(MAKE) 8cc

fulltest:
	$(MAKE) clean
	$(MAKE) test
	cp 8cc gen1
	rm $(OBJS) main.o
	$(MAKE) next
	$(MAKE) test
	cp 8cc gen2
	rm $(OBJS) main.o
	$(MAKE) next
	$(MAKE) test
	cp 8cc gen3
	diff gen2 gen3

sample/nqueen: 8cc sample/nqueen.c
	./8cc < sample/nqueen.c > sample/nqueen.s
	$(CC) -o sample/nqueen sample/nqueen.s

clean:
	rm -f 8cc *.o *.s tmp.* test/*.s test/*.o sample/*.o
	rm -f utiltest sample/nqueen.s sample/nqueen gen[1-9]
	rm -f test/util/testmain.o
	rm -f $(TESTS)

all: 8cc

.PHONY: clean test all
