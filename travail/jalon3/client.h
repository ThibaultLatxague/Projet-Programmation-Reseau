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
#include "client_commands.h"
#include "file_transfer.h"

// Connexion au serveur (quitte le programme en cas d'échec)
int handle_connect(char *srv_addr, char *srv_port);

// Saisie clavier : retourne -1 si le client doit s'arrêter
int handle_message(int sockfd);

// Message du serveur : retourne -1 si le serveur s'est déconnecté
int handle_server(int sockfd);
