#ifndef _LITE_NET_ 
#define _LITE_NET_ 

#define LITE_SOCKET_INVALID 0
#define LITE_SOCKET_ERROR -1
#define LITE_DEFAULT_PORT 8080
#define LITE_DEFAULT_BUFLEN 1024
#define LITE_DEFAULT_TIMEOUT 2500
#define LITE_HEADER 0x2017
#define LITE_PING_INTERVAL (LITE_DEFAULT_TIMEOUT / 4)

#ifdef _WIN32
#undef UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#else 
	
#endif
//---------------------------------- 
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
//----------------------------------

#ifdef __cplusplus
extern "C" {
#endif

//INTERNAL MESSAGING 
enum {
	LITE_INTERN_MSG_CONNECT = 10, 
	LITE_INTERN_MSG_DISCONNECT,
	LITE_INTERN_MSG_PING,
	LITE_INTERN_MSG_DATA
};

//DISCONNECT RESULTS 
enum {
	LITE_CONNECTED,
	LITE_DISCONNECTED,
	LITE_TIMEOUT,
	LITE_MAXSLOTSREACHED
};

typedef struct net_socket_t {
	//address descriptor
	struct sockaddr_in si_other;
	//ping 
	uint64_t last_ping_recv;
	uint64_t last_ping_send;
	uint64_t current_ping;
} net_socket_t;

typedef struct net_data_packet_t {
	uint32_t msg_type;
	size_t data_length;
	char* data;
	net_socket_t* socket;
} net_data_packet_t;

typedef int lite_sock_t;
typedef int(*lite_event_on_connect)(net_socket_t*, int);
typedef int(*lite_event_on_disconnect)(net_socket_t*, int);
typedef int(*lite_event_on_data)(net_socket_t*, char*, size_t);

typedef struct net_events_t {
	lite_event_on_connect 		event_conn = NULL;
	lite_event_on_disconnect	event_diss = NULL;
	lite_event_on_data			event_data = NULL;
} net_events_t;

double timer_freq;
int64_t timer_started;

typedef struct net_client_t {
	/* server connection */
	net_socket_t* connection;
	uint64_t send_ping_delay;
	/* connecting */
	bool connecting;
	uint64_t connect_timeout;
	/* buffer */
	char* buffer;
	size_t buffer_length, recv_len;
	/* events */
	net_events_t events;
	/* local server address */
	lite_sock_t socket;
	struct sockaddr_in server_addr, si_other;
	int addr_size;
	/* cross platform related */
#ifdef _WIN32
	WSADATA wsa;
#endif
} net_client_t;

typedef struct net_server_t {
	/* connections pool */
	size_t connection_count;
	size_t max_connections;
	uint64_t send_ping_delay;
	net_socket_t** connections;
	/* buffer */
	char* buffer;
	size_t buffer_length, recv_len;
	/* events */
	net_events_t events;
	/* local server address */
	lite_sock_t socket;
	struct sockaddr_in server_addr, si_other;
	int addr_size;
	/* cross platform related */
	#ifdef _WIN32
		WSADATA wsa;
	#endif
} net_server_t;

//Helper functions 
void print_socket_error(const char* fn_name);
int sockaddr_cmp(struct sockaddr_in *x, struct sockaddr_in *y);
net_socket_t* find_socket_by_address(net_server_t* context, struct sockaddr_in addr);
void litenet_server_socket_dealoc(net_server_t* context, net_socket_t* sock);
void socket_send(lite_sock_t sock, net_socket_t* destination, char* data, size_t data_len);
net_data_packet_t* read_net_packet(char* buffer);
uint64_t get_current_timestamp();

//Server functions
net_server_t* litenet_server_create(size_t max_connections, size_t max_buflen = LITE_DEFAULT_BUFLEN);
int litenet_server_delete(net_server_t* context);
void litenet_server_update(net_server_t* context);
void litenet_server_disconnect(net_server_t* context, net_socket_t* client_sock);
void litenet_server_intern_data_process(net_server_t* context, net_data_packet_t* packet);

//Client functions 
net_client_t* litenet_client_create(size_t max_buflen = LITE_DEFAULT_BUFLEN);
int litenet_client_delete(net_client_t* context);
int litenet_client_connect(net_client_t* context, const char* server_addr, int port = LITE_DEFAULT_PORT);
void litenet_client_disconnect(net_client_t* context);
void litenet_client_update(net_client_t* context);
void litenet_client_intern_data_process(net_client_t* context, net_data_packet_t* packet);
void litenet_clock_init();

#ifdef __cplusplus
}
#endif

