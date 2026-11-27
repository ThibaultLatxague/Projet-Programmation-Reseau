#include <time.h>

#include "client.h"

static int parse_nickname(const char *buffer, char *nick);

// Pseudo de l'émetteur d'une demande de transfert en attente de notre réponse Y/N ("" si aucune)
static char pending_sender[NICK_LEN] = "";

// Prévient le serveur que la connexion est établie (type CLIENT_HELLO) pour qu'il
// réponde par le message de bienvenue.
// CORRECTION (whois côté serveur) : le client n'envoie plus sa date/IP/port. Le
// serveur les déduit lui-même de accept() : un client ne peut plus les falsifier.
static void send_hello(int sfd) {
    struct message msg = {0};
    msg.type = CLIENT_HELLO;
    msg.pld_len = 0;
    if (protocol_send_message(sfd, &msg, NULL) < 0) {
        fprintf(stderr, "Could not send hello to server\n");
    }
}

int handle_connect(char *srv_addr, char *srv_port) {
    struct addrinfo hints, *result, *rp;
    int sfd;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int ret = getaddrinfo(srv_addr, srv_port, &hints, &result);
    die(ret != 0 ? -1 : 0, "error on getaddrinfo()");
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1) {
            continue;
        }
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            send_hello(sfd);
            break;
        }
        close(sfd);
    }
    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }
    freeaddrinfo(result);
    return sfd;
}

// Lit une ligne au clavier et l'envoie au serveur (taille puis chaîne).
// Retourne -1 si le client doit s'arrêter (/quit ou stdin fermée).
int handle_message(int sockfd) {
    // CORRECTION (octets non initialisés) : calloc au lieu de malloc, sinon les
    // champs non remplis de la struct (infos, nick_sender...) contiennent des
    // données aléatoires de la mémoire.
    struct message *msg = calloc(1, sizeof(struct message));
    if (msg == NULL)
        return -1;
    char *payload = handle_stdin(sockfd);

    // Une demande de transfert est en attente : la saisie Y/N est la réponse à envoyer
    if (pending_sender[0] != '\0' && payload != NULL) {
        char answer = payload[0];
        if (answer == 'Y' || answer == 'y' || answer == 'N' || answer == 'n') {
            msg->type = (answer == 'Y' || answer == 'y') ? FILE_ACCEPT : FILE_REJECT;
            snprintf(msg->infos, sizeof(msg->infos), "%s", pending_sender);
            msg->pld_len = 0;
            protocol_send_message(sockfd, msg, NULL);
            pending_sender[0] = '\0';
            free(payload);
            free(msg);
            return 0;
        }
        printf("Please respond with 'Y' or 'N': \n\n");
        free(payload);
        free(msg);
        return 0;
    }

    enum msg_type type = handle_command(sockfd, payload);
    if ((int) type < 0) {
        free(payload);
        free(msg);
        return 0;
    }
    handle_struct(sockfd, msg, type, payload);
    protocol_send_message(sockfd, msg, payload);

    free(payload);
    free(msg);
    return 0;
}

void handle_struct(int sockfd, struct message *msg, enum msg_type type, char *payload) {
    if (type == NICKNAME_LIST) {
        memset(payload, 0, strlen(payload)); // a ajouter si le message est une commande qui ne nécessite pas de payload
        strcpy(msg->infos, "");
    } else if (type == NICKNAME_NEW) {
        // Le serveur reçoit uniquement le pseudo (déjà validé par handle_command)
        parse_nickname(payload, msg->infos);
        memset(payload, 0, strlen(payload));
    } else if (type == NICKNAME_INFOS) {
        strcpy(msg->infos, payload);
        memset(payload, 0, strlen(payload));
    } else if (type == ECHO_SEND) {
        strcpy(msg->infos, "");
    } else if (type == UNICAST_SEND) {
        char *space = strchr(payload, ' ');
        if (space != NULL) {
            size_t nick_len = space - payload;
            if (nick_len >= INFOS_LEN)
                nick_len = INFOS_LEN - 1; // évite de déborder de msg->infos
            strncpy(msg->infos, payload, nick_len);
            msg->infos[nick_len] = '\0';
            memmove(payload, space + 1, strlen(space + 1) + 1); // décaler pour retirer le pseudo
        }
    } else if (type == BROADCAST_SEND) {
        strcpy(msg->infos, "");
    } else if (type == FILE_REQUEST) {
        // "<pseudo> <fichier>" : infos = pseudo du destinataire, payload = nom du fichier
        char *space = strchr(payload, ' ');
        if (space != NULL) {
            size_t nick_len = space - payload;
            if (nick_len >= INFOS_LEN)
                nick_len = INFOS_LEN - 1;
            strncpy(msg->infos, payload, nick_len);
            msg->infos[nick_len] = '\0';
            memmove(payload, space + 1, strlen(space + 1) + 1);
        }
    }

    strcpy(msg->nick_sender, "userx");
    msg->type = type;
    msg->pld_len = strlen(payload);
}

