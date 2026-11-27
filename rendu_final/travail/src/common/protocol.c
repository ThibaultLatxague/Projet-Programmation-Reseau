#include "../../include/protocol.h"

/* En-tête sur le réseau (cf. protocole.md), champ par champ :
 * type (2 octets) | payload_len (4 octets) | nick_sender (128) | infos (128)
 * Les entiers sont transmis en ordre réseau. */

int protocol_send_all(int socket_fd, const void *buffer, size_t length)
{
    const char *data = buffer;
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = write(socket_fd, data + sent, length - sent);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return -1;
        sent += (size_t)result;
    }

    return 0;
}

int protocol_recv_all(int socket_fd, void *buffer, size_t length)
{
    char *data = buffer;
    size_t received = 0;

    while (received < length) {
        ssize_t result = read(socket_fd, data + received, length - received);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return -1;
        received += (size_t)result;
    }

    return 0;
}

int protocol_validate_message(const struct message *message)
{
    return message != NULL &&
           message->pld_len >= 0 &&
           message->pld_len <= PROTO_MAX_PAYLOAD &&
           (int)message->type >= (int)NICKNAME_NEW &&
           (int)message->type <= (int)CLIENT_HELLO;
}

int protocol_send_message(int socket_fd, const struct message *message, const void *payload)
{
    if (!protocol_validate_message(message) || (message->pld_len > 0 && payload == NULL))
        return -1;

    /* Copies mises à zéro : les chaînes envoyées sont toujours terminées par '\0'
     * et ne contiennent aucune donnée non initialisée. */
    char nick[NICK_LEN] = {0};
    char infos[INFOS_LEN] = {0};
    memcpy(nick, message->nick_sender, NICK_LEN - 1);
    memcpy(infos, message->infos, INFOS_LEN - 1);

    uint16_t type = htons((uint16_t)message->type);
    uint32_t length = htonl((uint32_t)message->pld_len);

    if (protocol_send_all(socket_fd, &type, sizeof(type)) < 0 ||
        protocol_send_all(socket_fd, &length, sizeof(length)) < 0 ||
        protocol_send_all(socket_fd, nick, sizeof(nick)) < 0 ||
        protocol_send_all(socket_fd, infos, sizeof(infos)) < 0)
        return -1;
    if (message->pld_len > 0 && protocol_send_all(socket_fd, payload, (size_t)message->pld_len) < 0)
        return -1;
    return 0;
}

/* Lit et valide l'en-tête. La longueur est contrôlée avant toute allocation. */
static int recv_header(int socket_fd, struct message *message)
{
    uint16_t type;
    uint32_t length;

    memset(message, 0, sizeof(*message));
    if (protocol_recv_all(socket_fd, &type, sizeof(type)) < 0 ||
        protocol_recv_all(socket_fd, &length, sizeof(length)) < 0 ||
        protocol_recv_all(socket_fd, message->nick_sender, NICK_LEN) < 0 ||
        protocol_recv_all(socket_fd, message->infos, INFOS_LEN) < 0)
        return -1;

    type = ntohs(type);
    length = ntohl(length);
    if (length > PROTO_MAX_PAYLOAD || type > (uint16_t)CLIENT_HELLO)
        return -1;
    message->type = (enum msg_type)type;
    message->pld_len = (int)length;

    if (memchr(message->nick_sender, '\0', NICK_LEN) == NULL ||
        memchr(message->infos, '\0', INFOS_LEN) == NULL)
        return -1;
    return 0;
}

int protocol_recv_message(int socket_fd, struct message *message, void **payload)
{
    if (message == NULL || payload == NULL)
        return -1;
    *payload = NULL;
    if (recv_header(socket_fd, message) < 0)
        return -1;
    if (message->pld_len > 0) {
        char *data = malloc((size_t)message->pld_len + 1);
        if (data == NULL)
            return -1;
        if (protocol_recv_all(socket_fd, data, (size_t)message->pld_len) < 0) {
            free(data);
            return -1;
        }
        data[message->pld_len] = '\0';
        *payload = data;
    }
    return 0;
}
