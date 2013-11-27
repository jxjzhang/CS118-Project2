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

#define PSIZE 1000 // Packet size in bytes
#define DEBUG 1
#define PROPDELAYS 0 // Propagation delay in seconds
#define PROPDELAYNS 200000000 // Delay of sending ACK in nanoseconds

struct header {
    int seqno;
    char fin;
    char ack;
    short int checksum;
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


void printtime() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    printf("%lld.%.9ld: ", (long long)ts.tv_sec%100, ts.tv_nsec);
}

// Returns 0 if should send packet, returns 1 if should not
int decideReceive(float p) {
    float num;
    num = (rand() % 100 + 1);
    
    float t=num/100;
    if(t > p) {
        return 0;
    }
    return 1;
}

// argv: sender hostname, sender portnumber, filename, Pl, PC
int main (int argc, char *argv[]) {
	if(argc < 5) error("Expecting 5 arguments: sender hostname, sender portnumber, filename, Pl, PC\n"); 

    int sockfd, n, seqno = 0, r;
    float ploss, pcorrupt;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    struct header *h = 0;
    FILE *fp;
    size_t hsize = sizeof (struct header);
    
    // Randomness setup
    srand(time(NULL));
    ploss = atof(argv[3]);
    pcorrupt = atof(argv[4]);
    
    char buffer[PSIZE + hsize];
    
    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        error ("Error creating socket");
    
    // Populate server information
    server = gethostbyname (argv[1]);
    if (server == NULL)
        error ("No such host");
    memset ((char *)&serv_addr, 0, sizeof (serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy ((void *)&serv_addr.sin_addr, server->h_addr_list[0], server->h_length);
    serv_addr.sin_port = htons(atoi(argv[2]));
    
    
    // Initiate file request from sender
    // TODO: Standardize this request with header (?)
    if (sendto (sockfd, argv[3], strlen(argv[3]), 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
        error ("Sendto failed");
    
    // Open file for writing
    fp = fopen(argv[3], "w");
    
    // Receive packets from sender
    initheader(&h);
    while(!h->fin || !h->ack) {
		// Wait for data from sender
        n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, NULL, 0);
		memcpy (h, buffer, hsize); 
        // Introduce corruption/loss on receiving data
        if (decideReceive(ploss)) {
			if (DEBUG) printtime();
            printf("Loss: Ignoring %i bytes with seqno %i\n", n - hsize, h->seqno);
			h->fin = 0;
        } else if (decideReceive(pcorrupt)) {
			if (DEBUG) printtime();
            printf("Corruption: Ignoring %i bytes with seqno %i\n", n - hsize, h->seqno);
			h->fin = 0;
        } else {
            n -= hsize;
        	if (DEBUG) printtime();
            printf("Received: %i bytes with seqno %i, checksum %i\n", n, h->seqno, (short int)(h->checksum));
        
            // Received next seqno
            if (seqno == h->seqno) {
                seqno += n;
                if (n > 0) {
                    fwrite(buffer + hsize, 1, n, fp);
                
                    //Calculate Checksum
                    printf("Calculated Checksum: %i\n",(short int)calcChecksum(buffer + hsize,n));
                
                    // Send ACK
					struct header *outh; // Outgoing header
					initheader(&outh);
                    outh->seqno = seqno;
                    outh->ack = 1;
					outh->fin = h->fin;
                    nanosleep((struct timespec[]){{PROPDELAYS, PROPDELAYNS}}, NULL);
                    if (sendto (sockfd, h, sizeof(struct header), 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
                        error ("Sendto failed");
					if (DEBUG) printtime();
                    printf("Sending ACK with seqno %i\n", outh->seqno);
					if (outh->fin) {
						if(DEBUG) printtime();
						printf("File complete. FIN was requested\n");
						fclose(fp);
					}
					free(outh); outh = 0;
                } else if (h->ack && h->fin) {
					if(DEBUG) printtime();
					printf("Received fin-ack from sender. Done!\n");
					// TODO: Send back an ACK to sender
					break;
				} else {
                    printf("Requested file %s did not exist or had no data\n", argv[3]);
					fclose(fp);
				}
            } else {
                // Send out an ACK requesting the expected seqno
		        h->seqno = seqno;
		        h->ack = 1;
		        nanosleep((struct timespec[]){{PROPDELAYS, PROPDELAYNS}}, NULL);
				if (DEBUG) printtime();
                printf("Ignoring packet; expected seqno %i; resending ACK\n", seqno);
		        if (sendto (sockfd, h, sizeof(struct header), 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
		            error ("Sendto failed");
                h->fin = 0;
            }
        }
    }
    
    free(h); h = 0;
}
