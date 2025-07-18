CC=gcc
CFLAGS=-g
LIBS=
OBJS= mm.o	\
	test.o

LinuxMemoryManager.bin:${OBJS}
	${CC} ${CFLAGS} ${OBJS} -o LinuxMemoryManager.bin

mm.o:mm.c
	${CC} ${CFLAGS} -c mm.c -I . -o mm.o

test.o:test.c
	${CC} ${CFLAGS} -c test.c -I . -o test.o


all:
	make

clean:
	rm *.o
	rm *.bin