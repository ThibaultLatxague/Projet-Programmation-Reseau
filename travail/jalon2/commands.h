#ifndef COMMANDS_H
#define COMMANDS_H

#include <sys/socket.h>

#include "msg_struct.h"

/*
 * Le serveur doit conserver pour chaque client :
 * - le descripteur de socket
 * - l'adresse et le port du client
 * - la longueur de l'adresse (pour le port)
 * - un pointeur vers le client suivant dans la liste chaînée
 */
struct client
{
	int fd;
	struct sockaddr_storage addr;
	socklen_t addr_len;
	struct client *next;
	char nickname[NICK_LEN];
};

// Liste chaînée des clients (définie dans server.c)
extern struct client *clients;

void handle_nickname_new(int sockfd, struct message *msg);
void handle_nickname_list(int sockfd);
void handle_nickname_infos(int sockfd, struct message *msg);
void handle_unicast_send(int sockfd, struct message *msg, const char *payload);
void handle_broadcast_send(int sockfd, struct message *msg, const char *payload);
void handle_echo_send(int sockfd, struct message *msg, const char *payload);

// Fonctions utiles pour handle_nickname_new
void change_nickname(int sockfd, char* nickname);
int is_nickname_available(const char *nickname);

// Fonction utile pour handle_nickname_list
char* get_nicknames(int sockfd);

// Fonction utile pour handle_nickname_infos
char* get_client(int sockfd, char* nickname);

#endif // COMMANDS_H
