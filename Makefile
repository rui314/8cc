CFLAGS=-Wall -std=gnu99 -g -I. -O0 -DBUILD_DIR='"$(shell pwd)"'
OBJS=cpp.o debug.o dict.o gen.o lex.o vector.o parse.o buffer.o map.o error.o
SELF=cpp.s debug.s dict.s gen.s lex.s vector.s parse.s buffer.s map.s error.s main.s
TESTS := $(patsubst %.c,%.bin,$(wildcard test/*.c))
ECC_CFLAGS=-DBUILD_DIR='"$(shell pwd)"'

8cc: 8cc.h main.o $(OBJS)
	$(CC) -o $@ main.o $(OBJS) $(LDFLAGS)

$(OBJS) utiltest.o main.o: 8cc.h keyword.h

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
	./8cc $(ECC_CFLAGS) -c $<

test/%.bin: test/%.o test/main/testmain.s 8cc
	$(CC) -o $@ $< test/main/testmain.o $(LDFLAGS)

$(SELF) test/main/testmain.s: 8cc test/main/testmain.c
	./8cc $(ECC_CFLAGS) -c $(@:s=c)

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
	rm -f 8cc *.o *.s tmp.* test/*.s test/*.o test/main/*.o sample/*.o
	rm -f utiltest gen[1-9] test/util/testmain.[os]
	rm -f $(TESTS)

all: 8cc

.PHONY: clean test all
