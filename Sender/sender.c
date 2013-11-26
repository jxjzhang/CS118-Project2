#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <limits.h>
#include <sys/time.h>

#define PSIZE 1000
#define DEBUG 1
#define TIMEOUTS 2
#define TIMEOUTMS 0

struct header {
    int seqno;
    char fin;
    char ack;
    short int checksum;
};

struct packet {
    struct header *h;
    char *buffer;
    short int ack; // Not used for Go-Back-N
    size_t length;
    /* TODO: Add a time component for when this packet will timeout */
    struct packet *next;
};

void error (char *e) {
    perror (e);
    exit(0);
}

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

void initheader(struct header **h) {
    *h = malloc(sizeof(struct header));
    if(*h == NULL)
        error("Malloc failure in initheader\n");
    (*h)->seqno = 0;
    (*h)->fin = 0;
    (*h)->ack = 0;
    (*h)->checksum = 0;
}

struct packet *initpacket(int bufsize) {
    struct packet *p = malloc(sizeof(struct packet));
    if(p == NULL)
        error("Malloc failure in initpacket: packet\n");
    initheader(&(p->h));
    p->buffer = malloc(sizeof(char) * bufsize);
    if(p->buffer == NULL)
        error("Malloc failure in initpacket: buffer\n");
    p->ack = 0;
    p->next = 0;
    return p;
}

struct packet *freepacket (struct packet **p) {
    struct packet *next = (*p)->next;
    free((*p)->h);
    free((*p)->buffer);
    free(*p);
    *p = 0;
    return next;
}

/*Send set of packets beginning at p
 *sockfd: socket number
 *cli_addr: sockaddr_in of client to send packet to
 *addrlen: length of address
 */
