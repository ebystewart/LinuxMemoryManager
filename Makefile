CC=gcc
CFLAGS=-g
LIBS=
OBJS= mm.o

LinuxMemoryManager.bin:${OBJS}
	${CC} ${CFLAGS} ${OBJS} -o LinuxMemoryManager.bin

AppManager.o:AppManager.c
	${CC} ${CFLAGS} -c mm.c -I . -o Amm.o


all:
	make

clean:
	rm *.o
	rm *.bin