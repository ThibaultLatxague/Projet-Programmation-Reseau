#include "../../include/protocol.h"

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
           message->pld_len <= MSG_LEN &&
           message->type >= NICKNAME_NEW &&
           message->type <= CLIENT_HELLO;
}

int protocol_send_message(int socket_fd, const struct message *message, const void *payload)
{
    if (!protocol_validate_message(message) || (message->pld_len > 0 && payload == NULL))
        return -1;
    /* On envoie les octets de la struct message d'un
     * seul bloc, dans l'ordre de la consigne (pld_len, nick_sender, type,
     * infos), au lieu de champ par champ dans un ordre différent.
     * On passe par une copie mise à zéro pour que les octets de padding de la
     * structure et la fin des chaînes ne contiennent pas de données
     * non initialisées, et pour garantir que les chaînes sont terminées par '\0'. */
    struct message wire;
    memset(&wire, 0, sizeof(wire));
    wire.pld_len = message->pld_len;
    wire.type = message->type;
    memcpy(wire.nick_sender, message->nick_sender, NICK_LEN - 1);
    memcpy(wire.infos, message->infos, INFOS_LEN - 1);
    if (protocol_send_all(socket_fd, &wire, sizeof(wire)) < 0)
        return -1;
    if (message->pld_len > 0 && protocol_send_all(socket_fd, payload, (size_t)message->pld_len) < 0)
        return -1;
    return 0;
}

int protocol_recv_message(int socket_fd, struct message *message, void **payload)
{
    if (message == NULL || payload == NULL)
        return -1;
    *payload = NULL;
    if (protocol_recv_all(socket_fd, message, sizeof(*message)) < 0)
        return -1;
    if (!protocol_validate_message(message))
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