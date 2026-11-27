#ifndef COMMANDS_H
#define COMMANDS_H

#include <sys/socket.h>

#include "msg_struct.h"
#include "client_list.h"

void handle_client_hello(int sockfd);
void handle_nickname_new(int sockfd, struct message *msg);
void handle_nickname_list(int sockfd);
void handle_nickname_infos(int sockfd, struct message *msg);
void handle_unicast_send(int sockfd, struct message *msg, const char *payload);
void handle_broadcast_send(int sockfd, struct message *msg, const char *payload);
void handle_echo_send(int sockfd, const char *payload);
void handle_file_request(int sockfd, struct message *msg, const char *payload);
void handle_file_answer(int sockfd, struct message *msg, const char *payload);

// Fonction utile pour handle_nickname_infos
char* get_client(const char* nickname);

#endif // COMMANDS_H
