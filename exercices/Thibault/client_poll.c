#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

int die(int, char *);

struct bank_account{
    char name[128];
    int account_number;
    long long balance;
};

int send_next_message_size(int sockfd, int size){
    int bytes_sent = 0;
    int ret = 0;

    while(bytes_sent != sizeof(int)){
        ret = write(sockfd, (char*) &size + bytes_sent, sizeof(int) - bytes_sent);
        die(ret, "on sending message size");
        bytes_sent += ret;
    }
    return bytes_sent;
};

int send_message(int sockfd, void * buf, int size){
    int bytes_sent = 0;
    int ret = 0;

    while(bytes_sent != size){
        ret = write(sockfd, (char*)buf + bytes_sent, size - bytes_sent);
        die(ret, "on writing message");
        bytes_sent += ret;
    }
    return bytes_sent;
};

// Retourne -1 si le serveur s'est déconnecté ou en cas d'erreur
int recv_next_message_size(int sockfd, int *size){
    int bytes_read = 0;
    int ret = 0;

    while(bytes_read != sizeof(int)){
        ret = read(sockfd, (char*)size + bytes_read, sizeof(int) - bytes_read);
        if(ret <= 0) return -1;
        bytes_read += ret;
    }
    return *size;
}

int recv_message(int sockfd, void *buf, int size){
    int bytes_read = 0;
    int ret = 0;

    while(bytes_read != size){
        ret = read(sockfd, (char*)buf + bytes_read, size - bytes_read);
        if(ret <= 0) return -1;
        bytes_read += ret;
    }
    return bytes_read;
}

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"
#define LINE_SIZE 64

// Index des fds surveillés par poll
enum { IDX_STDIN = 0, IDX_SOCK, NB_FDS };

int die(int ret_value, char * msg){
    if(ret_value < 0){
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return 0;
}

// Traite une ligne saisie par l'utilisateur : envoie la nouvelle balance au serveur
void process_line(char *line, int sockfd, void *data){
    struct bank_account *account = data;
    long long new_balance = -1;
    if(sscanf(line, "%lld", &new_balance) != 1 || new_balance < 0){
        return;
    }

    account->balance = new_balance;
    send_next_message_size(sockfd, sizeof(struct bank_account));
    send_message(sockfd, account, sizeof(struct bank_account));
}

// Lit ce qui est disponible sur stdin et traite les lignes complètes.
// Retourne -1 si stdin est fermée (EOF ou erreur).
int handle_stdin(int sockfd, void *data, void (*process)(char *, int, void *)){
    static char line[LINE_SIZE];
    static size_t line_len = 0;
    char buf[LINE_SIZE];

    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if(n <= 0) return -1;

    for(ssize_t k = 0; k < n; k++){
        if(buf[k] == '\n'){
            line[line_len] = '\0';
            process(line, sockfd, data);
            line_len = 0;
        } else if(line_len < sizeof(line) - 1){
            line[line_len++] = buf[k];
        }
    }
    return 0;
}

// Reçoit une mise à jour du serveur et l'affiche.
// Retourne -1 si le serveur s'est déconnecté.
int handle_server(int sockfd, struct bank_account *account){
    int size = 0;
    struct bank_account update;

    if(recv_next_message_size(sockfd, &size) < 0) return -1;
    if(size != (int)sizeof(update)) return -1;
    if(recv_message(sockfd, &update, size) < 0) return -1;

    *account = update;
    printf("[serveur] Compte %d (%s) : nouvelle balance = %lld\n",
           account->account_number, account->name, account->balance);
    return 0;
}

int main(){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    die(sockfd, "socket creation");

    // Création du struct utilisé dans le connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_aton(SERVER_IP, &server_addr.sin_addr);

    // Connexion au serveur
    int ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    die(ret, "connecting");

    // Poll sur stdin (saisie utilisateur) et sur la socket (notifications serveur)
    struct pollfd fds[NB_FDS];
    fds[IDX_STDIN].fd = STDIN_FILENO;
    fds[IDX_STDIN].events = POLLIN;
    fds[IDX_STDIN].revents = 0;
    fds[IDX_SOCK].fd = sockfd;
    fds[IDX_SOCK].events = POLLIN;
    fds[IDX_SOCK].revents = 0;

    struct bank_account account;
    strcpy(account.name, "John Doe");
    account.account_number = 12345;
    account.balance = 1000;

    printf("Entrez une nouvelle balance :\n");

    while(1){
        int nready = poll(fds, NB_FDS, -1);
        if(nready < 0){
            if(errno == EINTR) continue;
            die(nready, "poll");
        }

        // Saisie utilisateur
        if(fds[IDX_STDIN].revents & (POLLIN | POLLHUP)){
            if(handle_stdin(sockfd, &account, process_line) < 0){
                // stdin fermée : on arrête de la surveiller mais on reste connecté au serveur
                fds[IDX_STDIN].fd = -1;
            }
        }

        // Notification du serveur
        if(fds[IDX_SOCK].revents & (POLLIN | POLLHUP | POLLERR)){
            if(handle_server(sockfd, &account) < 0){
                printf("Serveur déconnecté\n");
                break;
            }
        }
    }

    close(sockfd);
    return 0;
}
