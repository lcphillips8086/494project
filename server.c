/* Luke Phillips
 * CS494 Project
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "khash.h"
#include "common.h"

#define LISTEN_PORT 2019
#define BUFSIZE 1024

struct client;

struct room {
	int nmembers;
	uint32_t *members;
	char *name;
};

struct client {
	int sockfd;
	char *name;
};

KHASH_MAP_INIT_INT(room, struct room)
KHASH_MAP_INIT_INT(client, struct client)

khash_t(room) *rooms;
khash_t(client) *clients;

pthread_mutex_t everything;
int nextid;

void
add_room(int id, char *name)
{
	int ret;
	struct room record = {
		.nmembers = 0,
		.members = NULL,
		.name = name
	};
	khint_t it = kh_put(room, rooms, id, &ret);
	kh_value(rooms, it) = record;
}	

int
add_to_room(uint32_t id, uint32_t to_add, int *nrooms, uint32_t **membership)
{
	for (int i = 0; i < *nrooms; i++) {
		if ((*membership)[i] == to_add) {
			return 0;
		}
	}

	int res = -1;
	khint_t it = kh_get(room, rooms, to_add);
	if (it != kh_end(rooms)) {
		res = 1;
		uint32_t *members = kh_val(rooms, it).members;
		int nmembers = kh_val(rooms, it).nmembers;
		nmembers++;
		members = realloc(members, nmembers * sizeof(uint32_t));
		members[nmembers-1] = id;
		kh_val(rooms, it).members = members;
		kh_val(rooms, it).nmembers = nmembers;

		(*nrooms)++;
		*membership = realloc(*membership, (*nrooms) * sizeof(uint32_t));
		(*membership)[*nrooms-1] = to_add;
	}
	return res;
}

void
client_handler(long id)
{
	int sockfd;
	int nrooms = 0;
	uint32_t *membership = NULL;
	char buffer[BUFSIZE];

	pthread_mutex_lock(&everything);
	khint_t it = kh_get(client, clients, id);
	sockfd = kh_val(clients, it).sockfd;

	/* add to default room */
	add_to_room(id, default_room, &nrooms, &membership);

	/* send all current client names */
	write_operation(buffer, RES_NICK);
	for (int x = kh_begin(clients); x != kh_end(clients); ++x) {
		if (!kh_exist(clients, x)) continue;
		char *name = kh_val(clients, x).name;
		if (!name) continue;
		uint16_t peerid = kh_key(clients, x);
		write_subject(buffer, peerid);
		strncpy(buffer+8, name, BUFSIZE-9);
		uint16_t length = strnlen(buffer+8, BUFSIZE-9)+9;
		buffer[BUFSIZE-1] = '\0';
		write_length(buffer, length);
		send_message(sockfd, buffer, length);
	}

	/* send all current room names */
	write_operation(buffer, RES_ROOM);
	for (int x = kh_begin(rooms); x != kh_end(rooms); ++x) {
		if (!kh_exist(rooms, x)) continue;
		char *name = kh_val(rooms, x).name;
		if (!name) continue;
		uint16_t gid = kh_key(rooms, x);
		write_subject(buffer, gid);
		strncpy(buffer+8, name, BUFSIZE-9);
		uint16_t length = strnlen(buffer+8, BUFSIZE-9)+9;
		buffer[BUFSIZE-1] = '\0';
		write_length(buffer, length);
		send_message(sockfd, buffer, length);
	}

	pthread_mutex_unlock(&everything);
	printf("C - new connection with ID %ld on socket %d\n", id, sockfd);

	for (;;) {
		int n = recv(sockfd, buffer, 4, MSG_WAITALL);
		if (n <= 0) {
			fprintf(stderr, "%d ", n);
			perror("");
			break;
		}
		uint16_t length = get_length(buffer);
		uint16_t command = get_operation(buffer);;
		n = recv(sockfd, buffer+4, length-4, MSG_WAITALL);
		if (n <= 0) {
			fprintf(stderr, "%d ", n);
			perror("");
			break;
		}
		pthread_mutex_lock(&everything);
		uint32_t dst, room;
		switch (command) {
		case COM_MSG:
			dst = get_subject(buffer);
			write_operation(buffer, RES_MSG);
			memmove(buffer+12, buffer+8, length-8);
			length += 4;
			write_length(buffer, length);
			write_srcid(buffer, id);
			it = kh_get(room, rooms, dst);
			if (it == kh_end(rooms)) {
				printf("Room %d not found\n", dst);
				break;
			}
			uint32_t *members = kh_val(rooms, it).members;
			int nmembers = kh_val(rooms, it).nmembers;
			for (int i = 0; i < nmembers; i++) {
				int dstid = members[i];
				khint_t it2 = kh_get(client, clients, dstid);
				int dstfd = kh_val(clients, it2).sockfd;
				send_message(dstfd, buffer, length);
			}
			break;
		case COM_NICK:
			printf("Processing nickname\n");
			write_operation(buffer, RES_NICK);
			write_subject(buffer, id);
			for (int x = kh_begin(clients); x != kh_end(clients); ++x) {
				if (!kh_exist(clients, x)) continue;
				if (kh_key(clients, x) == id) {
					kh_val(clients, x).name = strdup(buffer+8);
				}
				int dstfd = kh_val(clients, x).sockfd;
				send_message(dstfd, buffer, length);
			}
			break;
		case COM_JOIN:
			printf("Processing join\n");
			room = get_subject(buffer);
			add_to_room(id, room, &nrooms, &membership);
			printf("Added %ld to room %d\n", id, room);
			break;
		case COM_CREATE:
			printf("Processing create\n");
			char *name = strdup(buffer+8);
			uint32_t newid = nextid++;
			add_room(newid, name);
			write_operation(buffer, RES_ROOM);
			write_subject(buffer, newid);
			for (int x = kh_begin(clients); x != kh_end(clients); ++x) {
				if (!kh_exist(clients, x)) continue;
				int dstfd = kh_val(clients, x).sockfd;
				send_message(dstfd, buffer, length);
			}
			break;
		default:
			printf("Invalid message\n");
			break;
		}
				
		pthread_mutex_unlock(&everything);
		
	}

	it = kh_get(client, clients, id);
	char *name = kh_val(clients, it).name;
	if (name) free(name);
	kh_del(client, clients, it);
	pthread_mutex_unlock(&everything);
	close(sockfd);
}


