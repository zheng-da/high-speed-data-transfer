// ----rawudp.c------
// Must be run by root lol! Just datagram, no payload/data
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

// The packet length
#define PCKT_LEN 8192

void isleep(int usec)
{
	int diff;
	struct timeval start, now;

	gettimeofday(&start, NULL);
	diff = 0;
	while(diff < usec) {
		/* If enough time to sleep, otherwise, busywait */
		if(usec - diff > 200) {
			usleep(usec-20);
		}
		gettimeofday(&now, NULL);
		diff = now.tv_sec - start.tv_sec;
		diff *= 1000000;
		diff += now.tv_usec - start.tv_usec;
	}
}

// Function for checksum calculation. From the RFC,
// the checksum algorithm is:
//  "The checksum field is the 16 bit one's complement of the one's
//  complement sum of all 16 bit words in the header.  For purposes of
//  computing the checksum, the value of the checksum field is zero."
unsigned short csum(unsigned short *buf, int nwords)
{       //
	unsigned long sum;
	for(sum=0; nwords>0; nwords--)
		sum += *buf++;
	sum = (sum >> 16) + (sum &0xffff);
	sum += (sum >> 16);
	return (unsigned short)(~sum);
}

// Source IP, source port, target IP, target port from the command line arguments
int main(int argc, char *argv[])
{
	int sd;
	// No data/payload just datagram
	char buffer[PCKT_LEN];
	// Our own headers' structures
	struct iphdr *ip = (struct iphdr *) buffer;
	struct udphdr *udp = (struct udphdr *) (buffer + sizeof(struct iphdr));

	// Source and destination addresses: IP and port
	struct sockaddr_in sin, din;

	int one = 1;
	const int *val = &one;

	memset(buffer, 0, PCKT_LEN);
	if(argc != 8)
	{
		printf("- Invalid parameters!!!\n");
		printf("- Usage %s <source hostname/IP> <source port> <target hostname/IP> <target port> <payload size> <num of packets> <rate>\n", argv[0]);
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

	// The source is redundant, may be used later if needed
	// The address family
	sin.sin_family = AF_INET;
	din.sin_family = AF_INET;

	// Port numbers
	sin.sin_port = htons(atoi(argv[2]));
	din.sin_port = htons(atoi(argv[4]));

	// IP addresses
	sin.sin_addr.s_addr = inet_addr(argv[1]);
	din.sin_addr.s_addr = inet_addr(argv[3]);

	int payload_size = atoi(argv[5]);
	int packet_len = sizeof(struct iphdr) + sizeof(struct udphdr) + payload_size;
	int num_packets = atoi(argv[6]);
	int send_rate = atoi(argv[7]);

	// Fabricate the IP header or we can use the
	// standard header structures but assign our own values.
	ip->ihl = 5;
	ip->version = 4;
	ip->tos = 16; // Low delay
	ip->tot_len = htons(packet_len);
	ip->id = htons(54321);
	ip->ttl = 64; // hops
	ip->protocol = 17; // UDP
	// Source IP address, can use spoofed address here!!!
	ip->saddr = inet_addr(argv[1]);
	// The destination IP address
	ip->daddr = inet_addr(argv[3]);
	ip->frag_off |= htons(IP_DF);

	// Fabricate the UDP header. Source port number, redundant
	udp->source = htons(atoi(argv[2]));
	// Destination port number
	udp->dest = htons(atoi(argv[4]));
	udp->len = htons(sizeof(struct udphdr));

	// Calculate the checksum for integrity
	ip->check = csum((unsigned short *)buffer, packet_len);

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
	printf("Using Source IP: %s port: %u, Target IP: %s port: %u.\n", argv[1], atoi(argv[2]), argv[3], atoi(argv[4]));

	int count;
	struct timeval start, now;
	gettimeofday(&start, NULL);
	for(count = 1; count <= num_packets + 10; count++)
	{
		long *payload = (long *) (buffer + sizeof(struct iphdr) + sizeof(struct udphdr));
		if (count < num_packets)
			*payload = 9999;
		else
			*payload = 9998;
		ssize_t len = sendto(sd, buffer, packet_len, 0, (struct sockaddr *)&din, sizeof(din));
		if(len < 0)
		{
			perror("sendto() error");
			exit(-1);
		}
		gettimeofday(&now, NULL);
		double rate_now;
		long duration_now;
        duration_now  = (now.tv_sec - start.tv_sec);
        duration_now *= 1000000;
        duration_now += now.tv_usec - start.tv_usec;
        rate_now = packet_len * count * 8 * 1000;
        rate_now = rate_now/duration_now;
		if(rate_now > send_rate) {
			long int_delay = packet_len * count * 8 * 1000;
			int_delay = int_delay/send_rate;
			int_delay = int_delay - duration_now;

			if((int_delay <= 0)||(int_delay > 10000000))
				printf("!!! BIG delay !!!  %ld\n", int_delay);
			if(int_delay > 0)
				isleep(int_delay);
		}
		printf("send a packet of %ld bytes\n", len);
	}

	close(sd);
	return 0;
}

