#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>

#include "common.h"

#pragma region Fonction send

int send_next_message_size(int sockfd, int size)
{
    int bytes_sent = 0;
    int ret = 0;

    while (bytes_sent != sizeof(int))
    {
        ret = write(sockfd, (char *)&size + bytes_sent, sizeof(size) - bytes_sent);
        die(ret, "on writing msg size");
        bytes_sent += ret;
    }
    
    return bytes_sent;
}

int send_message(int sockfd, void *buf, int size)
{
    int bytes_sent = 0;
    int ret = 0;

    while (bytes_sent != size)
    {
        ret = write(sockfd, buf + bytes_sent, size - bytes_sent);
        die(ret, "on writing");
        bytes_sent += ret;
    }

    return bytes_sent;
}

#pragma endregion Fonction send

int handle_connect()
{
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(SERV_ADDR, SERV_PORT, &hints, &result) != 0)
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
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
		{
			break;
		}
		close(sfd);
	}
	if (rp == NULL)
	{
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

void handle_stdin(int sockfd)
{
	char buff[MSG_LEN];
	int n;
	// Cleaning memory
	memset(buff, 0, MSG_LEN);
	// Getting message from client
	printf("Message: ");
	n = 0;
	while ((buff[n++] = getchar()) != '\n')
	{
	} // trailing '\n' will be sent
	// Sending message (ECHO)
	int ret = send(sockfd, buff, strlen(buff), 0);
	die(ret, "on sending msg");
	printf("Message sent!\n");
}

int handle_server(int sockfd)
{
	char buff[MSG_LEN];
	// Cleaning memory
	memset(buff, 0, MSG_LEN);
	// Receiving message
	int ret = recv(sockfd, buff, MSG_LEN, 0);
	if (ret == 0 || ret < 0)
		return ret;
	printf("Received: %s", buff);
	return ret;
}

// Index des fds surveillés par poll
enum
{
	IDX_STDIN = 0,
	IDX_SOCK,
	NB_FDS
};

int main()
{
	int sfd = handle_connect();

	// Poll sur stdin (saisie utilisateur) et sur la socket (notifications serveur)
	struct pollfd fds[NB_FDS];
	fds[IDX_STDIN].fd = STDIN_FILENO;
	fds[IDX_STDIN].events = POLLIN;
	fds[IDX_STDIN].revents = 0;
	fds[IDX_SOCK].fd = sfd;
	fds[IDX_SOCK].events = POLLIN;
	fds[IDX_SOCK].revents = 0;

	while (1)
	{
		int nready = poll(fds, NB_FDS, -1);
		die(nready, "poll");

		// Saisie utilisateur
		if (fds[IDX_STDIN].revents & (POLLIN | POLLHUP))
		{
			handle_stdin(sfd);
		}

		// Notification du serveur
		if (fds[IDX_SOCK].revents & (POLLIN | POLLHUP | POLLERR))
		{
			int ret = handle_server(sfd);
			if (ret == 0)
			{
				printf("server disconnected\n");
				break;
			}
			die(ret, "on receiving response");
			fds[IDX_SOCK].revents = 0;
		}
	}

	close(sfd);
	return EXIT_SUCCESS;
}
