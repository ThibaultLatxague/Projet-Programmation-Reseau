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
// #include "msg_struct.h"
#include "commands.h"

void handle_nickname_new(int sockfd, struct message *msg)
{
    if(is_nickname_available(msg->infos)) {
        // Ici, donner la confirmation au client que le nickname a été accepté
        change_nickname(sockfd, msg->infos);
        struct message response;
        response.type = (enum msg_type)NICKNAME_NEW;
        response.pld_len = 0;
        strcpy(response.infos, msg->infos); // On renvoie le nickname pour informer le client que nickname enregistré
        if (send_next_message_size(sockfd, sizeof(response)) < 0 || send_message(sockfd, &response, sizeof(response)) < 0) {
            printf("Error sending nickname acceptance to client on fd %d\n", sockfd);
            return;
        }
        printf("New nickname request from client on fd %d: %s\n", sockfd, response.infos);
    } else {
        // Ici, donner l'info au client que le nickname a été refusé
        struct message response;
        response.type = (enum msg_type)NICKNAME_NEW;
        response.pld_len = 0;
        strcpy(response.infos, "Nickname already taken");
        if (send_next_message_size(sockfd, sizeof(response)) < 0 || send_message(sockfd, &response, sizeof(response)) < 0) {
            printf("Error sending nickname rejection to client on fd %d\n", sockfd);
            return;
        }
        printf("Nickname %s is already taken for client on fd %d\n", response.infos, sockfd);
    }
    printf("End of NICKNAME_NEW handling for client on fd %d\n", sockfd);
    return;
}

void handle_nickname_list(int sockfd)
{
    char* nicknames = get_nicknames(sockfd);
    struct message response;
    response.type = (enum msg_type)NICKNAME_LIST;
    response.pld_len = strlen(nicknames);

    if(nicknames[0] == '\0') {
        strcpy(response.infos, "<no nicknames available>");
    } else {
        strcpy(response.infos, nicknames);
    }

    if (send_next_message_size(sockfd, sizeof(response)) < 0 || send_message(sockfd, &response, sizeof(response)) < 0) {
        printf("Error sending nickname list to client on fd %d\n", sockfd);
    }
    printf("End of NICKNAME_LIST handling for client on fd %d\n", sockfd);
}

void handle_nickname_infos(int sockfd, struct message *msg)
{
    char* client_info = get_client(sockfd, msg->infos);
    struct message response;
    response.type = (enum msg_type)NICKNAME_INFOS;
    if (client_info != NULL)
    {
        response.pld_len = strlen(client_info);
        strcpy(response.infos, client_info);
    }
    else
    {
        response.pld_len = 0;
        strcpy(response.infos, "Client not found");
    }
    if (send_next_message_size(sockfd, sizeof(response)) < 0 || send_message(sockfd, &response, sizeof(response)) < 0)
    {
        printf("Error sending client info to client on fd %d\n", sockfd);
        return;
    }
    printf("End of NICKNAME_INFOS handling for client on fd %d\n", sockfd);
}

void handle_unicast_send(int sockfd, struct message *msg)
{
    // Ici, faire attention
    // Il faut différencier dans le message le pseudo du destinataire et le contenu du message
    // Actuellement, on ne fait aucune différence
    struct client *c = clients;
    char* recipient_nickname = msg->infos;
    struct message response;
    while (c != NULL)
    {
        if (strcmp(c->nickname, recipient_nickname) == 0)
        {
            response.type = (enum msg_type)UNICAST_SEND;
            response.pld_len = strlen(msg->infos);
            strcpy(response.infos, msg->infos);
            if (send_next_message_size(c->fd, sizeof(response)) < 0 || send_message(c->fd, &response, sizeof(response)) < 0)
            {
                printf("Error sending unicast message to client on fd %d\n", c->fd);
            }
            return;
        }
        c = c->next;
    }
    printf("Recipient %s not found for client on fd %d\n", recipient_nickname, sockfd);
}

void handle_broadcast_send(int sockfd, struct message *msg)
{
    struct client *c = clients;
    while (c != NULL)
    {
        if (c->fd != sockfd && c->nickname[0] != '\0')
        {
            struct message response;
            response.type = (enum msg_type)BROADCAST_SEND;
            response.pld_len = strlen(msg->infos);
            strcpy(response.infos, msg->infos);
            if (send_next_message_size(c->fd, sizeof(response)) < 0 || send_message(c->fd, &response, sizeof(response)) < 0)
            {
                printf("Error sending broadcast message to client on fd %d\n", c->fd);
            }
        }
        c = c->next;
    }
    printf("End of BROADCAST_SEND handling for client on fd %d\n", sockfd);
}

void handle_echo_send(int sockfd, struct message *msg)
{
    if (send_next_message_size(sockfd, sizeof(struct message)) < 0 || send_message(sockfd, msg, sizeof(struct message)) < 0)
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