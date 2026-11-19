#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int die(int ret_value, char *msg)
{
    if (ret_value < 0)
    {
        perror(msg);
        exit(EXIT_FAILURE);
    }

    return 0;
}


int main()
{
    int sfd, s;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    s = getaddrinfo("www.google.com", "80", &hints, &result);
    die(s, "on getting address info");
    printf("Google info get\n");
    
    for (rp = result; rp != NULL; rp = rp->ai_next)
    {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
        {
            printf("Client connected to Google\n");
            break;
        }

        close(sfd);
    }

    freeaddrinfo(result);

    exit(EXIT_SUCCESS);
}
