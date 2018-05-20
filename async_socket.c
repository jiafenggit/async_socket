#include "async_socket.h"

int event_loop_started = 0;
struct epoll_event events[MAX_EVENTS];
int numfds, epollfd;
pthread_t event_loop_thread;

void * event_loop(void * args)
{
	async_socket_event_t * e = (async_socket_event_t *) args;
	int numfds, i, client_sock_fd, bytes_recv, bytes_sent = 0, total_length_to_send, bytes_remaining, total_length_to_recv;
	struct epoll_event client_ev;
	async_socket_t * client_socket;

	numfds = epoll_wait(epollfd, events, MAX_EVENTS, -1); // blocks
	
	if(numfds > 0){ // There is an event
		i = 0;
		for(i=0; i < MAX_EVENTS; i++){
			
			if(events[i].data.fd == e->socket->socket_fd){
				
				switch(e->function){

					case ACCEPT:
						// Get client_fd
						client_sock_fd = ((accept_func)e->socket_func)(e->socket->socket_fd,
							e->socket->address, (socklen_t *)&e->socket->address_length);
						if(client_sock_fd > 0){
							
							client_socket = (async_socket_t *) malloc(sizeof(async_socket_t));
							client_socket->socket_fd = client_sock_fd;

							fcntl(client_socket->socket_fd, F_SETFL, O_NONBLOCK); // set non blocking
							// realloc client 
							if(e->socket->AcceptCallback != NULL)
								e->socket->AcceptCallback(client_socket); // Call callback
						}
						else
							perror("event_loop:ACCEPT");
						break;
					case CONNECT:
						if(e->socket->ConnectCallback != NULL)
							e->socket->ConnectCallback(e->socket);
						break;
					case RECV:
						bytes_remaining = 0;
						total_length_to_recv = e->socket->buffer_size;
						while(bytes_remaining < e->socket->buffer_size){
							bytes_recv = recvfrom(e->socket->socket_fd, e->socket->buffer+bytes_remaining,
							total_length_to_recv, e->socket->recv_flags, NULL, 0);
							if(bytes_recv == -1){
								perror("error_loop:RECV1");
								break;
							}
							else if(bytes_recv == 0){
								perror("recv peer disconected");
								break;
							}
							bytes_remaining += bytes_recv;
							total_length_to_recv -= bytes_recv;
						}
						/* If the operation will not block */
						if((errno != EAGAIN || errno != EWOULDBLOCK) && bytes_recv > 0){
							if(e->socket->ReceiveCallback != NULL)
								e->socket->ReceiveCallback(e->socket); // Call callback with data
						}
						else
							perror("event_loop:RECV2");
							
						break;

					case SEND:
						bytes_remaining = 0;
						total_length_to_send = e->socket->buffer_size;
						
						while(bytes_remaining < e->socket->buffer_size){
							
							bytes_sent = sendto(e->socket->socket_fd, e->socket->buffer+bytes_remaining,
								total_length_to_send, e->socket->send_flags, NULL, 0);
								
							if(bytes_sent == -1){
								perror("event_loop:SEND");
								break;
							}
							
							bytes_remaining += bytes_sent;
							total_length_to_send -= bytes_sent;
						}
						
						if((errno != EAGAIN || errno != EWOULDBLOCK) && bytes_sent > 0){
							if(e->socket->SendCallback != NULL)
								e->socket->SendCallback(e->socket); // Call callback with data
						}
						
						break;

					default:
						break;

				}
				break; /* Event is done GTFO out of loop */				
			}
		}
	}

	pthread_exit(NULL);

}

int init_event_loop()
{
	if(event_loop_started == 0){
		epollfd = epoll_create(MAX_EVENTS);
		event_loop_started = 1;
	}
	return 0;
}

