#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "common.h"
#include "msg_struct.h"
#include "../include/protocol.h"

void handle_struct(struct message* msg, enum msg_type type, char* user_input);

int handle_connect(char *srv_addr, char *srv_port);

int handle_message(int sockfd);

int handle_server(int sockfd);

enum msg_type handle_command(char* buffer);

char* handle_stdin(void);