// Extrait le pseudo d'une commande "/nick <pseudo>\n" dans nick (taille NICK_LEN).
// Retourne 0 si le pseudo est valide (lettres/chiffres uniquement, 1 à NICK_LEN-1
// caractères, la place restante étant réservée au '\0'), -1 sinon (message d'erreur affiché).
static int parse_nickname(const char *buffer, char *nick) {
    const char *start = buffer + strlen("/nick");
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
enum msg_type handle_command(int sockfd, char *buffer) {
    if (strcmp(buffer, "/quit\n") == 0) {
        return (enum msg_type) MULTICAST_QUIT;
    } else if (strncmp(buffer, "/nick", 5) == 0 && (buffer[5] == ' ' || buffer[5] == '\n' || buffer[5] == '\0')) {
        char nick[NICK_LEN];
        if (parse_nickname(buffer, nick) < 0)
            return (enum msg_type) -1;
        return (enum msg_type) NICKNAME_NEW;
    } else if (strcmp(buffer, "/who\n") == 0) {
        return (enum msg_type) NICKNAME_LIST;
    } else if (strncmp(buffer, "/whois", 6) == 0 && (buffer[6] == '\0' || buffer[6] == ' ')) {
        // Ne garder dans le buffer que l'argument (le pseudo) après "/whois "
        char *arg = buffer + 6;
        if (*arg == ' ')
            arg++;
        arg[strcspn(arg, "\n")] = '\0';
        memmove(buffer, arg, strlen(arg) + 1);
        return (enum msg_type) NICKNAME_INFOS;
    } else if (strncmp(buffer, "/msgall", 7) == 0 && (buffer[7] == '\0' || buffer[7] == ' ' || buffer[7] == '\n')) {
        char *arg = buffer + 7;
        if (*arg == ' ')
            arg++;
        arg[strcspn(arg, "\n")] = '\0';
        // Message d'usage si aucun message n'est fourni
        if (*arg == '\0') {
            fprintf(stderr, "Usage: /msgall <message>\n");
            return (enum msg_type) -1;
        }
        memmove(buffer, arg, strlen(arg) + 1);
        return (enum msg_type) BROADCAST_SEND;
    } else if (strncmp(buffer, "/msg", 4) == 0 && (buffer[4] == '\0' || buffer[4] == ' ' || buffer[4] == '\n')) {
        char *arg = buffer + 4;
        if (*arg == ' ')
            arg++;
        arg[strcspn(arg, "\n")] = '\0';
        // Message d'usage s'il manque le pseudo ou le texte ("/msg", "/msg Bob")
        char *sp = strchr(arg, ' ');
        if (*arg == '\0' || sp == NULL || sp == arg || *(sp + 1) == '\0') {
            fprintf(stderr, "Usage: /msg <nickname> <message>\n");
            return (enum msg_type) -1;
        }
        memmove(buffer, arg, strlen(arg) + 1);
        return (enum msg_type) UNICAST_SEND;
    } else if (strncmp(buffer, "/send", 5) == 0 && (buffer[5] == '\0' || buffer[5] == ' ' || buffer[5] == '\n')) {
        char *arg = buffer + 5;
        if (*arg == ' ')
            arg++;
        arg[strcspn(arg, "\n")] = '\0';
        // Message d'usage s'il manque le pseudo ou le nom du fichier
        char *sp = strchr(arg, ' ');
        if (*arg == '\0' || sp == NULL || sp == arg || *(sp + 1) == '\0') {
            fprintf(stderr, "Usage: /send <nickname> <file>\n");
            return (enum msg_type) -1;
        }
        memmove(buffer, arg, strlen(arg) + 1);
        return (enum msg_type) FILE_REQUEST;
    }

    return (enum msg_type) ECHO_SEND;
}

char *handle_stdin(int sockfd) {
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
int handle_server(int sockfd) {
    struct message msg_recieved;
    void *payload = NULL;
    if (protocol_recv_message(sockfd, &msg_recieved, &payload) < 0)
        return -1;

    const char *payload_str = payload ? (char *) payload : "";

    if (strcmp(msg_recieved.infos, "/quit\n") == 0) {
        printf("/quit received from client on fd %d\n", sockfd);
        free(payload);
        return -1;
    } else {
        printf("%s\n\n", payload_str);
        // Demande de transfert reçue : on mémorise l'émetteur et on attend Y/N
        if (msg_recieved.type == FILE_REQUEST && msg_recieved.infos[0] != '\0')
            snprintf(pending_sender, sizeof(pending_sender), "%s", msg_recieved.infos);
        if (msg_recieved.type == FILE_REJECT)
            printf("File transfer request rejected by %s\n", msg_recieved.nick);
        if (msg_recieved.type == FILE_ACCEPT) {
            // Ici, récupérer dans infos le pseudo de l'émetteur initial et dans le payload l'adresse et le port d'écoute du récepteur
            // Ensuite connecter à cette adresse/port pour envoyer le fichier
            // lancer le transfert de fichier
        }
    }


    free(payload);
    return 0;
}

// Index des fds surveillés par poll
enum {
    IDX_STDIN = 0,
    IDX_SOCK,
    NB_FDS
};

int main(int argc, char *argv[]) {
    // 3 arguments : nom du programme + adresse + port
    if (argc != 3) {
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

    while (1) {
        int nready = poll(fds, NB_FDS, -1);
        if (nready < 0) {
            if (errno == EINTR)
                continue;
            die(nready, "poll");
        }

        // Saisie utilisateur
        if (fds[IDX_STDIN].revents & (POLLIN | POLLHUP)) {
            if (handle_message(sfd) < 0)
                break;
        }

        // Réponse du serveur
        if (fds[IDX_SOCK].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (handle_server(sfd) < 0) {
                printf("server disconnected\n");
                break;
            }
        }
    }

    close(sfd);
    return EXIT_SUCCESS;
}
