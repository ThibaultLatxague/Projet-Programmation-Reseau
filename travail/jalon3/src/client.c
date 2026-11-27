#include "client.h"

// Pseudo de l'émetteur d'une demande de transfert en attente de notre réponse Y/N ("" si aucune)
static char pending_sender[NICK_LEN] = "";

// Prévient le serveur que la connexion est établie (CLIENT_HELLO) pour qu'il réponde
// par le message de bienvenue. Date, IP et port sont déduits par le serveur (accept()).
static void send_hello(int sfd)
{
    struct message msg = {0};
    msg.type = CLIENT_HELLO;
    if (protocol_send_message(sfd, &msg, NULL) < 0)
        fprintf(stderr, "Could not send hello to server\n");
}

int handle_connect(char *srv_addr, char *srv_port)
{
    struct addrinfo hints, *result, *rp;
    int sfd = -1;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC; // IPv4 ou IPv6
    hints.ai_socktype = SOCK_STREAM;
    die(getaddrinfo(srv_addr, srv_port, &hints, &result) != 0 ? -1 : 0, "error on getaddrinfo()");
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;
        close(sfd);
    }
    freeaddrinfo(result);
    if (rp == NULL) {
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }
    send_hello(sfd);
    return sfd;
}

/* ------------------------------ Saisie clavier ----------------------------- */

// Une demande de transfert est en attente : "Y"/"N" est la réponse à envoyer au serveur.
static void answer_file_request(int sockfd, char answer)
{
    struct message msg = {0};
    char address[128] = "";
    int accept_transfer = answer == 'Y' || answer == 'y';

    msg.type = accept_transfer ? FILE_ACCEPT : FILE_REJECT;
    snprintf(msg.infos, sizeof(msg.infos), "%s", pending_sender);

    // Acceptation : on ouvre d'abord la socket d'écoute, puis on annonce IP:port.
    // Si on ne peut pas écouter, on refuse le transfert.
    if (accept_transfer && ft_open_listener(sockfd, address, sizeof(address)) < 0)
        msg.type = FILE_REJECT;
    msg.pld_len = msg.type == FILE_ACCEPT ? (int)strlen(address) : 0;
    protocol_send_message(sockfd, &msg, msg.pld_len > 0 ? address : NULL);
    pending_sender[0] = '\0';
}

// Lit une ligne au clavier et l'envoie au serveur.
// Retourne -1 si le client doit s'arrêter (stdin fermée).
int handle_message(int sockfd)
{
    char *payload = handle_stdin();
    if (payload == NULL)
        return -1;

    if (pending_sender[0] != '\0') {
        char c = payload[0];
        if (c == 'Y' || c == 'y' || c == 'N' || c == 'n')
            answer_file_request(sockfd, c);
        else
            printf("Please respond with 'Y' or 'N': \n\n");
        free(payload);
        return 0;
    }

    enum msg_type type = handle_command(payload);
    if (type != CMD_INVALID) {
        struct message msg = {0};
        handle_struct(&msg, type, payload);
        protocol_send_message(sockfd, &msg, payload);
    }
    free(payload);
    return 0;
}

/* ---------------------------- Messages du serveur -------------------------- */

// Le récepteur a accepté : address contient son "IP:port", on lui envoie le fichier
static void start_file_transfer(const struct message *msg, const char *address)
{
    printf("File transfer request accepted by %s\n", msg->infos);
    printf("Starting file transfer...\n");
    if (ft_send_pending_file(address) == 0)
        printf("%s has received the file.\n", msg->infos);
    else
        printf("File transfer to %s failed\n", msg->infos);
    ft_clear_pending_file();
}

// Reçoit la réponse du serveur.
// Retourne -1 si le serveur s'est déconnecté.
int handle_server(int sockfd)
{
    struct message msg;
    void *payload = NULL;
    if (protocol_recv_message(sockfd, &msg, &payload) < 0)
        return -1;

    const char *payload_str = payload ? (const char *)payload : "";

    // Le payload d'un FILE_ACCEPT est l'adresse technique IP:port : on ne l'affiche pas
    if (msg.type != FILE_ACCEPT)
        printf("%s\n\n", payload_str);

    if (msg.type == FILE_REQUEST && msg.infos[0] != '\0') {
        // Demande de transfert reçue : on mémorise l'émetteur et on attend Y/N
        snprintf(pending_sender, sizeof(pending_sender), "%s", msg.infos);
    } else if (msg.type == FILE_REJECT) {
        printf("File transfer request rejected by %s\n", msg.infos);
        ft_clear_pending_file();
    } else if (msg.type == FILE_ACCEPT) {
        start_file_transfer(&msg, payload_str);
    }

    free(payload);
    return 0;
}

/* ----------------------------------- Main ---------------------------------- */

// Index des fds surveillés par poll
enum {
    IDX_STDIN = 0,
    IDX_SOCK,
    IDX_TRANSFER,
    NB_FDS
};

static void init_poll(struct pollfd *fds, int sfd)
{
    for (int i = 0; i < NB_FDS; i++) {
        fds[i].events = POLLIN;
        fds[i].revents = 0;
    }
    fds[IDX_STDIN].fd = STDIN_FILENO;
    fds[IDX_SOCK].fd = sfd;
}

// Retourne -1 si le client doit s'arrêter
static int handle_events(struct pollfd *fds, int sfd)
{
    if ((fds[IDX_STDIN].revents & (POLLIN | POLLHUP)) && handle_message(sfd) < 0)
        return -1;
    if ((fds[IDX_SOCK].revents & (POLLIN | POLLHUP | POLLERR)) && handle_server(sfd) < 0) {
        printf("server disconnected\n");
        return -1;
    }
    if (fds[IDX_TRANSFER].revents & POLLIN) {
        if (ft_accept_and_receive() < 0)
            printf("Erreur lors de la reception du fichier\n");
    }
    return 0;
}

int main(int argc, char *argv[])
{
    // 3 arguments : nom du programme + adresse + port
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Un write sur une socket fermée par le pair ne doit pas tuer le client
    signal(SIGPIPE, SIG_IGN);

    int sfd = handle_connect(argv[1], argv[2]);
    struct pollfd fds[NB_FDS];
    init_poll(fds, sfd);

    while (1) {
        fds[IDX_TRANSFER].fd = ft_listener_fd(); // socket d'écoute du transfert (-1 si aucune)
        int nready = poll(fds, NB_FDS, -1);
        if (nready < 0 && errno == EINTR)
            continue;
        if (nready < 0)
            break;
        if (handle_events(fds, sfd) < 0)
            break;
    }

    ft_close_listener();
    close(sfd);
    return EXIT_SUCCESS;
}
