#include "client.h"

/* ------------------------------- Pseudos --------------------------------- */

// Extrait le pseudo d'une commande "/nick <pseudo>\n" dans nick (taille NICK_LEN).
// Retourne 0 si le pseudo est valide (lettres/chiffres uniquement, 1 à NICK_LEN-1
// caractères, la place restante étant réservée au '\0'), -1 sinon (message d'erreur affiché).
static int parse_nickname(const char *buffer, char *nick)
{
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

/* ------------------------- Analyse des commandes -------------------------- */

// Vrai si buffer commence par la commande cmd, suivie d'un espace, d'un '\n' ou de la fin
static int is_command(const char *buffer, const char *cmd)
{
    size_t len = strlen(cmd);
    return strncmp(buffer, cmd, len) == 0 && (buffer[len] == '\0' || buffer[len] == ' ' || buffer[len] == '\n');
}

// Ne garde dans buffer que l'argument de la commande (sans '\n') et le retourne
static char *keep_argument(char *buffer, size_t cmd_len)
{
    char *arg = buffer + cmd_len;
    if (*arg == ' ')
        arg++;
    arg[strcspn(arg, "\n")] = '\0';
    memmove(buffer, arg, strlen(arg) + 1);
    return buffer;
}

static enum msg_type parse_nick(char *buffer)
{
    char nick[NICK_LEN];
    return parse_nickname(buffer, nick) < 0 ? CMD_INVALID : NICKNAME_NEW;
}

static enum msg_type parse_whois(char *buffer)
{
    keep_argument(buffer, strlen("/whois"));
    return NICKNAME_INFOS;
}

static enum msg_type parse_msgall(char *buffer)
{
    if (*keep_argument(buffer, strlen("/msgall")) == '\0') {
        fprintf(stderr, "Usage: /msgall <message>\n");
        return CMD_INVALID;
    }
    return BROADCAST_SEND;
}

// Vrai si buffer est de la forme "<pseudo> <reste non vide>"
static int has_nick_and_text(const char *buffer)
{
    const char *space = strchr(buffer, ' ');
    return buffer[0] != '\0' && space != NULL && space != buffer && space[1] != '\0';
}

static enum msg_type parse_msg(char *buffer)
{
    if (!has_nick_and_text(keep_argument(buffer, strlen("/msg")))) {
        fprintf(stderr, "Usage: /msg <nickname> <message>\n");
        return CMD_INVALID;
    }
    return UNICAST_SEND;
}

static enum msg_type parse_send(char *buffer)
{
    if (!has_nick_and_text(keep_argument(buffer, strlen("/send")))) {
        fprintf(stderr, "Usage: /send <nickname> <file>\n");
        return CMD_INVALID;
    }
    const char *file_path = strchr(buffer, ' ') + 1;
    // Un chemin entre guillemets doit être fermé et non vide
    if (file_path[0] == '"' && (strlen(file_path) < 3 || file_path[strlen(file_path) - 1] != '"')) {
        fprintf(stderr, "Usage: /send <nickname> <file>\n");
        return CMD_INVALID;
    }
    return FILE_REQUEST;
}

struct command {
    const char *name;
    enum msg_type (*parse)(char *buffer);
};

static const struct command commands[] = {
    {"/nick", parse_nick},
    {"/whois", parse_whois},
    {"/msgall", parse_msgall},
    {"/msg", parse_msg},
    {"/send", parse_send},
};

// Retourne CMD_INVALID si la commande est invalide (rien ne doit être envoyé).
enum msg_type handle_command(char *buffer)
{
    if (strcmp(buffer, "/quit\n") == 0)
        return MULTICAST_QUIT;
    if (strcmp(buffer, "/who\n") == 0)
        return NICKNAME_LIST;

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
        if (is_command(buffer, commands[i].name))
            return commands[i].parse(buffer);
    return ECHO_SEND;
}

/* ----------------------- Construction du message -------------------------- */

// "<pseudo> <reste>" : le pseudo va dans infos, payload ne garde que le reste
static void split_nickname(struct message *msg, char *payload)
{
    char *space = strchr(payload, ' ');
    if (space == NULL)
        return;
    size_t nick_len = (size_t)(space - payload);
    if (nick_len >= INFOS_LEN)
        nick_len = INFOS_LEN - 1; // évite de déborder de msg->infos
    memcpy(msg->infos, payload, nick_len);
    msg->infos[nick_len] = '\0';
    memmove(payload, space + 1, strlen(space + 1) + 1);
}

// FILE_REQUEST : infos = pseudo du destinataire, payload = nom du fichier (sans guillemets ni dossier)
static void prepare_file_request(struct message *msg, char *payload)
{
    split_nickname(msg, payload);

    size_t path_len = strlen(payload);
    if (path_len >= 2 && payload[0] == '"' && payload[path_len - 1] == '"') {
        memmove(payload, payload + 1, path_len - 2);
        payload[path_len - 2] = '\0';
    }
    // Le chemin complet est mémorisé pour l'envoyer après l'ACCEPT du destinataire
    ft_set_pending_file(payload);
    const char *filename = ft_basename(payload);
    memmove(payload, filename, strlen(filename) + 1);
}

void handle_struct(struct message *msg, enum msg_type type, char *payload)
{
    switch (type) {
    case NICKNAME_LIST:
        payload[0] = '\0'; // commande sans payload
        break;
    case NICKNAME_NEW:
        // Le serveur reçoit uniquement le pseudo (déjà validé par handle_command)
        parse_nickname(payload, msg->infos);
        payload[0] = '\0';
        break;
    case NICKNAME_INFOS:
        snprintf(msg->infos, sizeof(msg->infos), "%s", payload);
        payload[0] = '\0';
        break;
    case UNICAST_SEND:
        split_nickname(msg, payload);
        break;
    case FILE_REQUEST:
        prepare_file_request(msg, payload);
        break;
    default:
        break;
    }

    msg->type = type;
    msg->pld_len = (int)strlen(payload);
}

char *handle_stdin(void)
{
    char buff[MSG_LEN];
    memset(buff, 0, MSG_LEN);
    if (fgets(buff, MSG_LEN, stdin) == NULL)
        return NULL;

    return strdup(buff);
}