int
main(int argc, char **argv)
{
	int listenfd;
	int ret;
	struct sockaddr_in servaddr;

	pthread_mutex_init(&everything, NULL);

	clients = kh_init(client);
	rooms = kh_init(room);

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) {
		perror("Error - socket() failed creating listen socket");
		exit(-1);
	}

	memset(&servaddr, 0, sizeof(struct sockaddr_in));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(LISTEN_PORT);

	ret = bind(listenfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in));
	if (ret < 0) {
		perror("Error - bind() failed on listen socket");
		exit(-1);
	}

	listen(listenfd, SOMAXCONN);

	add_room(1000, strdup("default"));

	for (;;) {
		int sockfd;
		struct sockaddr_in cliaddr;
		pthread_t newthread;
		socklen_t addrlen = sizeof(cliaddr);

		sockfd = accept(listenfd, (struct sockaddr *)&cliaddr, &addrlen);
		if (sockfd < 0) {
			perror("Error - accept() failed");
			exit(0);
		}

		khint_t newid = nextid;
		nextid++;
		struct client record = {
			.sockfd = sockfd,
			.name = NULL
		};
		pthread_mutex_lock(&everything);
		khint_t idx = kh_put(client, clients, newid, &ret);
		kh_value(clients, idx) = record;
		pthread_mutex_unlock(&everything);

		pthread_create(&newthread, NULL, (void*)client_handler, (void*)(long)newid);
	}

	exit(0);
}
