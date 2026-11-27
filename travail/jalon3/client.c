#include <time.h>
#include <limits.h>

#include "client.h"
#include "commands.h"

static int parse_nickname(const char *buffer, char *nick);

// Pseudo de l'émetteur d'une demande de transfert en attente de notre réponse Y/N ("" si aucune)
static char pending_sender[NICK_LEN] = "";
// Chemin du fichier que l'on propose d'envoyer (mémorisé jusqu'à la réponse ACCEPT/REJECT)
static char pending_file_path[PATH_MAX] = "";

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

// Socket d'écoute du récepteur pour le transfert pair à pair (-1 si aucune).
// Sera à surveiller dans le poll de main pour accepter l'émetteur (étape 4).
static int listen_fd = -1;

// Ouvre une socket d'écoute sur un port libre et écrit "IP:port" dans out.
// L'IP est celle de la connexion vers le serveur (celle que l'émetteur peut joindre).
// Retourne 0 en cas de succès, -1 sinon.
static int open_listening_socket(int server_fd, char *out, size_t out_size) {
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(server_fd, (struct sockaddr *)&addr, &len) < 0 || addr.sin_family != AF_INET) {
        fprintf(stderr, "Could not determine local IPv4 address\n");
        return -1;
    }
    addr.sin_port = 0; // port 0 : le système choisit un port libre

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 1) < 0) {
        perror("bind/listen");
        close(fd);
        return -1;
    }
    len = sizeof(addr);
    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        perror("getsockname");
        close(fd);
        return -1;
    }

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
    snprintf(out, out_size, "%s:%d", ip_str, ntohs(addr.sin_port));
    printf("Listening on %s\n", out);

    listen_fd = fd;
    return 0;
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

            // Acceptation : on ouvre d'abord la socket d'écoute, puis on annonce IP:port.
            // Si on ne peut pas écouter, on refuse le transfert.
            char payload_to_sender[128] = "";
            const char *pld = NULL;
            if (msg->type == FILE_ACCEPT) {
                if (open_listening_socket(sockfd, payload_to_sender, sizeof(payload_to_sender)) < 0) {
                    msg->type = FILE_REJECT;
                } else {
                    pld = payload_to_sender;
                }
            }
            msg->pld_len = pld != NULL ? (int)strlen(pld) : 0;
            protocol_send_message(sockfd, msg, pld);
            printf("[DEBUG][recepteur] %s envoye au serveur (infos='%s', pld_len=%d, payload='%s')\n",
                   msg->type == FILE_ACCEPT ? "FILE_ACCEPT" : "FILE_REJECT", msg->infos, msg->pld_len, pld ? pld : "");
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
        // Mémoriser le chemin du fichier pour l'envoyer après l'ACCEPT du destinataire
        snprintf(pending_file_path, sizeof(pending_file_path), "%s", payload);
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

/**
 * @brief Envoie le fichier indiqué par path sur la socket déjà connectée peer_fd.
 * @param peer_fd Le descripteur de la socket du destinataire
 * @param path Le chemin vers le fichier à envoyer
 * @param filename Le nom du fichier
 * @param sender_nick Le nickname de l'expéditeur
 * @return 0 en cas de succès et -1 en cas d'erreur.
 */
int send_file_chunks(int peer_fd, const char *path, const char *filename, const char *sender_nick) {
    struct message msg = {0};

    if (path == NULL || filename == NULL || filename[0] == '\0' || strlen(filename) >= sizeof(msg.infos)) {
        printf("Invalid file path or filename\n");
        return -1;
    }

    if (sender_nick != NULL && strlen(sender_nick) >= sizeof(msg.nick_sender)) {
        printf("Invalid sender nickname\n");
        return -1;
    }

    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }

    msg.type = FILE_SEND;
    strcpy(msg.infos, filename);
    if (sender_nick != NULL)
        strcpy(msg.nick_sender, sender_nick);

    unsigned char chunk[MSG_LEN];

    while (1) {
        int n = read(file_fd, chunk, sizeof(chunk));

        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            perror("read");
            close(file_fd);
            return -1;
        }
        if (n == 0)
            break;

        msg.pld_len = n;
        printf("[DEBUG][emetteur] FILE_SEND : morceau de %d octets sur la socket %d\n", n, peer_fd);

        if (protocol_send_message(peer_fd, &msg, chunk) < 0) {
            printf("Could not send file chunk\n");
            close(file_fd);
            return -1;
        }
    }

    if (close(file_fd) < 0) {
        perror("close");
        return -1;
    }

    msg.pld_len = 0;
    if (protocol_send_message(peer_fd, &msg, NULL) < 0) {
        printf("Could not send end-of-file message\n");
        return -1;
    }

    return 0;
}

