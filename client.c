/* Luke Phillips
 * CS494 Project
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include <pthread.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#include <unistd.h>

#include "khash.h"
#include "common.h"

#define SERVER_PORT "2019"

#define BUFSIZE 1024

KHASH_MAP_INIT_INT(name, char *)
KHASH_MAP_INIT_STR(id, uint32_t)

khash_t(name) *room_names;
khash_t(id) *room_ids;
khash_t(name) *user_names;
khash_t(id) *user_ids;

pthread_mutex_t everything;

int
open_connection(char *server)
{
	int sockfd;
	int ret;

	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	ret = getaddrinfo(server, SERVER_PORT, &hints, &result);
	if (ret != 0) {
		fprintf(stderr, "Error - getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}

	/* search getaddrinfo() results */
	struct addrinfo *ri;
	for (ri = result; ri != NULL; ri = ri->ai_next) {
		sockfd = socket(ri->ai_family, ri->ai_socktype, ri->ai_protocol);
		if (sockfd < 0) continue; /* keep trying */

		ret = connect(sockfd, ri->ai_addr, ri->ai_addrlen);
		if (!ret) break; /* found */
		else close(sockfd);
	}

	if (!ri) {
		fprintf(stderr, "Error - connect() failed for all addresses.\n");
		return -1;
	}

	freeaddrinfo(result);

	printf("Successfully connected to %s.\n", server);

	return sockfd;
}

void
server_handler(long sfd)
{
	int sockfd = sfd;
	char buffer[BUFSIZE];
	khint_t it, it2;
	int ret;
	char *srcname, *roomname;

	for (;;) {
		int n = recv(sockfd, buffer, 8, MSG_WAITALL);
		if (n <= 0) {
			perror("connection closed");
			break;
		}
		/*for (int i = 0; i < 8; i++) {
			printf("-%x\n", buffer[i]);
		}*/
		uint16_t length = get_length(buffer);
		uint16_t response = get_operation(buffer);
		uint32_t subject = get_subject(buffer);
		if (length > 8) {
			n = recv(sockfd, buffer+8, length-8, 0);
			if (n <= 0) {
				fprintf(stderr, "%d ", n);
				perror("connection closed");
				break;
			}
		}

		switch (response) {
		case RES_MSG:
			pthread_mutex_lock(&everything);
			uint32_t srcid = get_srcid(buffer);
			uint32_t roomid = get_subject(buffer);
			it = kh_get(name, user_names, srcid);
			srcname = it != kh_end(user_names) ? kh_val(user_names, it) : "?";
			it2 = kh_get(name, room_names, roomid);
			roomname = it2 != kh_end(room_names) ? kh_val(room_names, it2) : "?";
			printf("<%s/%s>: %s\n", roomname, srcname, buffer+12);
			pthread_mutex_unlock(&everything);
			break;
		case RES_NICK:
			printf("Adding nick %s to database\n", buffer+8);
			pthread_mutex_lock(&everything);
			it = kh_put(name, user_names, subject, &ret);
			kh_value(user_names, it) = strdup(buffer+8);
			pthread_mutex_unlock(&everything);
			break;
		case RES_LEAVE:
			pthread_mutex_lock(&everything);
			it = kh_get(name, user_names, subject);
			srcname = it != kh_end(user_names) ? kh_val(user_names, it) : "?";
			printf("%s Left the server.\n", srcname);
			kh_del(name, user_names, subject);
			pthread_mutex_unlock(&everything);
			break;
		case RES_ROOM:
			printf("Adding room %s to database\n", buffer+8);
			pthread_mutex_lock(&everything);
			it = kh_put(name, room_names, subject, &ret);
			printf(buffer+8);
			kh_value(room_names, it) = strdup(buffer+8);
			pthread_mutex_unlock(&everything);
			break;
		default:
			break;
		}
	}

	close(sockfd);
	printf("Closed socket.\n");
}



int
main(int argc, char **argv)
{
	int sockfd = 0;
	int ret;
	char buffer[BUFSIZE];
	int selected = default_room;

	room_names = kh_init(name);
	user_names = kh_init(name);
	room_ids = kh_init(id);
	user_ids = kh_init(id);

	sockfd = open_connection(argv[1]);
	if (sockfd < 0) {
		exit(EXIT_FAILURE);
	}

	pthread_t thread;	
	pthread_create(&thread, NULL, (void*)server_handler, (void*)(long)sockfd);

	for (;;) {
		fgets(buffer+8, BUFSIZE-8, stdin);
		uint16_t length = remove_newline(buffer+8, BUFSIZE-8)+9;
		char *cursor = buffer+8;
		if (*cursor == '!') { // if control
			length -= 3;
			memmove(buffer+8-3, buffer+8, BUFSIZE-8);
			cursor-= 2;
			char control = *cursor;
			cursor+=2;
			switch (control) {
			case 'n': // nick
				write_operation(buffer, COM_NICK);
				write_subject(buffer, 0);
				write_length(buffer, length);
				send_message(sockfd, buffer, length);
				break;
			case 'j': // join
				write_operation(buffer, COM_JOIN);
				write_subject(buffer, atoi(buffer+8));
				length = 8;
				write_length(buffer, length);
				send_message(sockfd, buffer, length);
				break;
			case 'l': // leave
				write_operation(buffer, COM_LEAVE);
				write_subject(buffer, atoi(buffer+8));
				length = 8;
				write_length(buffer, length);
				send_message(sockfd, buffer, length);
				break;
			case 's': // select
				selected = atoi(buffer+8);
				break;
			case 'u': // list users
				printf("Connected Users:\n");
				for (khint_t x = kh_begin(user_names); x != kh_end(user_names); ++x) {
					if (!kh_exist(user_names, x)) continue;
					char *name = kh_val(user_names, x);
					uint32_t id = kh_key(user_names, x);
					printf("%d: %s\n", id, name);
				}
				break;
			case 'r': // list rooms
				printf("Available Rooms:\n");
				for (khint_t x = kh_begin(room_names); x != kh_end(room_names); ++x) {
					if (!kh_exist(room_names, x)) continue;
					char *name = kh_val(room_names, x);
					uint32_t id = kh_key(room_names, x);
					printf("%d: %s\n", id, name);
				}
				break;
			case 'c': // create room
				write_operation(buffer, COM_CREATE);
				write_subject(buffer, selected);
				write_length(buffer, length);
				send_message(sockfd, buffer, length);
				break;
			}
		} else {
			write_operation(buffer, COM_MSG);
			write_subject(buffer, selected);
			write_length(buffer, length);
			send_message(sockfd, buffer, length);
		}

	}

	exit(0);
}
