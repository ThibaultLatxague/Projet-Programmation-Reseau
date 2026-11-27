#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <stddef.h>

/* Dossier de réception des fichiers*/
#define INBOX_DIR ".resa1/inbox"

/* Nom de fichier (dernier élément d'un chemin, séparateurs '/' ou '\') */
const char *ft_basename(const char *path);

/* Fichier que l'on propose d'envoyer, mémorisé jusqu'à la réponse du destinataire */
void ft_set_pending_file(const char *path);
void ft_clear_pending_file(void);

/* Côté récepteur : socket d'écoute temporaire (IPv4 ou IPv6, suivant la connexion au serveur).
 * ft_open_listener écrit l'adresse annoncée ("IP:port" ou "[IPv6]:port") dans out. */
int ft_open_listener(int server_fd, char *out, size_t out_size);
int ft_listener_fd(void);
void ft_close_listener(void);

/* Côté récepteur : accepte l'émetteur sur la socket d'écoute, reçoit le fichier,
 * puis ferme la socket d'écoute. Retourne 0 en cas de succès, -1 sinon. */
int ft_accept_and_receive(void);

/* Côté émetteur : se connecte à l'adresse annoncée par le récepteur et envoie le
 * fichier en attente. Retourne 0 en cas de succès, -1 sinon. */
int ft_send_pending_file(const char *address);

#endif // FILE_TRANSFER_H
