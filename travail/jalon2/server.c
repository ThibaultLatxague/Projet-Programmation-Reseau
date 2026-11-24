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

#include "common.h"
#include "msg_struct.h"

#define BACKLOG 10
#define MAX_CONN 20

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

struct client *clients = NULL;

void add_client(int fd, struct sockaddr_storage *addr, socklen_t addr_len)
{
	struct client *c = malloc(sizeof(struct client));
	die(c == NULL ? -1 : 0, "malloc()");
	c->fd = fd;
	c->addr = *addr;
	c->addr_len = addr_len;
	c->next = clients;
	c->nickname = "";
	clients = c;
}

// Ferme la socket du client et libère son maillon
void remove_client(int fd)
{
	struct client **p = &clients;
	while (*p != NULL)
	{
		if ((*p)->fd == fd)
		{
			struct client *to_free = *p;
			*p = to_free->next;
			close(to_free->fd);
			free(to_free);
			return;
		}
		p = &(*p)->next;
	}
}

void free_clients()
{
	while (clients != NULL)
		remove_client(clients->fd);
}

int is_nickname_available(const char *nickname)
{
	struct client *c = clients;
	while (c != NULL)
	{
		if (strcmp(c->nickname, nickname) == 0)
			return 0;
		c = c->next;
	}
	return 1;
}

int handle_bind(char *serv_port)
{
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	int gai_ret = getaddrinfo(NULL, serv_port, &hints, &result);
	die(gai_ret != 0 ? -1 : 0, "getaddrinfo()");
	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
		{
			continue;
		}
		int opt = 1;
		int ret = setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		die(ret, "on setsockopt");
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
		{
			break;
		}
		close(sfd);
	}
	if (rp == NULL)
	{
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

// Libère le slot poll d'un client et le retire de la liste chaînée
void disconnect_client(struct pollfd *pfd)
{
	printf("Client on fd %d disconnected\n", pfd->fd);
	remove_client(pfd->fd);
	pfd->fd = -1;
	pfd->events = 0;
	pfd->revents = 0;
}

int handle_command(int sockfd, char *buff)
{
	if (strncmp(buff, "/quit", 5) == 0 && (buff[5] == '\0' || buff[5] == ' '))
	{
		printf("/quit received from client on fd %d\n", sockfd);
		return -1;
	}
	else if (strncmp(buff, "/who", 3) == 0 && (buff[3] == '\0' || buff[3] == ' '))
  {
  }
}
		
void change_nickname(int sockfd, char* nickname){
	struct client *c = clients;
	while (c != NULL)
	{
		if (c->fd == sockfd)
		{
			strcpy(c->nickname, nickname);
			return;
		}
		c = c->next;
	}
}

// Reçoit un message du client et le lui renvoie (ECHO).
// Retourne -1 si le client se déconnecte ou envoie /quit.
int handle_message(int sockfd)
{
	int size = 0;

	if (recv_next_message_size(sockfd, &size) < 0)
		return -1;

	struct message msg_recieved;
	// memset(&msg_recieved, 0, sizeof(struct message));

	if (recv_message(sockfd, &msg_recieved, size) < 0)
		return -1;

	if (handle_command(sockfd, msg_recieved.infos) < 0)
		return -1;

	printf("Received: %s\n", msg_recieved.infos);

	if (msg_recieved.type == (enum msg_type)ECHO_SEND)
	{
		printf("Size: %d\n", size);
		printf("Message to send: %d\n", msg_recieved.type);
		if (send_next_message_size(sockfd, size) < 0 || send_message(sockfd, &msg_recieved, size) < 0)
		{
			printf("Error sending message to client on fd %d\n", sockfd);
			return -1;
		}
		printf("Message sent!\n");
	} else if (msg_recieved.type == (enum msg_type)NICKNAME_NEW) {
		if(is_nickname_available(msg_recieved.infos)) {
			// Ici, donner la confirmation au client que le nickname a été accepté
			change_nickname(sockfd, msg_recieved.infos);
			struct message msg;
			msg.type = (enum msg_type)NICKNAME_NEW;
			msg.pld_len = 0;
			strcpy(msg.infos, msg_recieved.infos); // On renvoie le nickname pour informer le client que nickname enregistré
			if (send_next_message_size(sockfd, sizeof(msg)) < 0 || send_message(sockfd, &msg, sizeof(msg)) < 0) {
				printf("Error sending nickname acceptance to client on fd %d\n", sockfd);
				return -1;
			}
			printf("New nickname request from client on fd %d: %s\n", sockfd, msg_recieved.infos);
		} else {
			// Ici, donner l'info au client que le nickname a été refusé
			struct message msg;
			msg.type = (enum msg_type)NICKNAME_NEW;
			msg.pld_len = 0;
			strcpy(msg.infos, "Nickname already taken");
			if (send_next_message_size(sockfd, sizeof(msg)) < 0 || send_message(sockfd, &msg, sizeof(msg)) < 0) {
				printf("Error sending nickname rejection to client on fd %d\n", sockfd);
				return -1;
			}
			printf("Nickname %s is already taken for client on fd %d\n", msg_recieved.infos, sockfd);
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int listen_fd = handle_bind(argv[1]);
	die(listen(listen_fd, BACKLOG), "listen()");

	// Poll sur la socket d'écoute (indice 0) et sur les sockets clientes
	struct pollfd fds[MAX_CONN] = {0};
	for (int i = 1; i < MAX_CONN; i++)
	{
		fds[i].fd = -1;
		fds[i].events = 0;
		fds[i].revents = 0;
	}
	fds[0].fd = listen_fd;
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	while (1)
	{
		int nfds = poll(fds, MAX_CONN, -1);
		if (nfds < 0)
		{
			if (errno == EINTR)
				continue;
			die(nfds, "poll");
		}

		for (int i = 0; i < MAX_CONN; i++)
		{
			if (fds[i].fd == listen_fd && (fds[i].revents & POLLIN))
			{
				struct sockaddr_storage client_addr;
				socklen_t client_addr_len = sizeof(client_addr);
				int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
				die(new_fd, "accepting");
				printf("New socket on fd %d\n", new_fd);

				int inserted = 0;
				for (int j = 1; j < MAX_CONN; j++)
				{
					if (fds[j].fd == -1)
					{
						fds[j].fd = new_fd;
						fds[j].events = POLLIN;
						fds[j].revents = 0;
						add_client(new_fd, &client_addr, client_addr_len);
						inserted = 1;
						break;
					}
				}
				if (!inserted)
				{
					fprintf(stderr, "Max connections reached, closing new connection\n");
					close(new_fd);
				}
				fds[i].revents = 0;
			}
			else if (fds[i].fd != listen_fd && fds[i].fd != -1 && (fds[i].revents & POLLIN))
			{
				if (handle_message(fds[i].fd) < 0)
					disconnect_client(&fds[i]);
			}
			else if (fds[i].fd != listen_fd && fds[i].fd != -1 && (fds[i].revents & (POLLHUP | POLLERR)))
			{
				disconnect_client(&fds[i]);
			}
		}
	}

	free_clients();
	close(listen_fd);
	return EXIT_SUCCESS;
}
