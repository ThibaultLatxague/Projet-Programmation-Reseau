#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"

void echo_client(int sockfd) {
	char buff[MSG_LEN];
	char* response;
	int n;
	while (1) {
		// Cleaning memory
		memset(buff, 0, MSG_LEN);
		// Getting message from client
		printf("Message: ");
		n = 0;
		while ((buff[n++] = getchar()) != '\n') {} // trailing '\n' will be sent
		response = handle_message(buff, sockfd);
		printf("Received: %s", response);
	}
}

char* handle_message(char* msg, int sockfd) {
	// Here you can implement any message handling logic if needed
	// For now, it just prints the message
	// Sending message (ECHO)

	if(strcmp(msg, "/quit\n") == 0) {
		send(sockfd, msg, strlen(msg), 0); // Send du quit au serveur pour qu'il termine la connexion avec ce client
		memset(msg, 0, MSG_LEN); // Clean de la mémoire côté client
		close(sockfd); // Close du socket côté client
		exit(EXIT_SUCCESS); // Fermeture de notre connexion
	} else {
		// Sinon cas classique
		if (send(sockfd, msg, strlen(msg), 0) <= 0) {
			break;
		}
		printf("Message sent!\n");
		memset(msg, 0, MSG_LEN);
		if (recv(sockfd, msg, MSG_LEN, 0) <= 0) {
			break;
		}
		printf("Handling message: %s", msg);
	}
	return msg;
}

int handle_connect(char* srv_addr, char* srv_port) {
	struct addrinfo hints, *result, *rp;
	int sfd;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	// Affichage des valeurs de connexion pour les tests et remplacement des #define par les arguments passés par la fonction
	printf("Test: SERV_ADDR = %s\n", srv_addr);
	printf("Test: SERV_PORT = %s\n", srv_port);
	if (getaddrinfo(srv_addr, srv_port, &hints, &result) != 0) {
		perror("getaddrinfo()");
		exit(EXIT_FAILURE);
	}
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,rp->ai_protocol);
		if (sfd == -1) {
			continue;
		}
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
			break;
		}
		close(sfd);
	}
	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}
	freeaddrinfo(result);
	return sfd;
}

int main(int argc, char * argv[]) {
	// Vérification du nombre d'arguments
	// 3 arguments car nom du programme + addresse + port
	if(argc != 3){
		fprintf(stderr, "Usage: %s <server_address> <server_port>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	// Récupération des arguments
	char* server_addr = argv[1];
	char* server_port = argv[2];

	int sfd;
	// Passage des arguments au serveur pour établir la connexion avec DNS
	sfd = handle_connect(server_addr, server_port);
	echo_client(sfd);
	close(sfd);
	return EXIT_SUCCESS;
}

