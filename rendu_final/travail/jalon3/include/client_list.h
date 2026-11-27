#ifndef CLIENT_LIST_H
#define CLIENT_LIST_H

#include <sys/socket.h>

#include "msg_struct.h"

/*
 * Le serveur conserve pour chaque client :
 * - le descripteur de socket
 * - l'adresse et le port du client (issus de accept())
 * - la longueur de l'adresse
 * - un pointeur vers le client suivant dans la liste chaînée
 */
struct client
{
	int fd;
	struct sockaddr_storage addr;
	char time_of_connexion[64];
	char ip_address[64];
	char port_number[64];
	socklen_t addr_len;
	struct client *next;
	char nickname[NICK_LEN];
};

// Liste chaînée des clients (définie dans client_list.c)
extern struct client *clients;

void add_client(int fd, struct sockaddr_storage *addr, socklen_t addr_len);
// Ferme la socket du client et libère son maillon
void remove_client(int fd);
// Ferme toutes les sockets clientes et libère toute la liste
void free_clients(void);

const char *nickname_of(int sockfd);
void change_nickname(int sockfd, const char *nickname);
int is_nickname_available(const char *nickname);

#endif // CLIENT_LIST_H
