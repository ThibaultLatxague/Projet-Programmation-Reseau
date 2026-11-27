#include <limits.h>
#include <sys/stat.h>

#include "client.h"

#define CHUNK_SIZE 4096

// Chemin du fichier que l'on propose d'envoyer (jusqu'à la réponse ACCEPT/REJECT)
static char pending_file_path[PATH_MAX] = "";
// Socket d'écoute du récepteur pour le transfert pair à pair (-1 si aucune)
static int listen_fd = -1;

/* ------------------------------ Utilitaires ------------------------------ */

const char *ft_basename(const char *path)
{
    const char *name = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');

    if (backslash != NULL && (name == NULL || backslash > name))
        name = backslash;
    return name != NULL ? name + 1 : path;
}

void ft_set_pending_file(const char *path)
{
    snprintf(pending_file_path, sizeof(pending_file_path), "%s", path);
}

void ft_clear_pending_file(void)
{
    pending_file_path[0] = '\0';
}

static int write_file_all(int fd, const void *buffer, size_t length)
{
    const unsigned char *data = buffer;
    size_t written = 0;

    while (written < length) {
        ssize_t result = write(fd, data + written, length - written);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return -1;
        written += (size_t)result;
    }
    return 0;
}

static int ensure_directory(const char *path)
{
    struct stat st;

    if (mkdir(path, 0755) == 0)
        return 0;
    if (errno != EEXIST)
        return -1;
    if (stat(path, &st) < 0)
        return -1;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

/* --------------------------- Socket d'écoute ----------------------------- */

int ft_listener_fd(void)
{
    return listen_fd;
}

void ft_close_listener(void)
{
    if (listen_fd >= 0)
        close(listen_fd);
    listen_fd = -1;
}

// Retourne un pointeur vers le port (ordre réseau) d'une adresse IPv4 ou IPv6
static in_port_t *address_port(struct sockaddr_storage *addr)
{
    if (addr->ss_family == AF_INET6)
        return &((struct sockaddr_in6 *)addr)->sin6_port;
    return &((struct sockaddr_in *)addr)->sin_port;
}

// Écrit "IP:port" (ou "[IPv6]:port") dans out
static int format_address(struct sockaddr_storage *addr, char *out, size_t out_size)
{
    char ip[INET6_ADDRSTRLEN];
    const void *raw = addr->ss_family == AF_INET6
                          ? (const void *)&((struct sockaddr_in6 *)addr)->sin6_addr
                          : (const void *)&((struct sockaddr_in *)addr)->sin_addr;
    int port = ntohs(*address_port(addr));

    if (inet_ntop(addr->ss_family, raw, ip, sizeof(ip)) == NULL)
        return -1;
    if (addr->ss_family == AF_INET6)
        snprintf(out, out_size, "[%s]:%d", ip, port);
    else
        snprintf(out, out_size, "%s:%d", ip, port);
    return 0;
}

int ft_open_listener(int server_fd, char *out, size_t out_size)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    ft_close_listener();
    // L'adresse locale de la connexion au serveur est celle que l'émetteur peut joindre
    if (getsockname(server_fd, (struct sockaddr *)&addr, &len) < 0 ||
        (addr.ss_family != AF_INET && addr.ss_family != AF_INET6)) {
        fprintf(stderr, "Could not determine local address\n");
        return -1;
    }
    *address_port(&addr) = 0; // port 0 : le système choisit un port libre

    int fd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    len = sizeof(addr);
    if (bind(fd, (struct sockaddr *)&addr, len) < 0 || listen(fd, 1) < 0 ||
        getsockname(fd, (struct sockaddr *)&addr, &len) < 0 ||
        format_address(&addr, out, out_size) < 0) {
        perror("bind/listen");
        close(fd);
        return -1;
    }
    printf("Listening on %s\n", out);
    listen_fd = fd;
    return 0;
}

/* ------------------------------ Émetteur --------------------------------- */

