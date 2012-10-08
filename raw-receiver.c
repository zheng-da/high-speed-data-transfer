#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

// The packet length
#define PCKT_LEN 8192

// listen port from the command line arguments
int main(int argc, char *argv[])
{
	int sd;
	// No data/payload just datagram
	char buffer[PCKT_LEN];
	// Our own headers' structures
	struct iphdr *ip = (struct iphdr *) buffer;
	struct udphdr *udp = (struct udphdr *) (buffer + sizeof(struct iphdr));

	// Source and destination addresses: IP and port
	struct sockaddr_in lin;

	int one = 1;
	const int *val = &one;

	memset(buffer, 0, PCKT_LEN);
	if(argc != 2)
	{
		printf("- Invalid parameters!!!\n");
		printf("- Usage %s <listen port>\n", argv[0]);
		exit(-1);
	}

	// Create a raw socket with UDP protocol
	sd = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
	if(sd < 0)
	{
		perror("socket() error");
		// If something wrong just exit
		exit(-1);
	}
	else
		printf("socket() - Using SOCK_RAW socket and UDP protocol is OK.\n");

	bzero((char *)&lin,sizeof(lin));
	// The source is redundant, may be used later if needed
	// The address family
	lin.sin_family = AF_INET;
	// Port numbers
	lin.sin_port = htons(atoi(argv[1]));

	if (bind(sd,(struct sockaddr *)&lin,sizeof(lin)) < 0)
	{
		fprintf(stderr, "ERROR binding socket.\n");
		exit(1);
	}

	int payload_size = atoi(argv[5]);
	int packet_len = sizeof(struct iphdr) + sizeof(struct udphdr) + payload_size;

	// Inform the kernel do not fill up the packet structure. we will build our own...
	if(setsockopt(sd, IPPROTO_IP, IP_HDRINCL, val, sizeof(one)) < 0)
	{
		perror("setsockopt() error");
		exit(-1);
	}
	else
		printf("setsockopt() is OK.\n");

	// Send loop, send for every 2 second for 100 count
	printf("Trying...\n");
	printf("Using raw socket and UDP protocol\n");
	printf("Using listen port: %u.\n", atoi(argv[1]));

	int count;
	for(count = 1; count <=20; count++)
	{
		ssize_t len = recv(sd, buffer, sizeof(buffer), 0);
		if(len < 0)
		{
			perror("recv() error");
			exit(-1);
		}
		printf("receive a packet of %ld bytes\n", len);
	}

	close(sd);
	return 0;
}
