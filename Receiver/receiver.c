#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>


// argv: sender hostname, sender portnumber, filename, Pl, PC
int main (int argc, char *argv[]) {
    int sockfd, portno;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    
    portno = atoi(argv[2]);
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Error creating socket");
        return 0;
    }
    
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        perror("No such host");
        return 0;
    }
    
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);
    
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Bind failed");
        return 0;
    }
}