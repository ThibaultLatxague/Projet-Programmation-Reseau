#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>

#define SERVER_PORT 8080

struct obj
{
    char Name[128];
    int Code;
};

// Affiche les erreurs
int die(int ret_value, char *msg)
{
    if (ret_value < 0)
    {
        perror(msg);
        exit(EXIT_FAILURE);
    }

    return 0;
}

int send_next_message_size(int sockfd, int size)
{
    int bytes_sent = 0;
    int ret = 0;

    while (bytes_sent != sizeof(int))
    {
        ret = write(sockfd, (char *)&size + bytes_sent, sizeof(size) - bytes_sent);
        die(ret, "on writing msg size");
        bytes_sent += ret;
    }
    
    return bytes_sent;
}

int send_message(int sockfd, void *buf, int size)
{
    int bytes_sent = 0;
    int ret = 0;

    while (bytes_sent != size)
    {
        ret = write(sockfd, buf + bytes_sent, size - bytes_sent);
        die(ret, "on writing");
        bytes_sent += ret;
    }

    return bytes_sent;
}

int main()
{
    // Ouvre une socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    die(fd, "socket creation");

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    // Transforme le port suivant le contexte rsx (big little endian)
    server_addr.sin_port = htons(SERVER_PORT);

    // Transforme l'adresse suivant le contexte rsx (big little endian)
    int ret = inet_aton("127.0.0.1", &(server_addr.sin_addr));
    die(ret, "on filling socket");

    // Connecte au serveur
    ret = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    die(ret, "on connecting");

    // Envoi de data
    char *msg = "Bonjour";
    struct obj objet;
    objet.Code = 28;
    strcpy(objet.Name, msg);
    int msg_size = sizeof(struct obj);
    //int bytes_sent = 0;

    // Envoie de la taille du msg
    /*while (bytes_sent != sizeof(int))
    {
        ret = write(fd, (char *)&msg_size + bytes_sent, sizeof(msg_size) - bytes_sent);
        die(ret, "on writing msg size");
        bytes_sent += ret;
    }

    bytes_sent = 0;*/

    // Envoie du msg
    /*while (bytes_sent != msg_size)
    {
        ret = write(fd, &objet + bytes_sent, msg_size - bytes_sent);
        die(ret, "on writing");
        bytes_sent += ret;
    }*/

    ret = send_next_message_size(fd, msg_size);
    die(ret, "on sending length");
    ret = send_message(fd, (void*)&objet, msg_size);
    die(ret, "on sending msg");

    return EXIT_SUCCESS;
}