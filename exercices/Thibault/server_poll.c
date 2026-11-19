#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>

int die(int, char *);

struct bank_account{
    char name[128];
    int account_number;
    long long balance;
};

// Compte géré par le serveur, envoyé aux clients à chaque mise à jour
struct bank_account account = {"John Doe", 12345, 1000};

// Envoie la taille du message puis le compte. Retourne -1 si l'écriture échoue
// (client déconnecté), sans quitter le serveur.
int send_account(int fd, struct bank_account *acc){
    int size = sizeof(*acc);
    const char *parts[2] = {(const char *)&size, (const char *)acc};
    int lens[2] = {sizeof(size), size};

    for(int p = 0; p < 2; p++){
        int sent = 0;
        while(sent != lens[p]){
            int ret = write(fd, parts[p] + sent, lens[p] - sent);
            if(ret <= 0) return -1;
            sent += ret;
        }
    }
    return 0;
}

int recv_next_message_size(int sockfd, int *size){
    int bytes_read = 0;
    int ret = 0;

    while(bytes_read != sizeof(int)){
        ret = read(sockfd, (char*)size + bytes_read, sizeof(int) - bytes_read);
        if(ret <= 0) return -1; // erreur ou client déconnecté
        bytes_read += ret;
    }
    return *size; 
}

int recv_message(int sockfd, void *buf, int size){
    int bytes_read = 0;
    int ret = 0;

    while(bytes_read != size){
        ret = read(sockfd, (char*)buf + bytes_read, size - bytes_read);
        if(ret <= 0) return -1; // erreur ou client déconnecté
        bytes_read += ret;
    }
    return bytes_read;
}

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
                // READ USER REQUESTS
                // printf
                // respond to user

                // Exchanging data
                struct bank_account account_tmp;
                int to_read = 0;
                int header_ok = recv_next_message_size(fds[i].fd, &to_read);
                int bytes_read = -1;
                if(header_ok >= 0 && to_read > 0 && to_read <= (int)sizeof(account_tmp)){
                    bytes_read = recv_message(fds[i].fd, &account_tmp, to_read);
                }
                if(bytes_read < 0){
                    // Client déconnecté ou message invalide : on libère le slot
                    printf("Client on fd %d disconnected\n", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    fds[i].events = 0;
                    fds[i].revents = 0;
                    continue;
                }

                printf("Received %d bytes\n", bytes_read);
                printf("Account name: %s\n", account_tmp.name);
                printf("Account number: %d\n", account_tmp.account_number);
                printf("Account balance: %lld\n", account_tmp.balance);

                // Mise à jour de la balance puis confirmation renvoyée uniquement à l'émetteur
                account.balance = account_tmp.balance;
                if(send_account(fds[i].fd, &account_tmp) < 0){
                    printf("Client on fd %d disconnected\n", fds[i].fd);
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    fds[i].events = 0;
                    fds[i].revents = 0;
                }
            } else if (fds[i].fd != listen_fd && (fds[i].revents & POLLHUP)){
                // HANDLE ERRORS
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