/* Params: socket_fd, buffer, buffer_size */
int add_to_event_loop(async_socket_event_t * event, uint32_t events)
{
	struct epoll_event ev;
	ev.events = events; /* Because we are watching for input EPOLLIN, EPOLLOUT. See man epoll_ctl(2) for more events */
	ev.data.fd = event->socket->socket_fd; /* The file descriptor to monitor for input */

	/* Attempt to register the socket to the epollfd and asociate the event ev to socket */
	if(epoll_ctl(epollfd, EPOLL_CTL_ADD, event->socket->socket_fd, &ev) < 0 && errno != EEXIST ){
		perror("Error adding socket to epoll");
		return -1;
	}
	else if(errno == EEXIST){ /* If it exists, mod it with new events to watch for. */
		if(epoll_ctl(epollfd, EPOLL_CTL_MOD, event->socket->socket_fd, &ev) < 0){
			perror("Error modding socket in epoll");
			return -1;
		}
	}

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr,  PTHREAD_CREATE_DETACHED);

	// Start thread for listening for that socket in a detached state.
	// That is when the thread ends all resources will be released.
	pthread_create(&event_loop_thread, &attr, event_loop, (void *)event);

	return 0;

}

int remove_from_event_loop(async_socket_t * socket)
{
	/* To maintain compat with < linux 2.6.9. You have to send empty event ptr */
	struct epoll_event ev;

	if(epoll_ctl(epollfd, EPOLL_CTL_DEL, socket->socket_fd, &ev) < 0){
		perror("Error removing socket from epoll");
		return -1;
	}
	return 0;
}