/////////////////////////////////////////////////////
//
// IMPLEMENTATION
//
/////////////////////////////////////////////////////
#ifdef LITE_NET_IMPLEMENTATION
#pragma region UTILS

void print_socket_error(const char* fn_name)
{
#ifdef _WIN32 
	printf("%s() - WSA failed: %d\n", fn_name, WSAGetLastError());
#else 
	printf("%s() failed: %s\n", fn_name, strerror(errno));
#endif
}

int sockaddr_cmp(struct sockaddr_in *x, struct sockaddr_in *y)
{
	return (x->sin_addr.S_un.S_addr == y->sin_addr.S_un.S_addr) && (x->sin_port == y->sin_port);
}

net_socket_t* find_socket_by_address(net_server_t* context, struct sockaddr_in addr)
{
	for (size_t i = 0; i < context->connection_count; i++) {
		net_socket_t* sock = context->connections[i];
		if (sockaddr_cmp(&sock->si_other, &addr)) return sock;
	}
	return NULL;
}

size_t find_index_by_sock(net_server_t* context, net_socket_t* socket)
{
	for (size_t i = 0; i < context->max_connections; i++) {
		if (context->connections[i] == socket) return i;
	}
	return -1;
}

void litenet_server_socket_dealoc(net_server_t* context, net_socket_t* sock)
{
	size_t idx = find_index_by_sock(context, sock);
	if (context->connections[idx] != NULL) {
		delete context->connections[idx];
		context->connections[idx] = NULL;
	}
}

void socket_send(lite_sock_t sock, net_socket_t* destination, char* data, size_t data_len)
{
	if (sendto(sock, data, data_len, 0, (struct sockaddr *) &destination->si_other, sizeof(destination->si_other)) == LITE_SOCKET_ERROR) {
		print_socket_error("sendto");
		return;
	}
}

void send_packet(lite_sock_t sock, net_data_packet_t* packet)
{
	size_t packet_length = packet->data_length + sizeof(uint32_t) * 2 + sizeof(size_t);
	uint32_t packet_header = LITE_HEADER;

	//Craft our packet
	char* send_packet = (char*)malloc(packet_length);
	memcpy(send_packet, &packet_header, sizeof(uint32_t));
	memcpy(send_packet + sizeof(uint32_t), &packet->msg_type, sizeof(uint32_t));
	memcpy(send_packet + (sizeof(uint32_t) * 2), &packet->data_length, sizeof(size_t));
	memcpy(send_packet + (sizeof(uint32_t) * 2) + sizeof(size_t), &packet->data, packet->data_length);

	socket_send(sock, packet->socket, send_packet, packet_length);
}

net_data_packet_t* read_net_packet(char* buffer)
{
	uint32_t packet_header;
	memcpy(&packet_header, buffer, sizeof(uint32_t));

	//NOTE(DavoSK): Check validity of packet also dont take invalid data while non blocking state 
	if (packet_header != LITE_HEADER || WSAGetLastError() == WSAEWOULDBLOCK) return NULL;

	net_data_packet_t* message = new net_data_packet_t;
	memcpy(&message->msg_type, buffer + sizeof(uint32_t), sizeof(uint32_t));
	memcpy(&message->data_length, buffer + (sizeof(uint32_t) * 2), sizeof(size_t));

	message->data = (char*)malloc(message->data_length);
	memcpy(message->data, buffer + ((sizeof(uint32_t) * 2) + sizeof(size_t)), message->data_length);

	return message;
}

uint64_t get_current_timestamp()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return uint64_t(li.QuadPart - timer_started) / timer_freq;
}

#pragma endregion UTILS

/* 
	litenet_init - start high res clock 
*/
void litenet_clock_init()
{
	timer_freq = 0.0f;
	timer_started = 0; 

	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	timer_freq = double(li.QuadPart) / 1000.0f;
	QueryPerformanceCounter(&li);
	timer_started = li.QuadPart;
}

