#ifndef ASYNC_SOCKET_H
#define ASYNC_SOCKET_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string.h>

#define MAX_EVENTS 25
#define ACCEPT 0
#define CONNECT 1
#define SHUTDOWN 2
#define RECV 3
#define SEND 4

/* Defining function types in order to cast functions from generic pointer to the function I want */
typedef int (*accept_func)(int, struct sockaddr *, socklen_t  *);
typedef int (*close_func)(int);
typedef int (*shutdown_func)(int, int);
typedef ssize_t (*recv_func)(int, void *, size_t, int);
typedef ssize_t (*send_func)(int, const void *, size_t, int);

/* Generic type for casting */
typedef void (*generic_func)(void);

typedef struct async_socket_t
{
	/* Function callback pointers */
	void (*AcceptCallback)(struct async_socket_t *);
	void (*ReceiveCallback)(struct async_socket_t *);
	void (*SendCallback)(struct async_socket_t *);
	void (*ConnectCallback)(struct async_socket_t *);

	void * buffer;
	int buffer_size;
	int data_length;
	int offset;
	int socket_fd;
	struct sockaddr * address;
	int address_length;
	int shutdown_how;
	int recv_flags;
	int send_flags;

} async_socket_t;

typedef struct async_socket_event_t
{
	/* Generic function*/
	void (*socket_func)(void);
	int function;
	async_socket_t * socket;

} async_socket_event_t;

int as_init_socket(char *, int, int, struct sockaddr *);
async_socket_t * as_init_async_socket(int, void *, int);
int async_sock_listen(async_socket_t*, int);
int async_sock_bind(async_socket_t *);
int as_accept_async(async_socket_t*);
int as_connect_async(async_socket_t*);
int as_receive_async(async_socket_t*);
int as_send_async(async_socket_t*);
int async_sock_shutdown(async_socket_t*, int);
int async_sock_close(async_socket_t*);

#endif
