#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>

#define PSIZE 1000

struct header {
    int seqno;
    char fin;
    char ack;
};

void initheader (struct header **h) {
    *h = malloc (sizeof (struct header));
    (*h)->seqno = 0;
    (*h)->fin = 0;
    (*h)->ack = 0;
}


// argv: sender hostname, sender portnumber, filename, Pl, PC
int main (int argc, char *argv[]) {
    int sockfd, portno, n, seqno = 0;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    struct header *h;
    FILE *fp;
    size_t hsize = sizeof (struct header);
    
    char buffer[PSIZE + hsize];
    
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
    initheader(&h);
    
    while(!h->fin) {
        n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, NULL, 0);
        buffer[n] = 0;
        memcpy (h, buffer, hsize);
        n -= hsize;
        seqno += n;
        printf("Received: %i bytes with seqno %i\n", n, h->seqno);
        if (n > 0) {
            fwrite(buffer + hsize, 1, n, fp);
        } else {
            printf("Requested file %s did not exist\n", argv[3]);
        }
    }
    
    free(h);
    fclose(fp);
}