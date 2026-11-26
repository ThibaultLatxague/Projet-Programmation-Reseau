#include "client.h"

static int parse_nickname(const char* buffer, char* nick);

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
	char* payload = handle_stdin(sockfd);
	enum msg_type type = handle_command(sockfd, payload);
	if ((int)type < 0) {
		free(payload);
		free(msg);
		return 0;
	}
	printf("Handling command of type: %d\n", type);
	handle_struct(sockfd, msg, type, payload);
	protocol_send_message(sockfd, msg, payload);
	
	free(payload);
	free(msg);
	return 0;
}

void handle_struct(int sockfd, struct message* msg, enum msg_type type, char* payload){
	if(type == NICKNAME_LIST){
		memset(payload,0,strlen(payload)); // a ajouter si le message est une commande qui ne nécessite pas de payload
		strcpy(msg->infos, "");
	} else if (type == NICKNAME_NEW) {
		// Le serveur reçoit uniquement le pseudo (déjà validé par handle_command)
		parse_nickname(payload, msg->infos);
		memset(payload,0,strlen(payload));
	} else if (type == NICKNAME_INFOS) {
		strcpy(msg->infos, payload);
		memset(payload,0,strlen(payload));
	} else if (type == ECHO_SEND){
		strcpy(msg->infos, "");
	} else if (type == UNICAST_SEND) {
		char* space = strchr(payload, ' ');
		if (space != NULL) {
			size_t nick_len = space - payload;
			strncpy(msg->infos, payload, nick_len);
			msg->infos[nick_len] = '\0';
			memmove(payload, space + 1, strlen(space + 1) + 1); // décaler pour retirer le pseudo
		}
	} else if (type == BROADCAST_SEND) {
		strcpy(msg->infos, "");
	}

	strcpy(msg->nick_sender, "TOTO");
	msg->type = type;
	msg->pld_len = strlen(payload);
}

// Extrait le pseudo d'une commande "/nick <pseudo>\n" dans nick (taille NICK_LEN).
// Retourne 0 si le pseudo est valide (lettres/chiffres uniquement, 1 à NICK_LEN-1
// caractères, la place restante étant réservée au '\0'), -1 sinon (message d'erreur affiché).
static int parse_nickname(const char* buffer, char* nick){
	const char* start = buffer + strlen("/nick");
	if (*start == ' ')
		start++;
	size_t len = strcspn(start, "\n");

	if (len == 0) {
		fprintf(stderr, "Nickname empty. Usage: /nick <nickname>\n");
		return -1;
	}
	if (len >= NICK_LEN) {
		fprintf(stderr, "Nickname too long (max %d characters)\n", NICK_LEN - 1);
		return -1;
	}
	for (size_t i = 0; i < len; i++) {
		char c = start[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
			fprintf(stderr, "Nickname invalid: only letters and digits are allowed\n");
			return -1;
		}
	}
	memcpy(nick, start, len);
	nick[len] = '\0';
	return 0;
}

// Retourne (enum msg_type)-1 si la commande est invalide (rien ne doit être envoyé).
enum msg_type handle_command(int sockfd, char* buffer){
	if (strcmp(buffer, "/quit\n") == 0) {
		return (enum msg_type)MULTICAST_QUIT;
	} else if (strncmp(buffer, "/nick", 5) == 0 && (buffer[5] == ' ' || buffer[5] == '\n' || buffer[5] == '\0')) {
		char nick[NICK_LEN];
		if (parse_nickname(buffer, nick) < 0)
			return (enum msg_type)-1;
		return (enum msg_type)NICKNAME_NEW;
	} else if (strcmp(buffer, "/who\n") == 0) {
		printf("/who sent\n");
		return (enum msg_type)NICKNAME_LIST;
	} else if (strncmp(buffer, "/whois", 6) == 0 && (buffer[6] == '\0' || buffer[6] == ' ')) {
		// Ne garder dans le buffer que l'argument (le pseudo) après "/whois "
		char* arg = buffer + 6;
		if (*arg == ' ')
			arg++;
		arg[strcspn(arg, "\n")] = '\0';
		memmove(buffer, arg, strlen(arg) + 1);
		printf("/whois sent, argument: %s\n", buffer);
		return (enum msg_type)NICKNAME_INFOS;

	} else if (strncmp(buffer, "/msgall", 7) == 0 && (buffer[7] == '\0' || buffer[7] == ' ')) {
		char* arg = buffer + 7;
		if (*arg == ' ')
			arg++;
		arg[strcspn(arg, "\n")] = '\0';
		memmove(buffer, arg, strlen(arg) + 1);
		printf("/msgall sent, argument: %s\n", buffer);
		return (enum msg_type)BROADCAST_SEND;

	} else if (strncmp(buffer, "/msg", 4) == 0 && (buffer[4] == '\0' || buffer[4] == ' ')) {
		char* arg = buffer + 4;
		if (*arg == ' ')
			arg++;
		arg[strcspn(arg, "\n")] = '\0';
		memmove(buffer, arg, strlen(arg) + 1);
		printf("/msg sent, argument: %s\n", buffer);
		return (enum msg_type)UNICAST_SEND;
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
	struct message msg_recieved;
	void *payload = NULL;
	if (protocol_recv_message(sockfd, &msg_recieved, &payload) < 0)
		return -1;

	const char* payload_str = payload ? (char *)payload : "";

	if (strcmp(msg_recieved.infos, "/quit\n") == 0)
	{
		printf("/quit received from client on fd %d\n", sockfd);
		free(payload);
		return -1;
	}

	if(msg_recieved.type == NICKNAME_NEW) {
		if(strcmp(payload_str, "Nickname already taken") == 0) {
			printf("Nickname rejected by server: %s\n", payload_str);
		} else {
			printf("Nickname accepted by server: %s\n", payload_str);
		}
	}

	printf("Received: %s\n", payload_str);
	free(payload);
	return 0;
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
