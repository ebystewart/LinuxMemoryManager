CC=gcc
CFLAGS=-g
LIBS=
OBJS= mm.o	\
	test.o	\
	glthread.o

LinuxMemoryManager.bin:${OBJS}
	${CC} ${CFLAGS} ${OBJS} -o LinuxMemoryManager.bin

mm.o:mm.c
	${CC} ${CFLAGS} -c mm.c -I . -o mm.o

test.o:test.c
	${CC} ${CFLAGS} -c test.c -I . -o test.o

glthread.o:glueThread/glthread.c
	${CC} ${CFLAGS} -c glueThread/glthread.c -I . -o glthread.o


all:
	make

clean:
	rm *.o
	rm *.bin