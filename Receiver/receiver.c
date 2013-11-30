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
#define TIMEOUTS 2
#define TIMEOUTUS 0

struct header {
    int seqno;
    char fin;
    char ack;
	char syn;
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
	(*h)->syn = 0;
    (*h)->checksum = 0;
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
    
	// Initiate Connection (3-way handshake to establish connection)
	fd_set masterset, rset;
	FD_ZERO(&masterset);
	FD_ZERO(&rset);
	FD_SET(sockfd, &masterset);
    int maxfdp = sockfd + 1;
	
	//Init Header for SYN request
	struct header *synh; 
	initheader(&synh);
    synh->seqno = 0;
    synh->ack = 0;
	synh->syn = 1;

	struct timespec now;
	struct timeval timeout;

	do {
		synh->seqno = 0;
		synh->ack = 0;
		synh->syn = 1;
		nanosleep((struct timespec[]){{PROPDELAYS, PROPDELAYNS}}, NULL);
		clock_gettime(CLOCK_REALTIME, &now);
		settimeout(&timeout, now);
		memcpy(buffer, synh, hsize);
        buffer[hsize] = 0;

		rset = masterset;
		if (sendto (sockfd, buffer, hsize, 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
			error ("Sendto failed");
		if (DEBUG) printtime();
			printf("Sent SYN at %lld.%.9ld\n", (long long)now.tv_sec%100, now.tv_nsec);
    
		if((n = select(maxfdp, &rset, NULL, NULL, &timeout)) < 0)
			error("Select failed");
		if(n == 0) {
			if(DEBUG) printtime();
				printf("Timer expired. Resending SYN\n");//Need to resend SYN, must have been lost
		} else if(FD_ISSET(sockfd, &rset)) {
			//SYN has been received
			n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, NULL, 0);
			//Decide if it got lost or not
			if (decideReceive(ploss)) {
				if (DEBUG) printtime();
				printf("Loss on SYNACK: Ignoring %i bytes with seqno %i\n", n - hsize, synh->seqno);
			 } else if (decideReceive(pcorrupt)) {
				if (DEBUG) printtime();
				printf("Corruption on SYNACK: Ignoring %i bytes with seqno %i\n", n - hsize, synh->seqno);
			} else {
				printf("Sending File Request\n");
				//Send file and final ACK here
				// Initiate file request from sender
				size_t length = strlen(argv[3]) + sizeof(struct header);
				synh->ack = 1;
				synh->syn = 0;
				// Fill out the buffer
				memset(buffer, '\0', length);
				memcpy(buffer, synh, sizeof(struct header));
				memcpy(buffer + sizeof(struct header), argv[3], strlen(argv[3]));

				if (sendto (sockfd, buffer, length, 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
					error ("Sendto failed");
				
				n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, NULL, 0);
				if((n-hsize)>0) {
					//Recieved the first packet of data so 3 way handshake was successful
					break;
				}
			 }
		}
	} while (1);
    
    // Open file for writing
    fp = fopen(argv[3], "w");

    // Receive packets from sender
    initheader(&h);
	int init=0;
    while(!h->fin || !h->ack) {
		// Wait for data from sender
		if (init!=0) {
			n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, NULL, 0);
		}
		init=1;
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
                    if (sendto (sockfd, outh, sizeof(struct header), 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
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
					// Send back an ACK to sender
					h->seqno = 0;
					h->ack = 1;

					fd_set masterset, rset;
					FD_ZERO(&masterset);
					FD_ZERO(&rset);
					FD_SET(sockfd, &masterset);
					int maxfdp = sockfd + 1;
					do {
		                nanosleep((struct timespec[]){{PROPDELAYS, PROPDELAYNS}}, NULL);
						struct timespec now;
						struct timeval timeout;
						clock_gettime(CLOCK_REALTIME, &now);
		                if (sendto (sockfd, h, sizeof(struct header), 0, (struct sockaddr *)&serv_addr, sizeof (serv_addr)) < 0)
		                    error ("Sendto failed");
						if (DEBUG) printtime();
		                printf("Sent final ACK at %lld.%.9ld\n", (long long)now.tv_sec%100, now.tv_nsec);
    
						settimeout(&timeout, now);
						rset = masterset;
						if((n = select(maxfdp, &rset, NULL, NULL, &timeout)) < 0)
							error("Select failed");
						if(n == 0) {
							if(DEBUG) printtime();
							printf("Timer expired! Ending connection\n");
							close(sockfd);
						} else if(FD_ISSET(sockfd, &rset)) {
							// Sender is re-sending the FIN-ACK
							n = recvfrom(sockfd, buffer, PSIZE + hsize, 0, NULL, 0);
							struct header *lastack;
							initheader(&lastack);
							memcpy(lastack, buffer, hsize);
							if(DEBUG) printtime();
							printf("FIN-ACK received again\n");
							free(lastack); lastack = 0;
						}
					} while (n > 0); // End when select expires
					break;
				} else {
                    printf("Requested file %s did not exist or had no data\n", argv[3]);
					fclose(fp);
					//Remove file created
					int status=remove(argv[3]);
					break;
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
