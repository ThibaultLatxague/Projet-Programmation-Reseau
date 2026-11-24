#include "client.h"

int handle_connect(char *srv_addr, char *srv_port)
{
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int ret = getaddrinfo(srv_addr, srv_port, &hints, &result);
	die(ret != 0 ? -1 : 0, "error on getaddrinfo()");
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

// Lit une ligne au clavier et l'envoie au serveur (taille puis chaîne).
// Retourne -1 si le client doit s'arrêter (/quit ou stdin fermée).
int handle_message(int sockfd)
{
	struct message* msg = malloc(sizeof(struct message));
	char* user_input = handle_stdin(sockfd);
	enum msg_type type = handle_command(sockfd, user_input);
	printf("Handling command of type: %d\n", type);
	handle_struct(sockfd, msg, type, user_input);
	handle_send(sockfd, msg);
	
	free(user_input);
	free(msg);
	return 0;
}

void handle_send(int sockfd, struct message* msg){
	die(send_next_message_size(sockfd, sizeof(*msg)), "on sending msg size");
	die(send_message(sockfd, msg, sizeof(*msg)), "on sending msg");
}

void handle_struct(int sockfd, struct message* msg, enum msg_type type, char* user_input){
	if(type == NICKNAME_LIST || type == BROADCAST_SEND){
		msg->pld_len = 0;
		strcpy(msg->infos, "");
	} else {
		msg->pld_len = strlen(user_input);
		strcpy(msg->infos, user_input);
	}

	strcpy(msg->nick_sender, "TOTO");
	msg->type = type;
}

enum msg_type handle_command(int sockfd, char* buffer){
	if (strcmp(buffer, "/quit\n") == 0) {
		return (enum msg_type)MULTICAST_QUIT;
	} else if (strstr(buffer, "/nick") == buffer) {
		return (enum msg_type)NICKNAME_NEW;
	}
	return (enum msg_type)ECHO_SEND;
}

char* handle_stdin(int sockfd){
	char buff[MSG_LEN];
	memset(buff, 0, MSG_LEN);
	// Ici, Afficher le nom de l'utilisateur qui parle
	// Ici, vérifier la taille du message < 128 octets
	if (fgets(buff, MSG_LEN, stdin) == NULL)
		return NULL;

	return strdup(buff);
}

// Reçoit la réponse du serveur (taille puis chaîne).
// Retourne -1 si le serveur s'est déconnecté.
int handle_server(int sockfd)
{
	int size = 0;

	if (recv_next_message_size(sockfd, &size) < 0)
		return -1;

	struct message msg_recieved;
	// memset(&msg_recieved, 0, sizeof(struct message));

	if (recv_message(sockfd, &msg_recieved, size) < 0)
		return -1;

	if (strcmp(msg_recieved.infos, "/quit\n") == 0)
	{
		printf("/quit received from client on fd %d\n", sockfd);
		return -1;
	}

	printf("Received: %s", msg_recieved.infos);
}

// Index des fds surveillés par poll
enum
{
	IDX_STDIN = 0,
	IDX_SOCK,
	NB_FDS
};

int main(int argc, char *argv[])
{
	// 3 arguments : nom du programme + adresse + port
	if (argc != 3)
	{
		fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int sfd = handle_connect(argv[1], argv[2]);

	// Poll sur stdin (saisie utilisateur) et sur la socket (réponses serveur)
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
		if (nready < 0)
		{
			if (errno == EINTR)
				continue;
			die(nready, "poll");
		}

		// Saisie utilisateur
		if (fds[IDX_STDIN].revents & (POLLIN | POLLHUP))
		{
			if (handle_message(sfd) < 0)
				break;
		}

		// Réponse du serveur
		if (fds[IDX_SOCK].revents & (POLLIN | POLLHUP | POLLERR))
		{
			if (handle_server(sfd) < 0)
			{
				printf("server disconnected\n");
				break;
			}
		}
	}

	close(sfd);
	return EXIT_SUCCESS;
}
