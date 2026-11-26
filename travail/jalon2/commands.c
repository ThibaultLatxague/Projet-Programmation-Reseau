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
#include "commands.h"
#include "../include/protocol.h"

static int send_response(int sockfd, enum msg_type type, const char *infos, const char *payload)
{
	struct message response = {0};
	response.type = type;
	if (infos != NULL)
		snprintf(response.infos, sizeof(response.infos), "%s", infos);
	response.pld_len = payload != NULL ? (int)strlen(payload) : 0;
	return protocol_send_message(sockfd, &response, payload);
}

void handle_nickname_new(int sockfd, struct message *msg)
{
    if(is_nickname_available(msg->infos)) {
        // Ici, donner la confirmation au client que le nickname a été accepté
        change_nickname(sockfd, msg->infos);
        if (send_response(sockfd, NICKNAME_NEW, msg->infos, msg->infos) < 0) {
            printf("Error sending nickname acceptance to client on fd %d\n", sockfd);
            return;
        }
        printf("New nickname request from client on fd %d: %s\n", sockfd, msg->infos);
    } else {
        // Ici, donner l'info au client que le nickname a été refusé
        if (send_response(sockfd, NICKNAME_NEW, "Nickname already taken", "Nickname already taken") < 0) {
            printf("Error sending nickname rejection to client on fd %d\n", sockfd);
            return;
        }
        printf("Nickname %s is already taken for client on fd %d\n", msg->infos, sockfd);
    }
    printf("End of NICKNAME_NEW handling for client on fd %d\n", sockfd);
    return;
}

void handle_nickname_list(int sockfd)
{
    char* nicknames = get_nicknames(sockfd);
    const char *payload = nicknames[0] == '\0' ? "<no nicknames available>" : nicknames;
    if (send_response(sockfd, NICKNAME_LIST, "", payload) < 0) {
        printf("Error sending nickname list to client on fd %d\n", sockfd);
    }
    printf("End of NICKNAME_LIST handling for client on fd %d\n", sockfd);
}

void handle_nickname_infos(int sockfd, struct message *msg)
{
    char* client_info = get_client(sockfd, msg->infos);
    const char *payload = client_info != NULL ? client_info : "Client not found";
    if (send_response(sockfd, NICKNAME_INFOS, "", payload) < 0)
    {
        printf("Error sending client info to client on fd %d\n", sockfd);
        return;
    }
    printf("End of NICKNAME_INFOS handling for client on fd %d\n", sockfd);
}

void handle_unicast_send(int sockfd, struct message *msg, const char *payload)
{
    // Ici, faire attention
    // Il faut différencier dans le message le pseudo du destinataire et le contenu du message
    // Actuellement, on ne fait aucune différence
    struct client *c = clients;
    char* recipient_nickname = msg->infos;
    while (c != NULL)
    {
        if (strcmp(c->nickname, recipient_nickname) == 0)
        {
            if (send_response(c->fd, UNICAST_SEND, msg->nick_sender, payload) < 0)
            {
                printf("Error sending unicast message to client on fd %d\n", c->fd);
            }
            return;
        }
        c = c->next;
    }
    printf("Recipient %s not found for client on fd %d\n", recipient_nickname, sockfd);
}

void handle_broadcast_send(int sockfd, struct message *msg, const char *payload)
{
    struct client *c = clients;
    while (c != NULL)
    {
        if (c->fd != sockfd && c->nickname[0] != '\0')
        {
            if (send_response(c->fd, BROADCAST_SEND, msg->nick_sender, payload) < 0)
            {
                printf("Error sending broadcast message to client on fd %d\n", c->fd);
            }
        }
        c = c->next;
    }
    printf("End of BROADCAST_SEND handling for client on fd %d\n", sockfd);
}

void handle_echo_send(int sockfd, struct message *msg, const char *payload)
{
    if (protocol_send_message(sockfd, msg, payload) < 0)
    {
        printf("Error sending message to client on fd %d\n", sockfd);
        return;
    }
    printf("End of ECHO_SEND\n");
    return;
}


/* -------------- Fonctions utiles pour commandes -------------- */

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

char* get_nicknames(int sockfd){
    static char nicknames[1024];
    nicknames[0] = '\0';
    struct client *c = clients;
    while (c != NULL)
    {
        if (c->nickname[0] != '\0')
        {
            strcat(nicknames, c->nickname);
            strcat(nicknames, "\n");
        }
        c = c->next;
    }
    return nicknames;
}

char* get_client(int sockfd, char* nickname){
    static char client_info[1024];
    client_info[0] = '\0';
    struct client *c = clients;
    while (c != NULL)
    {
        if (strcmp(c->nickname, nickname) == 0)
        {
            snprintf(client_info, sizeof(client_info), "Nickname: %s\nFD: %d\n", c->nickname, c->fd);
            return client_info;
        }
        c = c->next;
    }
    return NULL;
}