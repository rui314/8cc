CFLAGS=-Wall -std=gnu99 -g
OBJS=main.o lex.o string.o

8cc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

clean:
	rm -f 8cc *.o tmp.*
