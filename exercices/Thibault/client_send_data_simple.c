#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
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
        exit(ret_value);
    }
    return 0;
}

int main(){
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    die(fd, "socket creation");

    // Création du struct utilisé dans le connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_aton(SERVER_IP, &server_addr.sin_addr);

    // Connexion au serveur
    int ret = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    die(ret, "connecting");

    struct bank_account account;
    int size_acc = sizeof(struct bank_account);
    int bytes_send = 0;

    while(bytes_send != sizeof(int)){
        ret = write(fd, (char*) &size_acc + bytes_send, sizeof(int));
        die(ret, "Sending message size");
        bytes_send += ret;
    }

    bytes_send = 0;
    printf("Next msg size: %d", size_acc);

    // The send message itself
    while(bytes_send != size_acc){
        ret = write(fd, account + bytes_send, size_acc - bytes_send);
        die(ret, "writing");
        bytes_sent += ret;
    }
    printf("Sended %d bytes\n", bytes_send);

    return 0;
}