/////////////////////////////////////////////////////
//
// CLIENT IMPLEMENTATION
//
/////////////////////////////////////////////////////
net_client_t* litenet_client_create(size_t max_buflen)
{
	net_client_t* new_ctx = (net_client_t*)malloc(sizeof(net_client_t));
	new_ctx->buffer_length = max_buflen;
	new_ctx->buffer = (char*)malloc(max_buflen);
	new_ctx->connection = NULL;

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &new_ctx->wsa) != 0) {
		print_socket_error("WSAStartup");
		return NULL;
	}
#endif
	if ((new_ctx->socket = socket(AF_INET, SOCK_DGRAM, 0)) == LITE_SOCKET_INVALID) {
		print_socket_error("socket");
		return NULL;
	}
	//-------------------------
	// Set the socket I/O mode: In this case FIONBIO
	// enables or disables the blocking mode for the 
	// socket based on the numerical value of iMode.
	// If iMode = 0, blocking is enabled; 
	// If iMode != 0, non-blocking mode is enabled.
#ifdef _WIN32
	u_long iMode = 1;
	int iResult = ioctlsocket(new_ctx->socket, FIONBIO, &iMode);
	if (iResult != NO_ERROR)
		printf("ioctlsocket failed with error: %ld\n", iResult);
#else 
	//TODO Linux non blocking state of socket ! 
#endif
	return new_ctx;
}

int litenet_client_delete(net_client_t* context)
{
	if (context->connection) {
		delete context->connection;
		context->connection = NULL;
	}

	delete context;
	context = NULL;
	return 0;
}

int litenet_client_connect(net_client_t* context, const char* server_addr, int port)
{
	//fetch ip into sockaddr
	context->si_other.sin_family = AF_INET;
	context->si_other.sin_addr.s_addr = inet_addr(server_addr);
	context->si_other.sin_port = htons(port);
	context->addr_size = sizeof(context->si_other);

	uint32_t dummy;
	net_socket_t net_sock = { context->si_other, NULL };
	net_data_packet_t message = { LITE_INTERN_MSG_CONNECT, sizeof(uint32_t), (char*)&dummy, &net_sock };
	send_packet(context->socket, &message);

	context->connect_timeout = get_current_timestamp();
	context->connecting = true;
	return 0;
}

void litenet_client_disconnect(net_client_t* context)
{
	net_data_packet_t new_packet = { 0 };
	new_packet.data = NULL;
	new_packet.data_length = NULL;
	new_packet.msg_type = LITE_INTERN_MSG_DISCONNECT;
	new_packet.socket = context->connection;

	send_packet(context->socket, &new_packet);
	context->connecting = false;
}

void litenet_client_update(net_client_t* context)
{
	if ((context->recv_len = recvfrom(context->socket, context->buffer, context->buffer_length, 0, (struct sockaddr *)&context->si_other, &context->addr_size)) == SOCKET_ERROR 
		&& WSAGetLastError() != WSAEWOULDBLOCK) {
		print_socket_error("recvfrom");
		return;
	} else {
		net_data_packet_t* packet = read_net_packet(context->buffer);

		if (packet) {
			packet->socket = context->connection;
			litenet_client_intern_data_process(context, packet);
		}
	}

	if (get_current_timestamp() - context->send_ping_delay > LITE_PING_INTERVAL) {

		if (context->connection) {
			if (get_current_timestamp() - context->connection->last_ping_recv > LITE_DEFAULT_TIMEOUT) {
				if (context->events.event_diss != NULL) {
					context->events.event_diss(context->connection, LITE_TIMEOUT);
				}
				delete context->connection;
				context->connection = NULL;
			}
		} 

		if (!context->connection && context->connecting) {
			
			if (get_current_timestamp() - context->connect_timeout > LITE_DEFAULT_TIMEOUT) {
				if (context->events.event_conn != NULL) {
					context->events.event_conn(NULL, LITE_TIMEOUT);
				}
				context->connecting = false;
			}
		}
		context->send_ping_delay = get_current_timestamp();
	}
}

