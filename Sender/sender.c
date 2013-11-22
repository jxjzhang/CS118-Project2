#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>

#define PSIZE 1000

// argv: portnumber, Cwnd, Pl, PC
int main(int argc, char *argv[]) {
    int sockfd, portno, n;
    size_t f;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    FILE *fp;
    
    char buffer[PSIZE];
    
    portno = atoi(argv[1]);
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror ("Error creating socket");
        return 0;
    }
    
    memset((char *)&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    
    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0) {
        perror("Error on bind");
        return 0;
    };
    
    while(1) {
        addrlen = sizeof(cli_addr);
        n = recvfrom(sockfd, buffer, PSIZE, 0, (struct sockaddr *)&cli_addr, &addrlen);
        buffer[n] = 0;
        printf("Received: %s\n", buffer);
        
        fp = fopen (buffer, "r");
        if (fp == 0)
            printf("Requested file %s does not exist\n", buffer);
        
        f = fread(buffer, 1, PSIZE, fp);
        
        if (sendto(sockfd, buffer, f, 0, (struct sockaddr *)&cli_addr, addrlen) < 0) {
            perror("Sendto failed");
            return 0;
        }
        
        break;
    }
    
    fclose(fp);
}