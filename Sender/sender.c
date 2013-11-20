#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>

// argv: portnumber, Cwnd, Pl, PC
int main (int argc, char *argv[]) {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    
    char buffer[1000];
    
    portno = atoi(argv[1]);
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("Error creating socket");
        return 0;
    }
    
    memset ((char *)&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    
    if (bind (sockfd, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0) {
        perror ("Error on bind");
        return 0;
    };
    
    while (1) {
        addrlen = sizeof(cli_addr);
        n = recvfrom (sockfd, buffer, 1000, 0, (struct sockaddr *)&cli_addr, &addrlen);
        buffer[n] = 0;
        printf("Received: %s\n", buffer);
    }
}