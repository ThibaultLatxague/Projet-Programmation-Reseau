#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"
#include "msg_struct.h"

/* Taille maximale d'un payload applicatif (cf. protocole.md) */
#define PROTO_MAX_PAYLOAD 65536

struct message;

int protocol_send_all(int socket_fd, const void *buffer, size_t length);
int protocol_recv_all(int socket_fd, void *buffer, size_t length);
int protocol_send_message(int socket_fd, const struct message *message, const void *payload);
int protocol_recv_message(int socket_fd, struct message *message, void **payload);
int protocol_validate_message(const struct message *message);

#endif
