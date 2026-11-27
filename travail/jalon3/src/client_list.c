#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>

#include "common.h"
#include "client_list.h"

struct client *clients = NULL;

void add_client(int fd, struct sockaddr_storage *addr, socklen_t addr_len)
{
	struct client *c = calloc(1, sizeof(struct client));
	die(c == NULL ? -1 : 0, "calloc()");
	c->fd = fd;
	c->addr = *addr;
	c->addr_len = addr_len;
	// Date, IP et port viennent du serveur (accept()) : un client ne peut pas les falsifier
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

void free_clients(void)
{
	while (clients != NULL)
		remove_client(clients->fd);
}

const char *nickname_of(int sockfd)
{
	for (struct client *c = clients; c != NULL; c = c->next)
		if (c->fd == sockfd)
			return c->nickname;
	return "";
}

void change_nickname(int sockfd, const char *nickname)
{
	for (struct client *c = clients; c != NULL; c = c->next)
	{
		if (c->fd == sockfd)
		{
			snprintf(c->nickname, sizeof(c->nickname), "%s", nickname);
			return;
		}
	}
}

int is_nickname_available(const char *nickname)
{
	for (struct client *c = clients; c != NULL; c = c->next)
		if (strcmp(c->nickname, nickname) == 0)
			return 0;
	return 1;
}
