# LiteNet 

Simple crossplaftorm UDP networking library

 * single header library
 * cross plaftorm support
 * simple API for client and server
 * simple ussage

Here is some ussage of library :+1:

```c
int on_data_client(net_socket_t* from, char* data, size_t data_length)
{
	data[data_length] = '\0';
	printf("CLIENT %s", data);

	return 0;
}

int on_conn_client(net_socket_t* from, int reason)
{
	switch (reason) {

		case LITE_CONNECTED:
		{
			printf("[CLIENT] Connected !\n");
		}break;

		case LITE_MAXSLOTSREACHED:
		{
			printf("[CLIENT] Unable to connect (max slots reached) !\n");
		}break;

		case LITE_TIMEOUT: 
		{
			printf("[CLIENT] Unable to connect (cant reach server) !\n");
		}break;
	}

	return 0;
}

/* 
	-LITE_TIMEOUT - message when connection is lost 
	-LITE_DISCONNECTED - message when server disconnect client
*/

int on_diss_client(net_socket_t* from, int reason)
{
	switch (reason) {

		case LITE_TIMEOUT:
		{
			printf("[CLIENT] Disconnected timeout!\n");
		} break;

		case LITE_DISCONNECTED:
		{
			printf("[CLIENT] Disconnected\n");
		} break;
	}
	return 0;
}

//create context
net_client_t* client = litenet_client_create();

//bind needed events
client->events.event_conn = on_conn_client;
client->events.event_data = on_data_client;
client->events.event_diss = on_diss_client;

//Connect
litenet_client_connect(client, "127.0.0.1");

while (true) {

	//if connection is avable print ping
	if(client->connection)
  		printf("current ping: %d\n", client->connection->current_ping);

	
    //update 
    litenet_client_update(client);
    Sleep(1);
}
```


