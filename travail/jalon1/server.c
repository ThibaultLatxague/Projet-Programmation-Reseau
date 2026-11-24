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
};

struct client *clients = NULL;

void add_client(int fd, struct sockaddr_storage *addr, socklen_t addr_len)
{
	struct client *c = malloc(sizeof(struct client));
	if (c == NULL)
	{
		perror("malloc()");
		exit(EXIT_FAILURE);
	}
	c->fd = fd;
	c->addr = *addr;
	c->addr_len = addr_len;
	c->next = clients;
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

int handle_bind(char *serv_port)
{
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL, serv_port, &hints, &result) != 0)
	{
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
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

// Reçoit un message du client et le lui renvoie (ECHO).
// Retourne -1 si le client se déconnecte ou envoie /quit.
int handle_message(int sockfd)
{
	char buff[MSG_LEN];
	int size = 0;

	memset(buff, 0, MSG_LEN);
	if (recv_next_message_size(sockfd, &size) < 0)
		return -1;
	if (size <= 0 || size >= MSG_LEN)
		return -1;
	if (recv_message(sockfd, buff, size) < 0)
		return -1;

	if (strcmp(buff, "/quit\n") == 0)
	{
		printf("/quit received from client on fd %d\n", sockfd);
		return -1;
	}

	printf("Received: %s", buff);
	if (send_next_message_size(sockfd, size) < 0 || send_message(sockfd, buff, size) < 0)
		return -1;
	printf("Message sent!\n");
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
	if ((listen(listen_fd, BACKLOG)) != 0)
	{
		perror("listen()\n");
		exit(EXIT_FAILURE);
	}

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

	while (running)
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