void sendpackets(struct packet *p, int sockfd, struct sockaddr_in cli_addr, int addrlen, int *cwndleft) {
    while (p && *cwndleft >= p->length) {
        size_t length = p->length + sizeof(struct header);
        char buffer[length];
        
        // Fill out the buffer
        memset(buffer, '\0', length);
        memcpy(buffer, p->h, sizeof(struct header));
        memcpy(buffer + sizeof(struct header), p->buffer, p->length);
        
        // Send packet
        if (sendto(sockfd, buffer, length, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
            error("Sendto failed");
        printf("Sent %zu bytes\tseqno %i\tfin %i\n", p->length, p->h->seqno, p->h->fin);
        *cwndleft -= p->length;
        p = p->next;
    }
}

/*
 *Returns 0 if should send packet, returns 1 if should not
 */
int decideReceive(float ploss) {
    
    float num;
    num=(rand() % 100 + 1);
    
    float t=num/100;
    if (t>ploss) {
        return 0;
    }
    return 1;
}

// argv: portnumber, Cwnd, Pl, PC
int main(int argc, char *argv[]) {
    // Socket var
    int sockfd, n, cwnd, maxfdp;
    float ploss,pcorrupt;
    cwnd = atoi(argv[2]);
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    fd_set rset;
    ploss=atof(argv[3]);
    pcorrupt=atof(argv[4]);
    
    // Timeout setup
    struct timeval timeout;
    timeout.tv_sec = TIMEOUTS;
    timeout.tv_usec = TIMEOUTMS;
    FD_ZERO(&rset);
    struct timespec ts;
    
    // File var
    FILE *fp;
    size_t f;
    long fsize;
    
    //Set up random value
    srand(time(NULL));
    
    // Packet related var
    struct header *h;
    size_t hsize = sizeof (struct header);
    char buffer[PSIZE + hsize]; // scratch buffer sized for packet payload + header size
    struct packet *pfirst = 0, *plast = 0;
    int ackedseqno = 0, sentseqno = 0;
    
    // Make socket
    if((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        error ("Error creating socket");
    
    // Populate server address info
    memset((char *)&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(atoi (argv[1]));
    
    // Bind socket
    if(bind(sockfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0)
        error("Error on bind");
    
    // Obtain file request
    // TODO: Standardize request to use header(?)
    // TODO: Use select(?)
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
        if(sendto(sockfd, buffer, f + hsize, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
            error("Sendto failed");
        free(h); h = 0;
    } else {
        // Get file size
        fseek(fp, 0, SEEK_END);
        fsize = ftell(fp);
        rewind(fp);
        printf("Filesize of %s: %lu\n", buffer, fsize);
        
        int cwndleft = cwnd;
        size_t f;
        
        // select setup
        FD_SET(sockfd, &rset);
        maxfdp = sockfd + 1;
        
        do {
            if (DEBUG) printf("cwndleft:\t%i\n", cwndleft);
            if (DEBUG) printf("sentseqno:\t%i\n", sentseqno);
            
            while (cwndleft > 0 && sentseqno < fsize) {
                // Populate pfirst thru plast for available bytes in cwnd
                f = PSIZE < cwndleft ? PSIZE : cwndleft;
                f = fread(buffer, 1, f, fp);
                if (DEBUG) printf("Read %lu bytes from file\n", f);
                
                struct packet *newp = initpacket(f);
                memcpy(newp->buffer, buffer, f); // Store file contents in packet buffer
                newp->h->checksum = calcChecksum(newp->buffer,f);
                newp->length = f;
                newp->h->seqno = sentseqno;
                if (f + sentseqno >= fsize)
                    newp->h->fin = 1;
                if (DEBUG) printf("New packet instantiated with seqno %i\n", newp->h->seqno);
                
                // Keep track of the linked list pointers
                if (!pfirst)
                    pfirst = newp;
                if (plast)
                    plast->next = newp;
                plast = newp;
                
                // Send out packets
                sendpackets(newp, sockfd, cli_addr, addrlen, &cwndleft);
                sentseqno += f;
            }
            
            // TODO: Set select() timeout = pfirst's timeout. Resend if times out
            if((n = select(maxfdp, &rset, NULL, NULL, &timeout)) < 0)
                error("select failed");
            if (n == 0) {
                printf("Timer expired! TODO: Resend appropriate packets\n");
                // Send out packets
                cwndleft=cwnd;
                sendpackets(pfirst, sockfd, cli_addr, addrlen, &cwndleft);
            } /* timer expires */
            
            if(DEBUG) printf("Return value from select: %i\n", n);
            
            if(FD_ISSET(sockfd, &rset)) { /* socket is readable */
                if(DEBUG) printf("sockfd is readable\n");
                if (decideReceive(ploss)||decideReceive(pcorrupt)) {
                    printf("Triggering ACK ignore because of either loss or corruption\n");
                    n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
                    //Recieve ACK, but do nothing with it
                }else {
                    n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
                    if (DEBUG) printf("Return value from recvfrom: %i\n", n);
                
                    // Populate h with the ACK
                    initheader(&h);
                    memcpy(h, buffer, hsize);
                    printf("Received ACK\tseqno %i\n", h->seqno);
                
                    // Parse and process the seqno of the ACK
                    // Optional TODO: (sel repeat): currently assumes cumulative ACK
                    while (pfirst->h->seqno < h->seqno) {
                        printf("Freeing packet with length %zu and seqno %i\n", pfirst->length, pfirst->h->seqno);
                        cwndleft += pfirst->length;
                        pfirst = freepacket(&pfirst);
                        if (!pfirst)
                            break;
                    }
                    if (pfirst && pfirst->h->seqno == h->seqno) {
                        pfirst->ack++;
                    }
                    ackedseqno = (h->seqno > ackedseqno ? h->seqno : ackedseqno);
                
                    // Optional TODO (sel repeat): if any packet has received 3rd dupe ACK, then rtxmit
                    free(h); h = 0;
                }
            }
        } while(ackedseqno < fsize);
    }
    
    fclose(fp);
}
