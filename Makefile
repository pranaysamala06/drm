
FLAGS=`pkg-config cairo --cflags --libs libdrm`
FLAGS+=-Wall -O0 -g -lncurses
FLAGS+=-D_FILE_OFFSET_BITS=64

all:
	gcc -pthread -o run pr1.c $(FLAGS)

