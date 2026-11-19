#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct bank_account{
    char name[128];
    int account_number;
    long long balance;
};

#define SERVER_PORT 8080
#define SERVER_IP "127.0.0.1"
#define BACKLOG 10

int die(int ret_value, char * msg){
    if(ret_value < 0){
        perror(msg);
        exit(EXIT_FAILURE);
    }
    return 0;
}

int main(){
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

    // Accept
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    printf("Will block on accept...\n");
    int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    printf("On accepting...");
    printf("New socket on fd %d\n", new_fd);
    die(new_fd, "accepting");

    // Exchanging data
    struct bank_account account_tmp;
    int size_acc = sizeof(struct bank_account);

    int to_read = 0;
    int bytes_read = 0;

    while(bytes_read != sizeof(int)){
        ret = read(new_fd, &to_read + bytes_read, sizeof(int) - bytes_read);
        die(ret, "reading");
        bytes_read += ret;
    }

    // Reading the actuel message
    printf("Size of next message: %d\n", to_read);
    bytes_read = 0;
    char *msg = malloc(sizeof(char) * to_read);

    while(bytes_read != to_read){
        ret = read(new_fd, msg + bytes_read, to_read - bytes_read);
        die(ret, "reading");
        bytes_read += ret;
    }
    printf("Received %d bytes\n", bytes_read);

    return 0;
}