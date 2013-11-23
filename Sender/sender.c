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

void error (char *e) {
    perror (e);
    exit(0);
}

// argv: portnumber, Cwnd, Pl, PC
int main(int argc, char *argv[]) {
    int sockfd, n, cwnd, seqno = 0;
    cwnd = atoi (argv[2]);
    size_t f;
    long fsize;
    struct header *h;
    size_t hsize = sizeof (struct header);
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    FILE *fp;
    
    char buffer[PSIZE + sizeof(struct header)];
    
    // Make socket
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        error ("Error creating socket");

    
    // Populate server address info
    memset((char *)&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(atoi (argv[1]));
    
    // Bind socket
    if (bind (sockfd, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
        error("Error on bind");
    
    // Obtain file request
    // TODO: Standardize request to use header(?)
    addrlen = sizeof(cli_addr);
    n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
    buffer[n] = 0;
    printf("Received file request for: %s\n", buffer);
    
    fp = fopen (buffer, "r");
    if (fp == 0) { // fopen fails
        printf("Requested file %s does not exist\n", buffer);
        initheader(&h);
        h->fin = 1;
        memcpy (buffer, h, hsize);
        buffer[hsize] = 0;
        if (sendto(sockfd, buffer, f + hsize, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
            error("Sendto failed");
        free(h);
    } else {
        // get file size
        fseek (fp, 0, SEEK_END);
        fsize = ftell (fp);
        rewind (fp);
        
        do {
            initheader(&h);
            h->seqno = seqno;
            
            // read file chunk into buffer
            f = fread(buffer + hsize, 1, PSIZE, fp);
            if (f + seqno >= fsize)
                h->fin = 1;
            // prepend header
            memcpy (buffer, h, hsize);
            
            if (sendto(sockfd, buffer, f + hsize, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
                error("Sendto failed");
            
            // receive ACK
            n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
            memcpy (h, buffer, hsize);
            if (h->seqno > seqno) {
                seqno = h->seqno;
                if (seqno != ftell (fp))
                    fseek (fp, seqno, SEEK_SET);
            }
            
            free(h);
        } while (seqno < fsize);
    }
    
    fclose(fp);
}