void litenet_client_intern_data_process(net_client_t* context, net_data_packet_t* packet)
{
	switch (packet->msg_type) {

		case LITE_INTERN_MSG_CONNECT:
		{
			if (!packet->socket && context->connecting) {

				uint32_t connection_msg;
				memcpy(&connection_msg, packet->data, sizeof(uint32_t));

				if (connection_msg == LITE_CONNECTED) {

					net_socket_t* server_sock = new net_socket_t;
					server_sock->last_ping_recv = get_current_timestamp();
					server_sock->si_other = context->si_other;
					context->connection = server_sock;
					packet->socket = server_sock;
					packet->socket->current_ping = 0;
				}
			
				if(context->events.event_conn != NULL)
					context->events.event_conn(packet->socket, connection_msg);
			
				context->connecting = false;
			}
		} break;

		case LITE_INTERN_MSG_PING:
		{
			if (packet->socket) {

				uint64_t prev_ping;
				memcpy(&prev_ping, packet->data, packet->data_length);
				packet->socket->current_ping = prev_ping;
				packet->socket->last_ping_recv = get_current_timestamp();
				
				net_data_packet_t new_packet = { 0 };
				new_packet.data = NULL;
				new_packet.data_length = NULL;
				new_packet.msg_type = LITE_INTERN_MSG_PING;
				new_packet.socket = context->connection;
				send_packet(context->socket, &new_packet);
			}
		} break;

		case LITE_INTERN_MSG_DATA:
		{
			if (packet->socket && context->events.event_data != NULL)
				context->events.event_data(packet->socket, context->buffer, context->recv_len);

		} break;

		case LITE_INTERN_MSG_DISCONNECT:
		{
			if (context->events.event_diss != NULL) {
				context->events.event_diss(context->connection, LITE_DISCONNECTED);
			}

			delete context->connection;
			context->connection = NULL;
			context->connecting = false;

		} break;
	}
}

/////////////////////////////////////////////////////
//
// SERVER IMPLEMENTATION
//
/////////////////////////////////////////////////////
net_server_t* litenet_server_create(size_t max_connections, size_t max_buflen)
{
	net_server_t* new_ctx = (net_server_t*)malloc(sizeof(net_server_t));
	new_ctx->connection_count = 0;
	new_ctx->buffer_length = max_buflen;
	new_ctx->buffer = (char*)malloc(max_buflen);
	new_ctx->max_connections = max_connections;
	new_ctx->connections = (net_socket_t**)malloc(sizeof(net_socket_t) * max_connections);
	new_ctx->send_ping_delay = get_current_timestamp();

	for (size_t i = 0; i < max_connections; i++)
		new_ctx->connections[i] = NULL;

#ifdef _WIN32
	if (WSAStartup(MAKEWORD(2, 2), &new_ctx->wsa) != 0) {
		print_socket_error("WSAStartup");
		return NULL;
	}
#endif
	if ((new_ctx->socket = socket(AF_INET, SOCK_DGRAM, 0)) == LITE_SOCKET_INVALID) {
		print_socket_error("socket");
		return NULL;
	}
	//-------------------------
	// Set the socket I/O mode: In this case FIONBIO
	// enables or disables the blocking mode for the 
	// socket based on the numerical value of iMode.
	// If iMode = 0, blocking is enabled; 
	// If iMode != 0, non-blocking mode is enabled.
#ifdef _WIN32
	u_long iMode = 1;
	int iResult = ioctlsocket(new_ctx->socket, FIONBIO, &iMode);
	if (iResult != NO_ERROR)
		printf("ioctlsocket failed with error: %ld\n", iResult);
#endif
	new_ctx->server_addr.sin_family = AF_INET;
	new_ctx->server_addr.sin_addr.s_addr = INADDR_ANY;
	new_ctx->server_addr.sin_port = htons(LITE_DEFAULT_PORT);
	new_ctx->addr_size = sizeof(new_ctx->server_addr);

	if (bind(new_ctx->socket, (struct sockaddr *)&new_ctx->server_addr, sizeof(new_ctx->server_addr)) == LITE_SOCKET_ERROR) {
		print_socket_error("bind");
		return NULL;
	}
	return new_ctx;
}

int litenet_server_delete(net_server_t* context)
{
	if (context) {
		for (size_t i = 0; i < context->max_connections; i++) {
			net_socket_t* sock = context->connections[i];
			if (sock) {
				delete sock;
				sock = NULL;
			}
		}
	
		delete context;
		context = NULL;
	}
	return 0;
}

