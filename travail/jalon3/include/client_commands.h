#ifndef CLIENT_COMMANDS_H
#define CLIENT_COMMANDS_H

#include "msg_struct.h"

// Valeur retournée par handle_command quand la commande est invalide
#define CMD_INVALID ((enum msg_type)-1)

// Détermine le type de message correspondant à la saisie (le buffer peut être réduit à ses arguments)
enum msg_type handle_command(char *buffer);

// Remplit msg (type, infos, pld_len) et adapte payload à ce que le serveur attend
void handle_struct(struct message *msg, enum msg_type type, char *payload);

// Lit une ligne sur stdin (allouée, à libérer) ; NULL en cas de fin de saisie
char *handle_stdin(void);

#endif // CLIENT_COMMANDS_H