// Reçoit la réponse du serveur (taille puis chaîne).
// Retourne -1 si le serveur s'est déconnecté.
int handle_server(int sockfd) {
    struct message msg_recieved;
    void *payload = NULL;
    if (protocol_recv_message(sockfd, &msg_recieved, &payload) < 0)
        return -1;

    char *payload_str = payload ? (char *) payload : "";

    if (strcmp(msg_recieved.infos, "/quit\n") == 0) {
        printf("/quit received from client on fd %d\n", sockfd);
        free(payload);
        return -1;
    } else {
        // Le payload d'un FILE_ACCEPT est l'adresse technique IP:port : on ne l'affiche pas
        if (msg_recieved.type != FILE_ACCEPT)
            printf("%s\n\n", payload_str);
        // Demande de transfert reçue : on mémorise l'émetteur et on attend Y/N
        if (msg_recieved.type == FILE_REQUEST && msg_recieved.infos[0] != '\0')
            snprintf(pending_sender, sizeof(pending_sender), "%s", msg_recieved.infos);
        if (msg_recieved.type == FILE_REJECT) {
            printf("File transfer request rejected by %s\n", msg_recieved.infos);
            pending_file_path[0] = '\0';
        }
        if (msg_recieved.type == FILE_ACCEPT) {
            // Ici, récupérer dans infos le pseudo de l'émetteur initial et dans le payload l'adresse et le port d'écoute du récepteur
            // Ensuite connecter à cette adresse/port pour envoyer le fichier
            // lancer le transfert de fichier
            printf("File transfer request accepted by %s\n", msg_recieved.infos);
            printf("Starting file transfer...\n");
            printf("[DEBUG][emetteur] FILE_ACCEPT recu : infos='%s', pld_len=%d, payload brut='%s'\n",
                   msg_recieved.infos, msg_recieved.pld_len, payload_str);

            // Extraction de "IP:port" (payload non vide et bien formé, port entre 1 et 65535)
            char ip_address[INET_ADDRSTRLEN];
            char *colon = payload != NULL ? strchr(payload_str, ':') : NULL;
            long port = 0;
            if (colon != NULL && colon != payload_str && (size_t)(colon - payload_str) < sizeof(ip_address)) {
                memcpy(ip_address, payload_str, colon - payload_str);
                ip_address[colon - payload_str] = '\0';
                char *end = NULL;
                port = strtol(colon + 1, &end, 10);
                if (end == colon + 1 || *end != '\0')
                    port = 0;
            }
            if (port < 1 || port > 65535) {
                printf("Invalid address received for file transfer: '%s'\n", payload_str);
                pending_file_path[0] = '\0';
                free(payload);
                return 0;
            }
            printf("[DEBUG][emetteur] adresse extraite : ip='%s', port=%ld\n", ip_address, port);

            // Lancement du transfert du fichier
            // printf("Starting file transfer to %s...\n", msg_recieved.infos); // msg_received.info contient le pseudo du celui qui recevra le fichier
            if (pending_file_path[0] == '\0') {
                printf("No file pending for transfer\n");
                free(payload);
                return 0; // Erreur
            } 

            // Ouverture d'une nouvelle socket pour le transfert
            int transfer_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (transfer_sock < 0) {
                perror("socket");
                pending_file_path[0] = '\0';
                free(payload);
                return 0;
            }
            
            // Création du struct avec le port et adresse IP donnée par le récepteur
            struct sockaddr_in transfer_addr;
            memset(&transfer_addr, 0, sizeof(transfer_addr));
            transfer_addr.sin_family = AF_INET;
            transfer_addr.sin_port = htons(port);
            if (inet_pton(AF_INET, ip_address, &transfer_addr.sin_addr) <= 0) {
                printf("Invalid IP address received: '%s'\n", ip_address);
                pending_file_path[0] = '\0';
                free(payload);
                close(transfer_sock);
                return 0;
            }

            // Connexion au récepteur pour le transfert de fichier
            printf("[DEBUG][emetteur] connect() vers %s:%ld (socket %d, differente de la socket serveur %d)\n",
                   ip_address, port, transfer_sock, sockfd);
            if (connect(transfer_sock, (struct sockaddr *)&transfer_addr, sizeof(transfer_addr)) < 0) {
                perror("connect");
                pending_file_path[0] = '\0'; // Annuler le fichier en attente de transfert
                free(payload);
                close(transfer_sock);
                return 0;
            }

            // Connexion réussie
            printf("Connected to receiver for file transfer\n");
            // Nom du fichier = dernier élément du chemin
            const char *filename = strrchr(pending_file_path, '/');
            const char *bs = strrchr(pending_file_path, '\\');
            if (bs != NULL && (filename == NULL || bs > filename))
                filename = bs;
            filename = filename ? filename + 1 : pending_file_path;

            printf("[DEBUG][emetteur] envoi de '%s' (chemin '%s') sur la socket %d\n", filename, pending_file_path, transfer_sock);
            int send_ret = send_file_chunks(transfer_sock, pending_file_path, filename, NULL);
            printf("[DEBUG][emetteur] send_file_chunks a retourne %d\n", send_ret);
            pending_file_path[0] = '\0';
            close(transfer_sock);
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
