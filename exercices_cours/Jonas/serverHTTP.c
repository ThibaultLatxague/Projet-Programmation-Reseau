#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <netdb.h>

#define SERVER_PORT 8080
#define BACKLOG 20
#define MAX_CONN 5

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

int recv_next_message_size(int sockfd, int size)
{
    int bytes_read = 0;
    int ret = 0;

    while (bytes_read != sizeof(int))
    {
        ret = read(sockfd, (char *)&size + bytes_read, sizeof(int) - bytes_read);
        if (ret == 0)
            return 0;
        die(ret, "on reading msg size");
        bytes_read += ret;
    }

    return size;
}

int recv_message(int sockfd, void *buf, int size)
{
    int bytes_read = 0;
    int ret = 0;

    while (bytes_read != size)
    {
        ret = read(sockfd, buf + bytes_read, size - bytes_read);
        if (ret == 0)
            return 0;
        die(ret, "on reading");
        bytes_read += ret;
    }

    return bytes_read;
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
    printf("socket\n");
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    die(listen_fd, "socket creation");

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    // Transforme le port suivant le contexte rsx (big little endian)
    listen_addr.sin_port = htons(SERVER_PORT);
    listen_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    int ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    die(ret, "on setsockopt");

    // Lien port fichier (si comm sur ce port va dans ce fd)
    ret = bind(listen_fd, (struct sockaddr *)&listen_addr, sizeof(listen_addr));
    die(ret, "on binding");

    // Fixe l'entrée à des demande de connection
    ret = listen(listen_fd, BACKLOG);
    die(ret, "on listening");
    int to_read = 0;
    char *httpRequest;

    struct pollfd fds[MAX_CONN] = {0};
    for (int i = 1; i < MAX_CONN; i++)
    {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    while (1)
    {
        printf("1. Will poll...\n");
        int nfds = poll(fds, MAX_CONN, -1);
        die(nfds, "on poll");
        for (int i = 0; i < MAX_CONN; i++)
        {
            if (fds[i].fd == listen_fd && (fds[i].revents & POLLIN))
            {
                printf("2. %d fd active\n", fds[i].fd);
                printf("3. connection asked\n");
                struct sockaddr_in client_addr;
                socklen_t size_addr = sizeof(client_addr);
                printf("4. Will block on accept...\n");
                int new_fd = accept(fds[i].fd, (struct sockaddr *)&client_addr, &size_addr);
                die(new_fd, "on accepting");
                printf("5. New socket on fd (%d)\n", new_fd);
                for (int j = 0; j < MAX_CONN; j++)
                {
                    if (fds[j].fd == -1)
                    {
                        fds[j].fd = new_fd;
                        fds[j].events = POLLIN;
                        fds[j].revents = 0;
                        printf("6. New socked saved\n");
                        break;
                    }
                }
                fds[i].revents = 0;
                printf("\n");
            }
            else if (fds[i].fd != listen_fd && (fds[i].revents & POLLIN))
            {
                printf("2. %d fd active\n", fds[i].fd);
                printf("3. msg received\n");

                to_read = 2048;
                httpRequest = malloc(2048 * sizeof(char));

                ret = read(fds[i].fd, httpRequest, to_read);
                die(ret, "on receiving msg");
                printf("http request received\n");

                char *headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n";
                char* body = "<!DOCTYPE html>\n<html><head>Hello world</head>\n</html>";
                int length = strlen(body);
                char response[30000] = {0};
                sprintf(response, "%sContent-Length: %d\r\n\r\n%s", headers, length, body);
                ret = send_message(fds[i].fd, response, strlen(response));
                die(ret, "on writing msg");
                printf("http response send\n");

                if (ret == 0 && fds[i].fd != -1)
                {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    fds[i].events = 0;
                    fds[i].revents = 0;
                }

                fds[i].revents = 0;
                printf("\n");
            }
            else if (fds[i].fd != listen_fd && (fds[i].revents & POLLHUP))
            {
                printf("2. %d fd active\n", fds[i].fd);
                printf("3. socket closed\n");
                close(fds[i].fd);
                fds[i].fd = -1;
                fds[i].events = 0;
                fds[i].revents = 0;
                printf("\n");
            }
        }
    }

    return EXIT_SUCCESS;
}