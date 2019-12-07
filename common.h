/* Luke Phillips
 * CS494 Project
 */

const uint32_t default_room = 1000;

enum {
	COM_INVALID,
	COM_MSG,
	COM_NICK,
	COM_JOIN,
	COM_LEAVE,
	COM_CREATE,
	COM_LIST,
	RES_MSG,
	RES_NICK,
	RES_ROOM,
	RES_LEAVE
};

int
remove_newline(char *string, int bufsize)
{
	int n = strnlen(string, bufsize);
	if (n > 0) n--;
	string[n] = '\0';
	return n;
}

void
write_length(char *buffer, int length)
{
	uint16_t l = length;
	l = htons(l);
	memcpy(buffer, &l, 2);
}

uint16_t
get_length(char *buffer)
{
	uint16_t length;
	memcpy(&length, buffer, 2);
	length = ntohs(length);
	return length;
}

void
write_operation(char *buffer, uint16_t operation)
{
	operation = htons(operation);
	memcpy(buffer+2, &operation, 2);
}

uint16_t
get_operation(char *buffer)
{
	uint16_t op;
	memcpy(&op, buffer+2, 2);
	op = ntohs(op);
	return op;
}

void
write_srcid(char *buffer, uint32_t id)
{
	id = htonl(id);
	memcpy(buffer+8, &id, 4);
}

uint32_t
get_srcid(char *buffer)
{
	uint32_t id;
	memcpy(&id, buffer+8, 4);
	id = ntohl(id);
	return id;
}

void
write_subject(char *buffer, uint32_t id)
{
	id = htonl(id);
	memcpy(buffer+4, &id, 4);
}

uint32_t
get_subject(char *buffer)
{
	uint32_t dst;
	memcpy(&dst, buffer+4, 4);
	dst = ntohl(dst);
	return dst;
}

void
send_message(int sockfd, char *buffer, int length)
{
	do {
		int n = send(sockfd, buffer, length, 0);
		if (n <= 0) return;
		length -= n;
		buffer += n;
	} while (length > 0);
}
