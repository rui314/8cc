CFLAGS=-Wall -std=gnu99 -g -I. -O0
OBJS=cpp.o debug.o dict.o gen.o lex.o list.o parse.o string.o error.o
SELF=cpp.s debug.s dict.s gen.s lex.s list.s parse.s string.s error.s main.s
TESTS := $(patsubst %.c,%.bin,$(wildcard test/*.c))

8cc: 8cc.h main.o $(OBJS)
	$(CC) -o $@ main.o $(OBJS) $(LDFLAGS)

$(OBJS) utiltest.o main.o: 8cc.h

utiltest: 8cc.h utiltest.o $(OBJS)
	$(CC) -o $@ utiltest.o $(OBJS) $(LDFLAGS)

test: utiltest $(TESTS)
	@echo
	./utiltest
	@for test in $(TESTS); do  \
	    ./$$test || exit;      \
	done
	./test.sh

test/%.o: test/%.c 8cc
	./8cc -c $<

test/%.bin: test/%.o test/main/testmain.s 8cc
	$(CC) -o $@ $< test/main/testmain.o $(LDFLAGS)

$(SELF) test/main/testmain.s: 8cc test/main/testmain.c
	./8cc -c $(@:s=c)

self: $(SELF)
	rm -f 8cc utiltest
	$(MAKE) 8cc

fulltest:
	$(MAKE) clean
	$(MAKE) test
	cp 8cc gen1
	rm $(OBJS) main.o
	$(MAKE) self
	$(MAKE) test
	cp 8cc gen2
	rm $(OBJS) main.o
	$(MAKE) self
	$(MAKE) test
	cp 8cc gen3
	diff gen2 gen3

clean:
	rm -f 8cc *.o *.s tmp.* test/*.s test/*.o sample/*.o
	rm -f utiltest gen[1-9] test/util/testmain.[os]
	rm -f $(TESTS)

all: 8cc

.PHONY: clean test all