// Découpe "IP:port" ou "[IPv6]:port" en host et port (numérique, 1 à 65535)
static int parse_address(const char *address, char *host, size_t host_size, char *port, size_t port_size)
{
    const char *colon = strrchr(address, ':');
    const char *start = address;
    char *end = NULL;

    if (colon == NULL || colon == address)
        return -1;
    size_t len = (size_t)(colon - address);
    if (address[0] == '[' && colon[-1] == ']' && len >= 2) {
        start++;
        len -= 2;
    }
    long value = strtol(colon + 1, &end, 10);
    if (len == 0 || len >= host_size || end == colon + 1 || *end != '\0' || value < 1 || value > 65535)
        return -1;
    memcpy(host, start, len);
    host[len] = '\0';
    snprintf(port, port_size, "%ld", value);
    return 0;
}

static int connect_to_peer(const char *address)
{
    char host[INET6_ADDRSTRLEN], port[8];
    struct addrinfo hints, *result, *rp;
    int fd = -1;

    if (parse_address(address, host, sizeof(host), port, sizeof(port)) < 0) {
        printf("Invalid address received for file transfer: '%s'\n", address);
        return -1;
    }
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    if (getaddrinfo(host, port, &hints, &result) != 0) {
        printf("Invalid IP address received: '%s'\n", host);
        return -1;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    if (fd < 0)
        perror("connect");
    return fd;
}

// Attend le FILE_ACK du récepteur pour le fichier filename
static int wait_file_ack(int peer_fd, const char *filename)
{
    struct message ack;
    void *ack_payload = NULL;

    if (protocol_recv_message(peer_fd, &ack, &ack_payload) < 0) {
        printf("Could not receive file transfer acknowledgement\n");
        return -1;
    }
    free(ack_payload);
    if (ack.type != FILE_ACK || ack.pld_len != 0 || strcmp(ack.infos, filename) != 0) {
        printf("Invalid file transfer acknowledgement\n");
        return -1;
    }
    return 0;
}

// Envoie le contenu de file_fd par morceaux, puis un message vide de fin de fichier
static int send_chunks(int peer_fd, int file_fd, struct message *msg)
{
    unsigned char chunk[CHUNK_SIZE];

    while (1) {
        ssize_t n = read(file_fd, chunk, sizeof(chunk));

        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            perror("read");
            return -1;
        }
        msg->pld_len = (int)n;
        if (protocol_send_message(peer_fd, msg, n > 0 ? chunk : NULL) < 0) {
            printf("Could not send file chunk\n");
            return -1;
        }
        if (n == 0)
            return 0;
    }
}

// Envoie le fichier path (sous le nom filename) sur la socket peer_fd et attend l'ACK
static int send_file_chunks(int peer_fd, const char *path, const char *filename)
{
    struct message msg = {0};

    if (filename[0] == '\0' || strlen(filename) >= sizeof(msg.infos)) {
        printf("Invalid file path or filename\n");
        return -1;
    }
    int file_fd = open(path, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }
    msg.type = FILE_SEND;
    snprintf(msg.infos, sizeof(msg.infos), "%s", filename);

    int ret = send_chunks(peer_fd, file_fd, &msg);
    if (close(file_fd) < 0 && ret == 0) {
        perror("close");
        ret = -1;
    }
    return ret < 0 ? -1 : wait_file_ack(peer_fd, filename);
}

int ft_send_pending_file(const char *address)
{
    if (pending_file_path[0] == '\0') {
        printf("No file pending for transfer\n");
        return -1;
    }
    int peer_fd = connect_to_peer(address);
    if (peer_fd < 0)
        return -1;

    printf("Connected to receiver for file transfer\n");
    int ret = send_file_chunks(peer_fd, pending_file_path, ft_basename(pending_file_path));
    close(peer_fd);
    return ret;
}

/* ------------------------------ Récepteur -------------------------------- */