int check_valid_fd(int fd)
{
	return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

int as_init_socket(char * ip_addr_str, int port, int af, struct sockaddr * addr)
{
	// socket
	int socketfd, optval = 1, pton_result, working_ip_dyn = 0;
	char * working_ip_str;
	struct in_addr * dest_ipv4;
	struct in6_addr * dest_ipv6;

	if((socketfd = socket(af, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0){
		perror("Error creating socket");
		return -1;
	}
	// set sock opts

	if(setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){
		perror("Error setting server options");
		return -1;
	}
	
	if(strcmp(ip_addr_str, "localhost") == 0){
		working_ip_str = (char *)malloc(sizeof(char) * INET_ADDRSTRLEN);
		working_ip_dyn = 1;
		strcpy(working_ip_str, "0.0.0.0");
	}
	else
		working_ip_str = ip_addr_str;
	

	if(af == AF_INET){

		dest_ipv4 = (struct in_addr *)malloc(sizeof(struct in_addr *));
		pton_result = inet_pton(af, working_ip_str, dest_ipv4);

		if(pton_result == 1){

			struct sockaddr_in * temp = (struct sockaddr_in *)addr;
			temp->sin_family = AF_INET;
			temp->sin_port = htons(port);
			temp->sin_addr = *dest_ipv4;
			
		}
		else if(pton_result == 0){
			perror("IPv4 address is invalid");
			return -1;
		}
		else{
			perror("Cant convert ipv4 address");
			return -1;
		}
	
	}
	else if(af == AF_INET6){ // IPv6
		dest_ipv6 = (struct in6_addr *)malloc(sizeof(struct in6_addr *));
		pton_result = inet_pton(af, working_ip_str, dest_ipv6);
		if(pton_result == 1){
		
			struct sockaddr_in6 * temp = (struct sockaddr_in6 *)addr;
			temp->sin6_family = AF_INET6;
			temp->sin6_port = htons(port);
			temp->sin6_addr = *dest_ipv6;
		
		}
		else if(pton_result == 0){
			perror("IPv6 address is invalid");
			return -1;
		}
		else{
			perror("Cant convert ipv6 address");
			return -1;
		}
	}
	else{
		perror("Invalid af value");
		return -1;
	}
	
	if(working_ip_dyn == 1)
		free(working_ip_str); 
	
	return socketfd;

}

async_socket_t * as_init_async_socket(int socket_fd, void * buffer, int buffer_size)
{
	async_socket_t * socket = (async_socket_t *)malloc(sizeof(async_socket_t));
	socket->socket_fd = socket_fd;
	socket->buffer = buffer;
	socket->offset = 0;
	socket->data_length = 0;
	socket->buffer_size = buffer_size;
	socket->AcceptCallback = NULL;
	socket->ReceiveCallback = NULL;
	socket->SendCallback = NULL;
	socket->ConnectCallback = NULL;

	return socket;
}

int async_sock_listen(async_socket_t * socket, int backlog)
{
	if(listen(socket->socket_fd, backlog) < 0){
		perror("Could not listen on socket");
		return -1;
	}
	return 0;
}

int async_sock_bind(async_socket_t * socket)
{
	int result;
	if(socket != NULL){
		result = bind(socket->socket_fd, socket->address, (socklen_t)sizeof(struct sockaddr));
		if(result < 0){
			perror("Error binding socket to port");
			return -1;
		}
	}
	else
		return -1;
	
	return result;
}

int as_accept_async(async_socket_t * socket)
{
	if(socket == NULL){
		errno = EINVAL;
		return -1;
	}
	
	init_event_loop();
	/* All sockets must be added to event loop beforehand */

	// Wrap socket in async_event
	async_socket_event_t * e = (async_socket_event_t *) malloc(sizeof(async_socket_event_t));
	if(e == NULL){ return -1; }
	e->socket = socket;
	e->function = ACCEPT;
	e->socket_func = (generic_func)&accept; // send sys/socket.h function to func pointer to call when client comes in

	return add_to_event_loop(e, EPOLLIN);
}

int as_connect_async(async_socket_t * socket)
{
	if(socket == NULL){
		errno = EINVAL;
		return -1;
	}
	
	init_event_loop();
	
	int conn_result;
	conn_result = connect(socket->socket_fd, socket->address, (socklen_t)socket->address_length);

	if(conn_result < 0 && errno == EINPROGRESS){
	
		async_socket_event_t * e = (async_socket_event_t *) malloc(sizeof(async_socket_event_t));
		if(e == NULL){ return -1; }
		e->socket = socket;
		e->function = CONNECT;
		e->socket_func = NULL;

		return add_to_event_loop(e, EPOLLOUT);
	}
	else{
		perror("Error connecting socket");
		return -1;
	}
}

int as_receive_async(async_socket_t * socket)
{
	if(socket == NULL){
		errno = EINVAL;
		return -1;
	}
	
	init_event_loop();

	async_socket_event_t * e = (async_socket_event_t *) malloc(sizeof(async_socket_event_t));
	if(e == NULL){ return -1; }
	e->socket = socket;
	e->function = RECV;
	e->socket_func = (generic_func)recvfrom;

	return add_to_event_loop(e, EPOLLIN | EPOLLET);
}

int as_send_async(async_socket_t * socket)
{
	if(socket == NULL){
		errno = EINVAL;
		return -1;
	}

	init_event_loop();

	async_socket_event_t * e = (async_socket_event_t *) malloc(sizeof(async_socket_event_t));
	if(e == NULL){ return -1; }
	e->socket = socket;
	e->function = SEND;
	e->socket_func = (generic_func)sendto;
	
	return add_to_event_loop(e, EPOLLOUT | EPOLLET);
}

int async_sock_shutdown(async_socket_t * socket, int how)
{
	if(socket == NULL){
		errno = EINVAL;
		return -1;
	}
	
	if(remove_from_event_loop(socket) < 0){
		perror("Error shutting down socket");
		return -1;
	}

	if(shutdown(socket->socket_fd, how) < 0){
		perror("Error shutting down socket");
		return -1;
	}
	
	return 0;
}

int async_sock_close(async_socket_t * socket)
{
	if(socket == NULL){
		errno = EINVAL;
		return -1;
	}

	async_sock_shutdown(socket, SHUT_RDWR);

	if(close(socket->socket_fd) < 0){
		perror("Error closing socket");
		return -1;
	}
	
	free(socket);

	return 0;
}
