CFLAGS=-Wall -std=gnu99 -g
OBJS=lex.o string.o util.o

$(OBJS) unittest.o main.o: 8cc.h

8cc: 8cc.h main.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ main.o $(OBJS)

unittest: 8cc.h unittest.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ unittest.o $(OBJS)

test: unittest
	./unittest
	./test.sh

clean:
	rm -f 8cc *.o tmp.*
