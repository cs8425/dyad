#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dyad.h"

/*
 * Simple socks5 proxy based on dyad.c by cs8425
 */

#define SERVER_PORT 8080

typedef struct {
	dyad_Stream *remote;
	dyad_Stream *client;
	char step;
	char addr[6];
} Client;

static void s2c_onData(dyad_Event *e);
static void c2s_onData(dyad_Event *e);

static void onClose(dyad_Event *e);

/*static void dump(char *data, int len) {
	unsigned int i;
	printf("%d:", len);
	for(i=0; i<len; i++){
		printf("%02x ", data[i] & 0xff);
	}
	printf("\n");
}*/

static void onConnect(dyad_Event *e) {
	Client *client = e->udata;
	/*printf("c2s_onConnect: OK!\n");*/

	/* response status */
	dyad_write(client->client, "\x05\x00\x00\x01", 4);
	dyad_write(client->client, client->addr, 6);

	/* bind data stream */
	dyad_addListener(e->stream, DYAD_EVENT_DATA, s2c_onData, client);
	dyad_addListener(client->client, DYAD_EVENT_DATA, c2s_onData, client);
}

static void remote_onError(dyad_Event *e) {
	Client *client = e->udata;
	/*printf("remote error: %s\n", e->msg);*/
	dyad_write(client->client, "\x05\x01", 2);
	dyad_end(client->client);
}

char *server  = "000.000.000.000";
char addr[16];

/* parse request && create connect */
static void client_onData(dyad_Event *e) {
	char ok = 0;
	unsigned short i;
	unsigned int port;

	Client *client = e->udata;

	printf("client_onInit: %s:%d, %d\n", dyad_getAddress(e->stream), dyad_getPort(e->stream), client->step);

	/*dump(e->data, e->size);*/

	switch(client->step){
		case 0:
			printf("init...");
			ok = 0;
			if((e->data[0] == 0x05) && (e->data[1]+2 == e->size)){
				for(i=0; i<e->data[1]; i++){
					if(e->data[i+2] == 0x00){
						ok = 1;
						break;
					}
				}
				if(ok){
					printf("no auth ok!\n");
					dyad_write(e->stream, "\x05\x00", 2);
					client->step++;
					return;
				}
			}
			printf("no auth not found!\n");
			/* no impl yet */
			dyad_write(e->stream, "\x05\xff", 2);
			dyad_end(e->stream);

		break;
		case 1:
			if((e->data[0] == 0x05)&&(e->data[1] == 0x01)&&(e->data[2] == 0x00)){
				/* parse address type & port (only IPv4 stream connection) */
				if((e->data[3] == 0x01) && (e->size == 10)){

					sprintf(addr, "%u.%u.%u.%u", e->data[4] & 0xFF, e->data[5] & 0xFF, e->data[6] & 0xFF, e->data[7] & 0xFF);
					port = ((e->data[8]&0xFF) << 8) | (e->data[9]&0xFF);
					memcpy(client->addr, (e->data+4), 6);

					//dump(client->addr, 6);
					printf("goto %s:%u\n", addr, port);

					dyad_removeListener(e->stream, DYAD_EVENT_DATA,  client_onData, client);

					/* create connect */
					client->remote = dyad_newStream();
					dyad_setTimeout(client->remote, 180.0);
					dyad_addListener(client->remote, DYAD_EVENT_CONNECT, onConnect, client);
					dyad_addListener(client->remote, DYAD_EVENT_ERROR, remote_onError, client);
					dyad_addListener(client->remote, DYAD_EVENT_CLOSE, onClose, client);
					dyad_connect(client->remote, addr, port);
					return;
				}
			}
			/* no impl yet */
			dyad_write(e->stream, "\x05\x01\x00", 3);
			dyad_end(e->stream);
		break;
	}
}

static void c2s_onData(dyad_Event *e) {
	Client *client = e->udata;

	/*printf("c2s_onData: %s:%d\n", dyad_getAddress(e->stream), dyad_getPort(e->stream));*/
	/*dump(e->data, e->size);*/

	if(client->remote){
		/* send to remote */
		dyad_write(client->remote, e->data, e->size);
	}else{
		/* no connect */
		printf("c2s_onData: no connect!\n");
		dyad_end(e->stream);
	}
}

static void s2c_onData(dyad_Event *e) {
	Client *client = e->udata;

	/*printf("s2c_onData: %s:%d\n", dyad_getAddress(e->stream), dyad_getPort(e->stream));*/
	/*dump(e->data, e->size);*/

	if(client->client){
		/* send back to client */
		dyad_write(client->client, e->data, e->size);
	}else{
		/* no connect */
		printf("s2c_onData: no connect!\n");
		dyad_end(e->stream);
	}
}

static void onClose(dyad_Event *e) {
	Client *client = e->udata;
	if(client != NULL){
		//printf("onClose: free e->udata!, %s:%d\n", dyad_getAddress(e->stream), dyad_getPort(e->stream));
		if (client->remote){
			dyad_removeAllListeners(client->remote, DYAD_EVENT_NULL);
			dyad_end(client->remote);
		}
		if (client->client){
			dyad_removeAllListeners(client->client, DYAD_EVENT_NULL);
			dyad_end(client->client);
		}
		free(client);
		e->udata = NULL;
	}
}

static void server_onAccept(dyad_Event *e) {
	printf("server onAccept: %s:%d\n", dyad_getAddress(e->stream), dyad_getPort(e->stream));
	Client *client = calloc(1, sizeof(*client));
	client->client = e->remote;
	client->step = 0;
	dyad_setTimeout(client->client, 20.0);
	dyad_addListener(e->remote, DYAD_EVENT_DATA, client_onData, client);
	dyad_addListener(e->remote, DYAD_EVENT_CLOSE, onClose, client);
}

static void server_onListen(dyad_Event *e) {
  printf("server listening: http://localhost:%d\n", dyad_getPort(e->stream));
}

static void server_onError(dyad_Event *e) {
  printf("server error: %s\n", e->msg);
}


int main(void) {
  dyad_Stream *serv;
  dyad_init();

  serv = dyad_newStream();
  dyad_addListener(serv, DYAD_EVENT_ERROR,  server_onError,  NULL);
  dyad_addListener(serv, DYAD_EVENT_ACCEPT, server_onAccept, NULL);
  dyad_addListener(serv, DYAD_EVENT_LISTEN, server_onListen, NULL);
  dyad_listen(serv, SERVER_PORT);

  while (dyad_getStreamCount() > 0) {
    dyad_update();
  }

  dyad_shutdown();
  return 0;
}
