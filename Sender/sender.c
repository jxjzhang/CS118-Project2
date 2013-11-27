#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <limits.h>
#include <time.h>

#define PSIZE 1000
#define DEBUG 1
#define TIMEOUTS 2
#define TIMEOUTUS 0

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
    struct timespec ts;
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

void printtime() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    printf("%lld.%.9ld: ", (long long)ts.tv_sec%100, ts.tv_nsec);
}

struct timespec diff(struct timespec start, struct timespec end)
{
	struct timespec temp;
	if((end.tv_nsec - start.tv_nsec)<0) {
		temp.tv_sec = end.tv_sec - start.tv_sec-1;
		temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec = end.tv_sec-start.tv_sec;
		temp.tv_nsec = end.tv_nsec-start.tv_nsec;
	}
	return temp;
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
    	clock_gettime(CLOCK_REALTIME, &p->ts);
		if(DEBUG) printtime();
        printf("Sent %zu bytes\tseqno %i\tfin %i\tts %lld.%.9ld\n", p->length, p->h->seqno, p->h->fin, (long long)p->ts.tv_sec%100, p->ts.tv_nsec);
        *cwndleft -= p->length;
        p = p->next;
    }
}

// Returns 0 if should send packet, returns 1 if should not
int decideReceive(float p) {
    float num;
    num = (rand() % 100 + 1);
    
    float t=num/100;
    if (t > p) {
        return 0;
    }
    return 1;
}

void settimeout(struct timeval *timeout, struct timespec origin) {
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	struct timespec d = diff(origin, now);
	struct timespec t; 
	t.tv_sec = TIMEOUTS;
	t.tv_nsec = TIMEOUTUS * 1000;
	t = diff(d, t);
	timeout->tv_sec = t.tv_sec;
	timeout->tv_usec = t.tv_nsec/1000;
}

// argv: portnumber, Cwnd, Pl, PC
int main(int argc, char *argv[]) {
	if(argc < 5) error("Expecting 5 arguments: portnumber, Cwnd, Pl, PC\n"); 	
	
    // Socket var
    int sockfd, n, cwnd, maxfdp;
    cwnd = atoi(argv[2]);
	if (cwnd < PSIZE) error("cwnd cannot be smaller than packet size\n");
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t addrlen;
    fd_set masterset, rset;
    
    // Timeout setup
    struct timeval timeout;
	FD_ZERO(&masterset);
    FD_ZERO(&rset);

    // File var
    FILE *fp;
    size_t f;
    long fsize;
    
    // Corruption/loss setup
    srand(time(NULL));
    float ploss = atof(argv[3]);
    float pcorrupt = atof(argv[4]);
    
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
	if(DEBUG) printtime();
    printf("Received file request for: %s\n", buffer);
    
    fp = fopen (buffer, "r");
    if (fp == 0) { // fopen fails
        printf("Requested file %s does not exist\n", buffer);
        initheader(&h);
        h->fin = 1;
        memcpy (buffer, h, hsize);
        buffer[hsize] = 0;
        if(sendto(sockfd, buffer, hsize, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
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
        FD_SET(sockfd, &masterset);
        maxfdp = sockfd + 1;
		initheader(&h);
        
        do {
            if (DEBUG) printf("cwndleft:\t%i\n", cwndleft);
            if (DEBUG) printf("sentseqno:\t%i\n", sentseqno);
            
            while(cwndleft > 0 && sentseqno < fsize) {
                // Populate pfirst thru plast for available bytes in cwnd
                f = PSIZE < cwndleft ? PSIZE : cwndleft;
                f = fread(buffer, 1, f, fp);
				if (DEBUG) printtime();
                if (DEBUG) printf("Read %zu bytes from file\n", f);
                
                struct packet *newp = initpacket(f);
                memcpy(newp->buffer, buffer, f); // Store file contents in packet buffer
                newp->h->checksum = calcChecksum(newp->buffer,f);
                newp->length = f;
                newp->h->seqno = sentseqno;
                if(f + sentseqno >= fsize)
                    newp->h->fin = 1;
				if(DEBUG) printtime();
                if(DEBUG) printf("New packet instantiated with seqno %i\n", newp->h->seqno);
                
                // Keep track of the linked list pointers
                if(!pfirst)
                    pfirst = newp;
                if(plast)
                    plast->next = newp;
                plast = newp;
                
                // Send out packets
                sendpackets(newp, sockfd, cli_addr, addrlen, &cwndleft);
                sentseqno += f;
            }
            
            // TODO: Set select() timeout = pfirst's timeout. Resend if times out
			rset = masterset;
			if (!pfirst) {
				timeout.tv_sec = TIMEOUTS;
				timeout.tv_usec = TIMEOUTUS;
			} else { // Set the timeout appropriately for oldest unACKed packet (pfirst)
				settimeout(&timeout, pfirst->ts);
				printtime();
				printf("Setting timeout to %lld.%.6ld for data packet seqno %i\n", (long long)timeout.tv_sec%100, timeout.tv_usec, pfirst->h->seqno);
			}
            if((n = select(maxfdp, &rset, NULL, NULL, &timeout)) < 0)
                error("Select failed");
            if(n == 0) {
				if(DEBUG) printtime();
                printf("Timer expired! Resending appropriate packets\n");
                // Send out packets
                cwndleft = cwnd;
                sendpackets(pfirst, sockfd, cli_addr, addrlen, &cwndleft);
            } /* timer expires */
            if(FD_ISSET(sockfd, &rset)) { /* socket is readable */
                // Populate h with the ACK
            	n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, (struct sockaddr *)&cli_addr, &addrlen);
				memcpy(h, buffer, hsize);
                if(decideReceive(ploss)) {
					if(DEBUG) printtime();
                    printf("Loss: ignoring ACK for seqno %i\n", h->seqno);
                    // Receive ACK, but do nothing
                } else if(decideReceive(pcorrupt)) {
					if(DEBUG) printtime();
                    printf("Corruption: ignoring ACK for seqno %i\n", h->seqno);
                    // Receive ACK, but do nothing
                } else {
					if(DEBUG) printtime();
                    printf("Received ACK\tseqno %i\n", h->seqno);
                
                    // Parse and process the seqno of the ACK
                    // Optional TODO: (sel repeat): currently assumes cumulative ACK
                    while(pfirst->h->seqno < h->seqno) {
						if(DEBUG) printtime();
                        printf("Freeing packet with length %zu and seqno %i\n", pfirst->length, pfirst->h->seqno);
                        cwndleft += pfirst->length;
                        pfirst = freepacket(&pfirst);
                        if (!pfirst) {
							plast = 0;
                            break;
						}
                    }
                    if (pfirst && pfirst->h->seqno == h->seqno) {
                        pfirst->ack++;
                    }
                    ackedseqno = (h->seqno > ackedseqno ? h->seqno : ackedseqno);
                
                    // Optional TODO (sel repeat): if any packet has received 3rd dupe ACK, then rtxmit
                }

            }
        } while(ackedseqno < fsize);


		// TODO: Gracefully handle closing the connection. Still need to wait for ACK from Receiver after this
        // Send FIN-ACK packet
		h->ack = 1;
		h->fin = 1;
        memcpy(buffer, h, hsize);
        buffer[hsize] = 0;
        if (sendto(sockfd, buffer, hsize, 0, (struct sockaddr *)&cli_addr, addrlen) < 0)
            error("Sendto failed");
		if(DEBUG) printtime();
        printf("Sent fin-ack\n");
    }
    free(h); h = 0;
    if (fp) fclose(fp);
}
