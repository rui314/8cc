CFLAGS=-Wall -std=gnu99 -g
OBJS=lex.o string.o util.o gen.o parse.o list.o

8cc: 8cc.h main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ main.o $(OBJS)

$(OBJS) unittest.o main.o: 8cc.h

unittest: 8cc.h unittest.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ unittest.o $(OBJS)

test: unittest nqueen
	./unittest
	./test.sh

clean:
	rm -f 8cc *.o tmp.* unittest nqueen.s nqueen

nqueen: 8cc sample/nqueen.c
	./8cc < sample/nqueen.c > nqueen.s
	gcc -o nqueen nqueen.s
