#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <limits.h>

#define PSIZE 1000

struct header {
    int seqno;
    char fin;
    char ack;
    short int checksum;
};

struct packet {
    struct header *h;
    char *buffer;
    clock_t time;
    struct packet *next;
};

short int calcChecksum(char *buf,int len) {
    int sum=0;
    int count=0;
    char *tmp=buf;
    short int finalcheck;
    //Find the sum of all values
    short int temp=*((short int *)tmp);
    while (1) {
        short int litEadian=(short int)(temp << 8) | (temp >> (sizeof(temp)*CHAR_BIT - 8));
        sum=sum+litEadian;
        tmp++;
        tmp++;
        temp=*((short int *) tmp);
        count=count+2;
        if (count>=(len-1)) {
            break;
        }
    }
    //only has one character left to add to sum
    if ((count+1)==len) {
        sum=sum+*(tmp);
    }
    
    while (sum>>16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    //Convert to 1s complement
    finalcheck=~sum;
    
    return finalcheck;
    
}

void initheader (struct header **h) {
    *h = malloc (sizeof (struct header));
    (*h)->seqno = 0;
    (*h)->fin = 0;
    (*h)->ack = 0;
    (*h)->checksum=0;
}

void initpacket (struct packet **p, int bufsize) {
    *p = malloc (sizeof (struct packet));
    initheader(&(*p)->h);
    (*p)->buffer = malloc (sizeof (char) * bufsize);
    (*p)->next = 0;
}

void freepacket (struct packet **p) {
    free((*p)->h);
    free((*p)->buffer);
    free(*p);
}

void error (char *e) {
    perror (e);
    exit(0);
}

/*Send single packet with all bytes to be send in buffer
 *buffer: character array which holds bytes to be send
 *sockfd: socket number
 *size: number of bytes to be sent
 *cli_addr: sockaddr_in of client to send packet to
 *addrlen: length of address
 */
int sendpacket(char * buffer,int sockfd,int size,struct sockaddr_in cli_addr,int addrlen) {
    
    if (sendto(sockfd, buffer, size, 0, (struct sockaddr *)&cli_addr, addrlen) < 0) {
        error("Sendto failed");
        return 0;
    }
    return 1;
}

// argv: portnumber, Cwnd, Pl, PC
int main(int argc, char *argv[]) {
    int sockfd, n, cwnd, sentseqno = 0, ackedseqno = 0;
    cwnd = atoi (argv[2]);
    size_t f;
    long fsize;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    FILE *fp;
    struct header *h;
    size_t hsize = sizeof (struct header);
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
        
        int cwndleft = cwnd;
        do {
            initheader(&h);
            size_t f;
            size_t hsize = sizeof (struct header);

            while (cwndleft > 0 && sentseqno < fsize) {
                h->seqno = sentseqno;
                // read file chunk into buffer
                
                f = fread(buffer + hsize, 1, (PSIZE < cwndleft ? PSIZE : cwndleft), fp);
                h->checksum = calcChecksum(buffer + hsize,f);
                
                sentseqno += f;
                if (sentseqno >= fsize)
                    h->fin = 1;
                // prepend header
                memcpy (buffer, h, hsize);
                
                //send packet
                if (sendto(sockfd, buffer, f + hsize, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
                    error("Sendto failed");
                cwndleft -= f;
                printf("Sent %lu bytes with seqno %i, checksum %i, fin %i; cwnd remaining: %i\n", f, h->seqno, h->checksum, (int)h->fin, cwndleft);
            }

            // receive ACK
            n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
            memcpy (h, buffer, hsize);
            printf("Received ACK with seqno %i\n", h->seqno);
            if (h->seqno > ackedseqno) {
                cwndleft += h->seqno - ackedseqno;
                ackedseqno = h->seqno;
                if (sentseqno != ftell (fp))
                    fseek (fp, sentseqno, SEEK_SET);
                printf("New cwnd between %i and %i; remaining cwnd %i\n", ackedseqno, ackedseqno + cwnd, cwndleft);
            }
            
            free(h);
        } while (ackedseqno < fsize);
    }
    
    fclose(fp);
}