void litenet_server_update(net_server_t* context)
{
	if ((context->recv_len = recvfrom(context->socket, context->buffer, context->buffer_length, 0, (struct sockaddr *)&context->si_other, &context->addr_size)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
		print_socket_error("recvfrom");
		return;
	} else {

		net_data_packet_t* packet = read_net_packet(context->buffer);

		if (packet) {	
			packet->socket = find_socket_by_address(context, context->si_other);
			litenet_server_intern_data_process(context, packet);
		}
	}
	
	if (get_current_timestamp() - context->send_ping_delay > LITE_PING_INTERVAL) {
		
		for (size_t i = 0; i < context->max_connections; i++) {
			
			net_socket_t* sock = context->connections[i];

			if (sock) {
				if (get_current_timestamp() - sock->last_ping_recv > LITE_DEFAULT_TIMEOUT) {

					if (context->events.event_diss != NULL) {
						context->events.event_diss(sock, LITE_TIMEOUT);
					}
					
					litenet_server_socket_dealoc(context, sock);
				} else {

					sock->last_ping_send = get_current_timestamp();

					net_data_packet_t new_packet = { 0 };
					new_packet.data = (char*)sock->current_ping;
					new_packet.data_length = sizeof(uint64_t);
					new_packet.msg_type = LITE_INTERN_MSG_PING;
					new_packet.socket = sock;
					send_packet(context->socket, &new_packet);
				}
			}
		}

		context->send_ping_delay = get_current_timestamp();
	}
}

void litenet_server_disconnect(net_server_t* context, net_socket_t* client_sock)
{
	if (client_sock) {

		net_data_packet_t new_packet = { 0 };
		new_packet.data = NULL;
		new_packet.data_length = NULL;
		new_packet.msg_type = LITE_INTERN_MSG_DISCONNECT;
		new_packet.socket = client_sock;
		send_packet(context->socket, &new_packet);

		if (context->events.event_diss != NULL)
			context->events.event_diss(client_sock, LITE_DISCONNECTED);

		litenet_server_socket_dealoc(context, client_sock);
	}
}

void litenet_server_intern_data_process(net_server_t* context, net_data_packet_t* packet)
{
	switch (packet->msg_type) {
			
		case LITE_INTERN_MSG_CONNECT:
		{
			if (!packet->socket) {

				size_t free_idx = -1;
				for (size_t i = 0; i < context->max_connections; i++) {

					if (!context->connections[i]) {
						free_idx = i;
						break;
					}
				}

				if (free_idx != -1) {

					net_socket_t* sock = new net_socket_t;
					sock->last_ping_recv = get_current_timestamp();
					sock->current_ping = get_current_timestamp();
					sock->si_other = context->si_other;
					context->connections[free_idx] = sock;
					context->connection_count++;
				}
				
				if (context->events.event_conn != NULL)
					context->events.event_conn(context->connections[free_idx], free_idx == -1 ? LITE_MAXSLOTSREACHED : LITE_CONNECTED);

				uint32_t connect_mesg = free_idx == -1 ? LITE_MAXSLOTSREACHED : LITE_CONNECTED;
				net_socket_t net_sock = { context->si_other, NULL };
				
				net_data_packet_t new_packet = { 0 };
				new_packet.data = (char*)connect_mesg;
				new_packet.data_length = sizeof(uint32_t);
				new_packet.msg_type = LITE_INTERN_MSG_CONNECT;
				new_packet.socket = &net_sock;
				send_packet(context->socket, &new_packet);
			}

		} break;

		case LITE_INTERN_MSG_PING: 
		{
			if (packet->socket) {

				packet->socket->last_ping_recv = get_current_timestamp();
				packet->socket->current_ping = (packet->socket->last_ping_recv - packet->socket->last_ping_send);
			}
		} break;

		case LITE_INTERN_MSG_DATA:
		{
			if (packet->socket && context->events.event_data != NULL)
				context->events.event_data(packet->socket, context->buffer, context->recv_len);

		} break;

		case LITE_INTERN_MSG_DISCONNECT:
		{
			if (packet->socket) {

				if (context->events.event_diss != NULL)
					context->events.event_diss(packet->socket, LITE_DISCONNECTED);

				litenet_server_socket_dealoc(context, packet->socket);
			}
		} break;
	}
}
#endif
#endif