#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <limits.h>

#define PSIZE 1000
#define DEBUG 1

struct header {
    int seqno;
    char fin;
    char ack;
    size_t length;
    short int checksum;
};

struct packet {
    struct header *h;
    char *buffer;
    short int ack;
    /* TODO: Add a time component for when this packet will timeout */
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
    (*h)->checksum = 0;
    (*h)->length = 0;
}

struct packet *initpacket (int bufsize) {
    struct packet *p = malloc (sizeof (struct packet));
    initheader(&(p->h));
    p->buffer = malloc (sizeof (char) * bufsize);
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

void error (char *e) {
    perror (e);
    exit(0);
}

/*Send set of packets beginning at p
 *sockfd: socket number
 *cli_addr: sockaddr_in of client to send packet to
 *addrlen: length of address
 */
void sendpackets(struct packet *p, int sockfd, struct sockaddr_in cli_addr, int addrlen, int *cwndleft) {
    while (p && *cwndleft >= p->h->length) {
        size_t length = p->h->length + sizeof(struct header);
        char buffer[length];
        
        // Fill out the buffer
        memset(buffer, '\0', length);
        memcpy(buffer, p->h, sizeof(struct header));
        memcpy(buffer + sizeof(struct header), p->buffer, p->h->length);
        
        // Send packet
        if (sendto(sockfd, buffer, length, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
            error("Sendto failed");
        printf("Sent %zu bytes\tseqno %i\tfin %i\n", p->h->length, p->h->seqno, p->h->fin);
        *cwndleft -= p->h->length;
        p = p->next;
    }
}

// argv: portnumber, Cwnd, Pl, PC
int main(int argc, char *argv[]) {
    // Socket var
    int sockfd, n, cwnd;
    cwnd = atoi (argv[2]);
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    
    // File var
    FILE *fp;
    size_t f;
    long fsize;

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
                newp->h->length = f;
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
            
            // TODO: Use select() to wait for the ACK from the receiver
            n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
            
            // Populate h with the ACK
            initheader(&h);
            memcpy (h, buffer, hsize);
            printf("Received ACK with seqno %i\n", h->seqno);
            
            // Parse and process the seqno of the ACK
            while (pfirst->h->seqno < h->seqno) {
                printf("Freeing packet with length %zu and seqno %i\n", pfirst->h->length, pfirst->h->seqno);
                cwndleft += pfirst->h->length;
                pfirst = freepacket(&pfirst);
                if (!pfirst)
                    break;
            }
            if (pfirst && pfirst->h->seqno == h->seqno) {
                pfirst->ack++;
                if (pfirst->ack > 1)
                    cwndleft += h->length;
            }
            ackedseqno = (h->seqno > ackedseqno ? h->seqno : ackedseqno);
            
            // TODO: if pfirst has received a duplicate ACK, then resend starting from that point
            
            
            free(h); h = 0;
        } while (ackedseqno < fsize);
    }
    
    fclose(fp);
}
