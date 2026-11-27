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

// Pseudo actuel du client associé à sockfd ("" s'il est introuvable)
static const char *nickname_of(int sockfd)
{
    for (struct client *c = clients; c != NULL; c = c->next)
        if (c->fd == sockfd)
            return c->nickname;
    return "";
}

// Premier message d'un client : payload "heure|ip|port"
void handle_client_hello(int sockfd, const char *payload)
{
    struct client *c = clients;
    while (c != NULL && c->fd != sockfd)
        c = c->next;
    if (c == NULL)
        return;
    (void)payload; // date/IP/port sont déjà renseignés par le serveur dans add_client()

    char message_to_send[1024];
    snprintf(message_to_send, sizeof(message_to_send),
             "[Server] : Welcome on the chat, your temporary nickname is %s", c->nickname);
    if (send_response(sockfd, CLIENT_HELLO, "", message_to_send) < 0)
        printf("Error sending welcome message to client on fd %d\n", sockfd);
}

// Pseudo valide : 1 à NICK_LEN-1 caractères, uniquement lettres et chiffres
static int is_nickname_valid(const char *nick)
{
    size_t len = strlen(nick);
    if (len == 0 || len >= NICK_LEN)
        return 0;
    for (size_t i = 0; i < len; i++)
    {
        char ch = nick[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
            return 0;
    }
    return 1;
}

void handle_nickname_new(int sockfd, struct message *msg)
{
    // le serveur revalide le pseudo
    if (!is_nickname_valid(msg->infos)) {
        if (send_response(sockfd, NICKNAME_NEW, "", "[Server] : /!\\ Error : Invalid nickname (1 to 127 letters or digits only)") < 0)
            printf("Error sending nickname rejection to client on fd %d\n", sockfd);
        return;
    }
    // Erreur personnalisée si le client redemande le pseudo qu'il possède déjà
    if (strcmp(nickname_of(sockfd), msg->infos) == 0) {
        if (send_response(sockfd, NICKNAME_NEW, "", "[Server] : /!\\ Error : You already use this nickname") < 0)
            printf("Error sending nickname rejection to client on fd %d\n", sockfd);
        return;
    }
    if(is_nickname_available(msg->infos)) {
        change_nickname(sockfd, msg->infos);
        char message_to_send[1024];
        snprintf(message_to_send, sizeof(message_to_send), "[Server] : Welcome on the chat %s", msg->infos);
        if (send_response(sockfd, NICKNAME_NEW, "", message_to_send) < 0) {
            printf("Error sending nickname acceptance to client on fd %d\n", sockfd);
            return;
        }
    } else {
        // Ici, donner l'info au client que le nickname a été refusé
        if (send_response(sockfd, NICKNAME_NEW, "", "[Server] : /!\\ Error : Nickname already taken") < 0) {
            printf("Error sending nickname rejection to client on fd %d\n", sockfd);
            return;
        }
    }
    return;
}

void handle_nickname_list(int sockfd)
{
    char new_payload[1024];
    snprintf(new_payload, sizeof(new_payload), "[Server] : Online users are");
    int has_users = 0;
    for (struct client *c = clients; c != NULL; c = c->next)
    {
        if (c->nickname[0] == '\0')
            continue;
        has_users = 1;
        strncat(new_payload, "\n           - ", sizeof(new_payload) - strlen(new_payload) - 1);
        strncat(new_payload, c->nickname, sizeof(new_payload) - strlen(new_payload) - 1);
    }
    const char *payload = has_users ? new_payload : "[Server] : /!\\ Error : no nicknames available";

    if (send_response(sockfd, NICKNAME_LIST, "", payload) < 0) {
        printf("Error sending nickname list to client on fd %d\n", sockfd);
    }
}

void handle_nickname_infos(int sockfd, struct message *msg)
{
    char* client_info = get_client(sockfd, msg->infos);
    const char *payload = client_info != NULL ? client_info : "[Server] : /!\\ Error : Client not found";
    if (send_response(sockfd, NICKNAME_INFOS, "", payload) < 0)
    {
        printf("Error sending client info to client on fd %d\n", sockfd);
        return;
    }
}

void handle_unicast_send(int sockfd, struct message *msg, const char *payload)
{
    struct client *c = clients;
    char* recipient_nickname = msg->infos;
    // Le pseudo de l'expéditeur est celui connu du serveur (pas celui envoyé par le client)
    snprintf(msg->nick_sender, sizeof(msg->nick_sender), "%s", nickname_of(sockfd));
    char formatted_payload[1024];
    snprintf(formatted_payload, sizeof(formatted_payload), "[%s] : %s", msg->nick_sender, payload);
    payload = formatted_payload;
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
    send_response(sockfd, UNICAST_SEND, msg->nick_sender, "[Server] : /!\\ Error : User not found");
}

void handle_broadcast_send(int sockfd, struct message *msg, const char *payload)
{
    struct client *c = clients;
    snprintf(msg->nick_sender, sizeof(msg->nick_sender), "%s", nickname_of(sockfd));
    char formatted_payload[1024];
    snprintf(formatted_payload, sizeof(formatted_payload), "[%s] : %s", msg->nick_sender, payload);
    payload = formatted_payload;
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
    char message_to_send[1024];
    snprintf(message_to_send, sizeof(message_to_send), "[Server] : %s", payload);
    // send_response recalcule pld_len : msg->pld_len est celui du message reçu (sans le préfixe)
    if (send_response(sockfd, ECHO_SEND, "", message_to_send) < 0)
    {
        printf("Error sending message to client on fd %d\n", sockfd);
        return;
    }
    printf("End of ECHO_SEND\n");
    return;
}

void handle_file_request(int sockfd, struct message *msg, const char *payload){
    struct client *c = clients;
    char* recipient_nickname = msg->infos;

    // Le pseudo de l'expéditeur est celui connu du serveur (pas celui envoyé par le client)
    snprintf(msg->nick_sender, sizeof(msg->nick_sender), "%s", nickname_of(sockfd));

    // Vérification que le client n'envoie pas un fichier a lui-même
    if (strcmp(msg->nick_sender, recipient_nickname) == 0) {
        send_response(sockfd, FILE_REQUEST, "", "[Server] : /!\\ Error : Cannot send a file to yourself");
        return;
    }
    
    char formatted_payload[1024];
    snprintf(formatted_payload, sizeof(formatted_payload), "[Server] : [%s] wants you to accept the transfer of the file named \"%s\". Do you accept? [Y/N]", msg->nick_sender, payload);
    payload = formatted_payload;

    // Le serveur ne fait que relayer : la réponse Y/N du destinataire reviendra
    // plus tard sous forme d'un message FILE_ACCEPT / FILE_REJECT (cf. handle_file_answer).
    while (c != NULL)
    {
        if (c->nickname[0] != '\0' && strcmp(c->nickname, recipient_nickname) == 0)
        {
            if (send_response(c->fd, FILE_REQUEST, msg->nick_sender, payload) < 0)
                printf("Error sending file request to client on fd %d\n", c->fd);
            return;
        }
        c = c->next;
    }
    send_response(sockfd, FILE_REQUEST, "", "[Server] : /!\\ Error : User not found");
}

// Réponse du destinataire (FILE_ACCEPT / FILE_REJECT) : msg->infos = pseudo de l'émetteur initial.
// On la relaie à l'émetteur.
void handle_file_answer(int sockfd, struct message *msg, const char *payload)
{
    char text[1024];
    const char *answerer = nickname_of(sockfd);
    printf("[DEBUG][serveur] %s de '%s' pour l'emetteur '%s' : pld_len=%d, payload recu='%s'\n",
           msg->type == FILE_ACCEPT ? "FILE_ACCEPT" : "FILE_REJECT", answerer, msg->infos,
           msg->pld_len, payload ? payload : "(aucun)");
    // Acceptation : on relaie l'adresse "IP:port" du récepteur (le client affiche lui-même le message).
    // Refus : on relaie un texte.
    const char *to_send = text;
    if (msg->type == FILE_ACCEPT) {
        if (payload == NULL || payload[0] == '\0') {
            send_response(sockfd, FILE_REQUEST, "", "[Server] : /!\\ Error : Missing address in file acceptance");
            return;
        }
        to_send = payload;
    } else {
        snprintf(text, sizeof(text), "[Server] : %s declined the file transfer", answerer);
    }

    for (struct client *c = clients; c != NULL; c = c->next)
    {
        if (c->nickname[0] != '\0' && strcmp(c->nickname, msg->infos) == 0)
        {
            printf("[DEBUG][serveur] relais vers l'emetteur (fd %d) : '%s'\n", c->fd, to_send);
            if (send_response(c->fd, msg->type, answerer, to_send) < 0)
                printf("Error sending file answer to client on fd %d\n", c->fd);
            return;
        }
    }
    // Type FILE_REQUEST pour que le client affiche l'erreur sans la prendre pour une réponse
    send_response(sockfd, FILE_REQUEST, "", "[Server] : /!\\ Error : User not found");
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
            snprintf(client_info, sizeof(client_info), "[Server] : %s connected since %s with IP address %s and port number %s", c->nickname, c->time_of_connexion, c->ip_address, c->port_number);
            return client_info;
        }
        c = c->next;
    }
    return NULL;
}