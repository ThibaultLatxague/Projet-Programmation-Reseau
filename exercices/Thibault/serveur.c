#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <string.h>

int die(int, char *);

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"
#define BACKLOG 10
#define MAX_CONN 20

int die(int ret_value, char * msg){
    if(ret_value < 0){
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return 0;
}

int main(){
    // Un écrit sur une socket fermée ne doit pas tuer le serveur
    signal(SIGPIPE, SIG_IGN);

    // Init
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    die(listen_fd, "socket creation");
    // Création du struct utilisé dans le connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    // inet_aton(SERVER_IP, &server_addr.sin_addr);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Serveur
    int ret = bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    die(ret, "binding");
    // Ecoute
    ret = listen(listen_fd, BACKLOG);
    die(ret, "listening");

    // Création des polls pour gérer les connexion
    struct pollfd fds[MAX_CONN] = {0};
    for(int i = 1; i < MAX_CONN; i++){
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    while(1){
        int nfds = poll(fds, MAX_CONN, -1);
        printf("Number of fds with events: %d\n", nfds);

        for(int i = 0; i < MAX_CONN; i++){
            // If fd = listen_fd && est-ce que activité
            if(fds[i].fd == listen_fd && (fds[i].revents & POLLIN)){
                // ACCEPT
                // will create new fd
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                printf("Will block on accept...\n");
                int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
                printf("On accepting...\n");
                printf("New socket on fd %d\n", new_fd);
                die(new_fd, "accepting");

                // Insert new fd into fds
                int inserted = 0;
                for(int j = 1; j < MAX_CONN; j++){
                    if(fds[j].fd == -1){
                        fds[j].fd = new_fd;
                        fds[j].events = POLLIN;
                        fds[j].revents = 0;
                        inserted = 1;
                        break;
                    }
                }
                if(!inserted){
                    fprintf(stderr, "Max connections reached, closing new connection\n");
                    close(new_fd);
                }

                // should set back revents to 0
                fds[i].revents = 0;

            } else if (fds[i].fd != listen_fd && (fds[i].revents & POLLIN)){
                // En attente d'un message du navigateur
                // Lors de la première connexion, on doit renvoyer un 200 OK au navigateur
                char message_to_send[500];
                char index[256] = "Hello World";
                int index_len = strlen(index);
                
                snprintf(message_to_send, sizeof(message_to_send),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: %d\r\n"
                    "\r\n"
                    "%s", index_len, index);
                write(fds[i].fd, message_to_send, strlen(message_to_send));

            } else if (fds[i].fd != listen_fd && (fds[i].revents & POLLHUP)){
                // Errors
                printf("Client on fd %d disconnected\n", fds[i].fd);
                close(fds[i].fd);
                fds[i].fd = -1;
                fds[i].events = 0;
                fds[i].revents = 0;
            }
        }
    }

    return 0;
}