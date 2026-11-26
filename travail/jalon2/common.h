#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MSG_LEN 1024

// Affiche les erreurs
static inline int die(int ret_value, char *msg)
{
	if (ret_value < 0)
	{
		perror(msg);
		exit(EXIT_FAILURE);
	}

	return 0;
}

/* On envoie d'abord la taille du message (les octets d'un int) pour que le serveur sache quelle ressource préparer
// pour la réception du message. On envoie ensuite le message dans une autre fonction. 
Toutes les fonctions retournent -1 en cas d'erreur ou de déconnexion de l'autre côté.*/

// Envoie de la taille du message
static inline int send_next_message_size(int sockfd, int size)
{
	int bytes_sent = 0;
	int ret = 0;

	while (bytes_sent != sizeof(int))
	{
		ret = write(sockfd, (char *)&size + bytes_sent, sizeof(int) - bytes_sent);
		if (ret <= 0)
			return -1;
		bytes_sent += ret;
	}
	return bytes_sent;
}

// Envoie du message en fonctione de la taille spécifiée
static inline int send_message(int sockfd, void *buf, int size)
{
	int bytes_sent = 0;
	int ret = 0;

	while (bytes_sent != size)
	{
		ret = write(sockfd, (char *)buf + bytes_sent, size - bytes_sent);
		if (ret <= 0)
			return -1;
		bytes_sent += ret;
	}
	return bytes_sent;
}

// Côté serveur, réception de la taille du futur message
static inline int recv_next_message_size(int sockfd, int *size)
{
	int bytes_read = 0;
	int ret = 0;

	while (bytes_read != sizeof(int))
	{
		ret = read(sockfd, (char *)size + bytes_read, sizeof(int) - bytes_read);
		if (ret <= 0)
			return -1;
		bytes_read += ret;
	}
	return *size;
}

// Côté serveur, réception du message en fonction de la taille spécifiée
static inline int recv_message(int sockfd, void *buf, int size)
{
	int bytes_read = 0;
	int ret = 0;

	while (bytes_read != size)
	{
		ret = read(sockfd, (char *)buf + bytes_read, size - bytes_read);
		if (ret <= 0)
			return -1;
		bytes_read += ret;
	}
	return bytes_read;
}

#endif
