CFLAGS=-Wall -Wno-strict-aliasing -std=gnu11 -g -I. -O2
OBJS=cpp.o debug.o dict.o gen.o lex.o vector.o parse.o buffer.o map.o error.o
TESTS := $(patsubst %.c,%.bin,$(filter-out test/testmain.c,$(wildcard test/*.c)))
override CFLAGS += -DBUILD_DIR='"$(shell pwd)"'

8cc: 8cc.h main.o $(OBJS)
	cc -o $@ main.o $(OBJS) $(LDFLAGS)

$(OBJS) utiltest.o main.o: 8cc.h keyword.h

utiltest: 8cc.h utiltest.o $(OBJS)
	cc -o $@ utiltest.o $(OBJS) $(LDFLAGS)

test/%.o: test/%.c
	$(CC) $(CFLAGS) -w -o $@ -c $<

test/%.bin: test/%.o test/testmain.o
	cc -o $@ $< test/testmain.o $(LDFLAGS)

self: 8cc cleanobj
	$(MAKE) CC=./8cc CFLAGS= 8cc

test: 8cc
	$(MAKE) CC=./8cc CFLAGS= utiltest $(TESTS)
	./utiltest
	./test.sh
	$(MAKE) runtests

runtests:
	@for test in $(TESTS); do  \
	    ./$$test || exit;      \
	done

fulltest:
	$(MAKE) clean
	$(MAKE) $(TESTS)
	$(MAKE) runtests
	$(MAKE) clean

	$(MAKE) test
	cp 8cc stage1
	$(MAKE) self
	$(MAKE) test
	cp 8cc stage2
	$(MAKE) self
	cp 8cc stage3
	cmp stage2 stage3

clean: cleanobj
	rm -f 8cc stage?

cleanobj:
	rm -f *.o *.s test/*.o test/*.bin utiltest

all: 8cc

.PHONY: clean cleanobj test runtests fulltest self all
