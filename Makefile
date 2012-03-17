CFLAGS=-Wall -std=gnu99 -g -I.
OBJS=lex.o string.o util.o gen.o parse.o list.o debug.o
TESTS := $(patsubst %.c,%,$(wildcard test/*.c))

8cc: 8cc.h main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ main.o $(OBJS)

$(OBJS) unittest.o main.o: 8cc.h

unittest: 8cc.h unittest.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ unittest.o $(OBJS)

test: nqueen unittest $(TESTS)
	@for test in $(TESTS); do \
	    ./$$test;             \
	done
	./unittest
	./test.sh

test/%.s: test/%.c 8cc
	./8cc < $< > $@

test/%: test/%.s 8cc
	$(CC) $(CFLAGS) -o $@ $<

nqueen: 8cc sample/nqueen.c
	./8cc < sample/nqueen.c > sample/nqueen.s
	$(CC) $(CFLAGS) -o sample/nqueen sample/nqueen.s

.PHONY: clean test
clean:
	rm -f 8cc *.o tmp.* test/*.s test/*.o sample/*.o unittest sample/nqueen.s sample/nqueen 
	rm -f $(TESTS)