struct recv_state {
    int fd;                 // fichier de sortie (-1 tant que non créé)
    int created;
    size_t total;
    char filename[INFOS_LEN];
    char path[sizeof(INBOX_DIR) + INFOS_LEN + 1];
};

// Un nom de fichier reçu ne doit pas pouvoir sortir du dossier de réception
static int is_valid_filename(const char *name)
{
    return name[0] != '\0' && strchr(name, '/') == NULL && strchr(name, '\\') == NULL &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static int create_output_file(struct recv_state *st, const char *name)
{
    if (ensure_directory(".resa1") < 0 || ensure_directory(INBOX_DIR) < 0) {
        perror("create inbox directory");
        return -1;
    }
    snprintf(st->filename, sizeof(st->filename), "%s", name);
    snprintf(st->path, sizeof(st->path), "%s/%s", INBOX_DIR, name);
    st->fd = open(st->path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (st->fd < 0) {
        perror("open received file");
        return -1;
    }
    st->created = 1;
    printf("Receiving file '%s'...\n", st->filename);
    return 0;
}

// Valide un morceau reçu et l'écrit dans le fichier (créé au premier morceau)
static int store_chunk(struct recv_state *st, const struct message *msg, const void *payload)
{
    if (msg->type != FILE_SEND) {
        printf("Unexpected message type %d during file transfer\n", msg->type);
        return -1;
    }
    if (!is_valid_filename(msg->infos)) {
        printf("Invalid filename in file transfer\n");
        return -1;
    }
    if (st->fd < 0 && create_output_file(st, msg->infos) < 0)
        return -1;
    if (strcmp(st->filename, msg->infos) != 0) {
        printf("Filename changed during file transfer\n");
        return -1;
    }
    if (msg->pld_len > 0 && write_file_all(st->fd, payload, (size_t)msg->pld_len) < 0) {
        perror("write received file");
        return -1;
    }
    st->total += (size_t)msg->pld_len;
    return 0;
}

// Reçoit les morceaux jusqu'au message vide de fin de fichier
static int receive_chunks(int peer_fd, struct recv_state *st)
{
    struct message msg;
    void *payload;

    while (1) {
        if (protocol_recv_message(peer_fd, &msg, &payload) < 0) {
            printf("Could not receive file message (disconnected or invalid frame)\n");
            return -1;
        }
        int ret = store_chunk(st, &msg, payload);
        free(payload);
        if (ret < 0)
            return -1;
        if (msg.pld_len == 0)
            return 0;
    }
}

static int send_file_ack(int peer_fd, const char *filename)
{
    struct message ack = {0};

    ack.type = FILE_ACK;
    snprintf(ack.infos, sizeof(ack.infos), "%s", filename);
    if (protocol_send_message(peer_fd, &ack, NULL) < 0) {
        printf("Could not send file transfer acknowledgement\n");
        return -1;
    }
    return 0;
}

// Reçoit un fichier sur peer_fd (qui est fermée en sortie)
static int receive_file(int peer_fd)
{
    struct recv_state st = {.fd = -1};
    int ret = receive_chunks(peer_fd, &st);

    if (st.fd >= 0 && close(st.fd) < 0) {
        perror("close received file");
        ret = -1;
    }
    if (ret == 0)
        ret = send_file_ack(peer_fd, st.filename);
    if (ret == 0)
        printf("%s saved in %s (%zu bytes)\n", st.filename, st.path, st.total);
    else if (st.created && unlink(st.path) < 0)
        perror("remove incomplete received file");
    if (close(peer_fd) < 0)
        perror("close transfer socket");
    return ret;
}

int ft_accept_and_receive(void)
{
    int peer_fd = accept(listen_fd, NULL, NULL);
    int ret = -1;

    if (peer_fd < 0)
        perror("accept");
    else
        ret = receive_file(peer_fd);
    ft_close_listener();
    return ret;
}
