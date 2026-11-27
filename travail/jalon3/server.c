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
#include "client_list.h"
#include "../include/protocol.h"

#define BACKLOG 10
#define MAX_CONN 20

// Positionné par le gestionnaire de signal pour arrêter le serveur
static volatile sig_atomic_t stop_requested = 0;

static void on_stop_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static void install_signal_handlers(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_stop_signal; // sans SA_RESTART : poll() est interrompu
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	// Un write sur une socket fermée par un client ne doit pas tuer le serveur
	signal(SIGPIPE, SIG_IGN);
}

// Crée la socket d'écoute pour une famille donnée (-1 en cas d'échec).
// En IPv6, la socket accepte aussi les clients IPv4 (IPV6_V6ONLY désactivé).
static int bind_with_family(const char *serv_port, int family)
{
	struct addrinfo hints, *result, *rp;
	int sfd = -1;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if (getaddrinfo(NULL, serv_port, &hints, &result) != 0)
		return -1;
	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;
		int opt = 1;
		int off = 0;
		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		if (family == AF_INET6)
			setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
		if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(sfd);
		sfd = -1;
	}
	freeaddrinfo(result);
	return sfd;
}

int handle_bind(char *serv_port)
{
	int sfd = bind_with_family(serv_port, AF_INET6);
	if (sfd < 0)
		sfd = bind_with_family(serv_port, AF_INET);
	if (sfd < 0)
	{
		fprintf(stderr, "Could not bind\n");
		exit(EXIT_FAILURE);
	}
	return sfd;
}

// Libère le slot poll d'un client et le retire de la liste chaînée
void disconnect_client(struct pollfd *pfd)
{
	printf("Client on fd %d disconnected\n", pfd->fd);
	remove_client(pfd->fd);
	pfd->fd = -1;
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
			handle_client_hello(sockfd);
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
			handle_echo_send(sockfd, payload);
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

	free(payload);
	return 0;
}

// Accepte une nouvelle connexion et lui attribue un slot libre de fds (indices >= 1)
static void accept_new_client(int listen_fd, struct pollfd *fds)
{
	struct sockaddr_storage client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
	if (new_fd < 0)
	{
		perror("accept()");
		return;
	}
	printf("New socket on fd %d\n", new_fd);

	for (int j = 1; j < MAX_CONN; j++)
	{
		if (fds[j].fd == -1)
		{
			fds[j].fd = new_fd;
			fds[j].events = POLLIN;
			fds[j].revents = 0;
			add_client(new_fd, &client_addr, client_addr_len);
			return;
		}
	}
	fprintf(stderr, "Max connections reached, closing new connection\n");
	close(new_fd);
}

// Traite les événements des sockets clientes (indices >= 1)
static void serve_clients(struct pollfd *fds)
{
	for (int i = 1; i < MAX_CONN; i++)
	{
		if (fds[i].fd == -1)
			continue;
		if ((fds[i].revents & POLLIN) && handle_message(fds[i].fd) < 0)
			disconnect_client(&fds[i]);
		else if (!(fds[i].revents & POLLIN) && (fds[i].revents & (POLLHUP | POLLERR)))
			disconnect_client(&fds[i]);
	}
}

int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <server_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	install_signal_handlers();

	int listen_fd = handle_bind(argv[1]);
	die(listen(listen_fd, BACKLOG), "listen()");

	// Poll sur la socket d'écoute (indice 0) et sur les sockets clientes
	struct pollfd fds[MAX_CONN] = {0};
	for (int i = 1; i < MAX_CONN; i++)
		fds[i].fd = -1;
	fds[0].fd = listen_fd;
	fds[0].events = POLLIN;

	while (!stop_requested)
	{
		if (poll(fds, MAX_CONN, -1) < 0)
		{
			if (errno == EINTR)
				continue;
			perror("poll");
			break;
		}
		if (fds[0].revents & POLLIN)
			accept_new_client(listen_fd, fds);
		serve_clients(fds);
	}

	// Arrêt propre : fermeture de toutes les sockets et libération de la liste
	free_clients();
	close(listen_fd);
	return EXIT_SUCCESS;
}
