all: libasock.a

async_socket.o: async_socket.h async_socket.c
	gcc -c async_socket.h async_socket.c -lpthread
	rm *.gch

libasock.a: async_socket.o
	ar rcs libasock.a async_socket.o
