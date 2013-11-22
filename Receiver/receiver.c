#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>

#define PSIZE 1000

struct header {
    int sequence;
};


// argv: sender hostname, sender portnumber, filename, Pl, PC
int main (int argc, char *argv[]) {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    FILE *fp;
    
    char buffer[PSIZE];
    
    portno = atoi(argv[2]);
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("Error creating socket");
        return 0;
    }
    
    server = gethostbyname (argv[1]);
    if (server == NULL) {
        perror ("No such host");
        return 0;
    }
    
    memset ((char *)&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy ((void *)&serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(portno);
    
    n = strlen(argv[3]);
    if (sendto (sockfd, argv[3], n, 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0) {
        perror ("Sendto failed");
        return 0;
    }
    
    fp = fopen(argv[3], "w");
    
    while(1) {
        n = recvfrom(sockfd, buffer, PSIZE, 0, NULL, 0);
        buffer[n] = 0;
        fwrite(buffer, 1, n, fp);
        break;
    }
    
    fclose(fp);
}