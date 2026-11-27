#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"
#include "commands.h"
#include "../include/protocol.h"

#define BACKLOG 10
#define MAX_CONN 20

struct client *clients = NULL;

void add_client(int fd, struct sockaddr_storage *addr, socklen_t addr_len)
{
	struct client *c = malloc(sizeof(struct client));
	die(c == NULL ? -1 : 0, "malloc()");
	c->fd = fd;
	c->addr = *addr;
	c->addr_len = addr_len;
	// CORRECTION (whois côté serveur) : date, IP et port viennent du serveur
	// On fait ça côté serveur pour s'assurer de l'exactitude des données
	time_t now = time(NULL);
	strftime(c->time_of_connexion, sizeof(c->time_of_connexion), "%Y/%m/%d@%H:%M", localtime(&now));
	if (getnameinfo((struct sockaddr *)addr, addr_len,
					c->ip_address, sizeof(c->ip_address),
					c->port_number, sizeof(c->port_number),
					NI_NUMERICHOST | NI_NUMERICSERV) != 0)
	{
		strcpy(c->ip_address, "unknown");
		strcpy(c->port_number, "unknown");
	}

	// Pseudo temporaire "user<N>" où N s'incrémente à chaque connexion
	static int connection_count = 0;
	do
		snprintf(c->nickname, sizeof(c->nickname), "user%d", ++connection_count);
	while (!is_nickname_available(c->nickname));

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

int handle_command(int sockfd, struct message *msg, const char *payload)
{
	switch(msg->type)
	{
		case MULTICAST_QUIT:
			printf("/quit received from client on fd %d\n", sockfd);
			return -1;
		case CLIENT_HELLO:
			printf("Connection infos received from client on fd %d\n", sockfd);
			handle_client_hello(sockfd, payload);
			break;
		case NICKNAME_NEW:
			printf("/nick received from client on fd %d\n", sockfd);
			handle_nickname_new(sockfd, msg);
			break;
		case NICKNAME_LIST:
			printf("/who received from client on fd %d\n", sockfd);
			handle_nickname_list(sockfd);
			break;
		case NICKNAME_INFOS:
			printf("/whois received from client on fd %d\n", sockfd);
			handle_nickname_infos(sockfd, msg);
			break;
		case UNICAST_SEND:
			printf("/msg received from client on fd %d\n", sockfd);
			handle_unicast_send(sockfd, msg, payload);
			break;
		case BROADCAST_SEND:
			printf("/broadcast received from client on fd %d\n", sockfd);
			handle_broadcast_send(sockfd, msg, payload);
			break;
		case FILE_REQUEST:
			printf("/send received from client on fd %d\n", sockfd);
			handle_file_request(sockfd, msg, payload);
			break;
		case FILE_ACCEPT:
		case FILE_REJECT:
			printf("File answer received from client on fd %d\n", sockfd);
			handle_file_answer(sockfd, msg, payload);
			break;
		default:
			// Case ECHO_SEND par défaut selon les consignes du jalon 2
			printf("No commands (using echo) received from client on fd %d\n", sockfd);
			handle_echo_send(sockfd, msg, payload);
			break;
	}
	return 0;
}

// Reçoit un message du client et le lui renvoie (ECHO).
// Retourne -1 si le client se déconnecte ou envoie /quit.
int handle_message(int sockfd)
{ 
	struct message msg_received;
	void *payload = NULL;
	if (protocol_recv_message(sockfd, &msg_received, &payload) < 0)
		return -1;

	const char *payload_str = payload ? (const char *)payload : "";
	if (handle_command(sockfd, &msg_received, payload_str) < 0)
	{
		free(payload);
		return -1;
	}

	printf("Received: %s\n", payload_str);
	free(payload);
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
