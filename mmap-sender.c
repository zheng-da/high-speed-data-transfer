#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <poll.h>
#include <pthread.h>
#include <netdb.h>

/* params */
static char * str_devname= NULL;
static int c_packet_sz   = 150;
static int c_packet_nb   = 1000;
static int c_buffer_sz   = 1024*8;
static int c_buffer_nb   = 1024;
static int c_sndbuf_sz   = 0;
static int c_mtu         = 0;
static int c_send_mask   = 127;
static int c_error       = 0;
static int mode_dgram    = 0;
static int mode_thread   = 0;
static int mode_loss     = 0;
static int mode_verbose  = 0;
static int c_num_pkts    = 1000;
static struct sockaddr_in dst_addr;

/* globals */
volatile int fd_socket;
volatile int data_offset = 0;
volatile struct sockaddr_ll *ps_sockaddr = NULL;
volatile struct tpacket_hdr * ps_header_start;
volatile int shutdown_flag = 0;
struct tpacket_req s_packet_req;
struct sockaddr_in src_addr;
char ether_src[ETH_ALEN];
char ether_dst[ETH_ALEN];
// TODO I just hard code these two for now.
short src_port = 6666;

int task_send(int blocking);
ssize_t sendudp(char *data, ssize_t len, const struct sockaddr_in *dest);

static void usage()
{
	fprintf( stderr,
			"Usage: ./packet_mmap [OPTION] [INTERFACE]\n"
			" -h\tshow this help\n"
			" -g\tuse SOCK_DGRAM\n"
			" -t\tuse dual thread\n"
			" -s\tset packet size\n"
			" -c\tset packet count\n"
			" -m\tset mtu\n"
			" -b\tset buffer size\n"
			" -n\tset buffer count\n"
			" -j\tset send() period (mask==0)\n"
			" -z\tset socket buffer size\n"
			" -a\tset destination IP address\n"
			" -p\tset destination port\n"
			" -M\tset destination MAC address\n"
			" -l\tdiscard wrong packets\n"
			" -e\tgenerate error [num]\n"
			" -v\tbe verbose\n"
		   );
}

void getargs( int argc, char ** argv )
{
	int c;
	opterr = 0;
	while( (c = getopt( argc, argv, "e:s:m:b:B:n:c:z:j:a:p:M:vhgtl"))!= EOF) {
		switch( c ) {
			case 's': c_packet_sz = strtoul( optarg, NULL, 0 ); break;
			case 'c': c_packet_nb = strtoul( optarg, NULL, 0 ); break;
			case 'b': c_buffer_sz = strtoul( optarg, NULL, 0 ); break;
			case 'n': c_buffer_nb = strtoul( optarg, NULL, 0 ); break;
			case 'z': c_sndbuf_sz = strtoul( optarg, NULL, 0 ); break;
			case 'm': c_mtu       = strtoul( optarg, NULL, 0 ); break;
			case 'j': c_send_mask = strtoul( optarg, NULL, 0 ); break;
			case 'a': {
						  struct hostent h_dent;
						  memcpy(&h_dent, gethostbyname(optarg), sizeof(h_dent));
						  memcpy(&dst_addr.sin_addr, h_dent.h_addr, sizeof(dst_addr));
						  break;
					  }
			case 'p': dst_addr.sin_port = atoi(optarg);         break;
			case 'M': {
						  char *tmp = optarg;
						  int i;
						  for (i = 0; i < 5; i++) {
							  tmp = strstr(tmp, ":");
							  *tmp = 0;
							  ether_dst[i] = strtoul(tmp, NULL, 16);
							  tmp++;
						  }
						  ether_dst[5] = strtoul(tmp, NULL, 16);
						  break;
					  }
			case 'e': c_error     = strtoul( optarg, NULL, 0 ); break;
			case 'g': mode_dgram  = 1;                          break;
			case 't': mode_thread = 1;                          break;
			case 'l': mode_loss   = 1;                          break;
			case 'v': mode_verbose= 1;                          break;
			case 'h': usage(); exit( EXIT_FAILURE );            break;
			case '?':
					  if ( isprint (optopt) ) {
						  fprintf ( stderr,
								  "ERROR: unrecognised option \"%c\"\n",
								  (char) optopt );
						  exit( EXIT_FAILURE );
					  }
					  break;
			default:
					  fprintf( stderr, "ERROR: unrecognised command line option\n");
					  exit( EXIT_FAILURE );
					  break;
		}
	}
	/* take first residual non option argv element as interface name. */
	if ( optind < argc ) {
		str_devname = argv[ optind ];
	}

	if( !str_devname ) {
		fprintf( stderr, "ERROR: No interface was specified\n");
		usage();
		exit( EXIT_FAILURE );
	}

	printf( "CURRENT SETTINGS:\n" );
	printf( "str_devname:       %s\n", str_devname );
	printf( "c_packet_sz:       %d\n", c_packet_sz );
	printf( "c_buffer_sz:       %d\n", c_buffer_sz );
	printf( "c_buffer_nb:       %d\n", c_buffer_nb );
	printf( "c_packet_sz count: %d\n", c_packet_sz );
	printf( "c_packet_nb count: %d\n", c_packet_nb );
	printf( "c_mtu:             %d\n", c_mtu );
	printf( "c_send_mask:       %d\n", c_send_mask );
	printf( "c_sndbuf_sz:       %d\n", c_sndbuf_sz );
	printf( "mode_loss:         %d\n", mode_loss );
	printf( "mode_thread:       %d\n", mode_thread );
}

struct sockaddr get_if_addr(char *dev)
{
	struct ifreq if_mac;
	memset(&if_mac, 0, sizeof(struct ifreq));
	strncpy(if_mac.ifr_name, dev, IFNAMSIZ - 1);
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}
	if (ioctl(sock, SIOCGIFHWADDR, &if_mac) < 0) {
		perror("SIOCGIFHWADDR");
		exit(1);
	}
	close(sock);
	return if_mac.ifr_hwaddr;
}

struct sockaddr_in get_if_ip(char *dev)
{
	struct ifreq if_ip;
	memset(&if_ip, 0, sizeof(struct ifreq));
	if_ip.ifr_addr.sa_family = AF_INET;
	strncpy(if_ip.ifr_name, dev, IFNAMSIZ - 1);
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		perror("socket");
		exit(1);
	}
	if (ioctl(sock, SIOCGIFADDR, &if_ip) < 0) {
		perror("SIOCGIFADDR");
		exit(1);
	}
	close(sock);
	return *(struct sockaddr_in *) &if_ip.ifr_addr;
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

int main( int argc, char ** argv )
{
	uint32_t size, opt_len;
	int ec;
	struct sockaddr_ll my_addr, peer_addr;
	struct ifreq s_ifr; /* points to one interface returned from ioctl */
	int len;
	int i_ifindex;
	int mode_socket;
	int tmp;

	/* get configuration */
	getargs( argc, argv );

	src_addr = get_if_ip(str_devname);
	struct sockaddr hwaddr = get_if_addr(str_devname);
	memcpy(ether_src, hwaddr.sa_data, sizeof(ether_dst));
	dst_addr.sin_family = AF_INET;

	printf("\nSTARTING TEST:\n");

	if (mode_dgram) {
		mode_socket = SOCK_DGRAM;
	}
	else
		mode_socket = SOCK_RAW;

	fd_socket = socket(PF_PACKET, mode_socket, htons(ETH_P_ALL));
	if(fd_socket == -1)
	{
		perror("socket");
		return EXIT_FAILURE;
	}

	/* start socket config: device and mtu */

	/* initialize interface struct */
	strncpy (s_ifr.ifr_name, str_devname, sizeof(s_ifr.ifr_name));

	/* Get the index of the network interface. */
	ec = ioctl(fd_socket, SIOCGIFINDEX, &s_ifr);
	if(ec == -1)
	{
		perror("iotcl");
		return EXIT_FAILURE;
	}
	/* update with interface index */
	i_ifindex = s_ifr.ifr_ifindex;

	/* new mtu value */
	if(c_mtu) {
		s_ifr.ifr_mtu = c_mtu;
		/* update the mtu through ioctl */
		ec = ioctl(fd_socket, SIOCSIFMTU, &s_ifr);
		if(ec == -1)
		{
			perror("iotcl");
			return EXIT_FAILURE;
		}
	}

	/* set sockaddr info */
	memset(&my_addr, 0, sizeof(struct sockaddr_ll));
	my_addr.sll_family = AF_PACKET;
	my_addr.sll_protocol = htons(ETH_P_ALL);
	my_addr.sll_ifindex = i_ifindex;

	/* bind port */
	if (bind(fd_socket, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll)) == -1)
	{
		perror("bind");
		return EXIT_FAILURE;
	}

	/* prepare Tx ring request */
	s_packet_req.tp_block_size = c_buffer_sz;
	s_packet_req.tp_frame_size = c_buffer_sz;
	s_packet_req.tp_block_nr = c_buffer_nb;
	s_packet_req.tp_frame_nr = c_buffer_nb;


	/* calculate memory to mmap in the kernel */
	size = s_packet_req.tp_block_size * s_packet_req.tp_block_nr;

	/* set packet loss option */
	tmp = mode_loss;
	if (setsockopt(fd_socket, SOL_PACKET, PACKET_LOSS,
				(char *)&tmp, sizeof(tmp))<0)
	{
		perror("setsockopt: PACKET_LOSS");
		return EXIT_FAILURE;
	}

	/* send TX ring request */
	if (setsockopt(fd_socket, SOL_PACKET, PACKET_TX_RING,
				(char *)&s_packet_req, sizeof(s_packet_req))<0)
	{
		perror("setsockopt: PACKET_TX_RING");
		return EXIT_FAILURE;
	}


	/* change send buffer size */
	if(c_sndbuf_sz) {
		printf("send buff size = %d\n", c_sndbuf_sz);
		if (setsockopt(fd_socket, SOL_SOCKET, SO_SNDBUF, &c_sndbuf_sz,
					sizeof(c_sndbuf_sz))< 0)
		{
			perror("getsockopt: SO_SNDBUF");
			return EXIT_FAILURE;
		}
	}

	/* get data offset */
	data_offset = TPACKET_HDRLEN - sizeof(struct sockaddr_ll);
	printf("data offset = %d bytes\n", data_offset);

	/* mmap Tx ring buffers memory */
	ps_header_start = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd_socket, 0);
	if (ps_header_start == (void*)-1)
	{
		perror("mmap");
		return EXIT_FAILURE;
	}

	/* fill peer sockaddr for SOCK_DGRAM */
	if (mode_dgram)
	{
		char dstaddr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
		peer_addr.sll_family = AF_PACKET;
		peer_addr.sll_protocol = htons(ETH_P_IP);
		peer_addr.sll_ifindex = i_ifindex;
		peer_addr.sll_halen = ETH_ALEN;
		memcpy(&peer_addr.sll_addr, dstaddr, ETH_ALEN);
		ps_sockaddr = &peer_addr;
	}

	int i;
	char buf[1400];
	for (i = 0; i < c_num_pkts; i++)
		sendudp(buf, sizeof(buf), &dst_addr);


	/* display header of all blocks */
	return EXIT_SUCCESS;
}

/* This task will call send() procedure */
int task_send(int blocking) {
	int ec_send;
	static int total=0;

	if(blocking) printf("start send() thread\n");

	do
	{
		/* send all buffers with TP_STATUS_SEND_REQUEST */
		/* Wait end of transfer */
		if(mode_verbose) printf("send() start\n");
		ec_send = sendto(fd_socket,
				NULL,
				0,
				(blocking? 0 : MSG_DONTWAIT),
				(struct sockaddr *) ps_sockaddr,
				sizeof(struct sockaddr_ll));
		if(mode_verbose) printf("send() end (ec=%d)\n",ec_send);

		if(ec_send < 0) {
			perror("send");
			break;
		}
		else if ( ec_send == 0 ) {
			/* nothing to do => schedule : useful if no SMP */
			usleep(0);
		}
		else {
			total += ec_send/(c_packet_sz);
			printf("send %d packets (+%d bytes)\n",total, ec_send);
			fflush(0);
		}

	} while(blocking && !shutdown_flag);

	if(blocking) printf("end of task send()\n");
	//printf("end of task send(ec=%x)\n", ec_send);

	return ec_send;
}

void *get_free_buffer()
{
	int i;
	for (i = 0; i < c_buffer_nb; i++) {
		struct tpacket_hdr * ps_header = ((struct tpacket_hdr *)((void *)ps_header_start
					+ (c_buffer_sz * i)));
		char *data = ((void*) ps_header) + data_offset;
		if ((volatile uint32_t)ps_header->tp_status == TP_STATUS_AVAILABLE)
			return (void *) data;
	}
	return NULL;
}

struct iphdr *construct_ip(struct iphdr *ip,
		const struct sockaddr_in *dest, ssize_t packet_len)
{
	memset(ip, 0, sizeof (*ip));
	ip->ihl = 5;
	ip->version = 4;
	ip->tos = 16; // Low delay
	ip->tot_len = htons(packet_len);
	ip->id = htons(54321);
	ip->ttl = 64; // hops
	ip->protocol = 17; // UDP
	// Source IP address, can use spoofed address here!!!
	ip->saddr = src_addr.sin_addr.s_addr;
	// The destination IP address
	ip->daddr = dest->sin_addr.s_addr;
	ip->frag_off |= htons(IP_DF);
	return ip;
}

struct udphdr *construct_udp(struct udphdr *udp,
		short dst_port, ssize_t packet_len)
{
	udp->source = htons(src_port);
	// Destination port number
	udp->dest = htons(dst_port);
	udp->len = htons(packet_len);
	return udp;
}

struct ether_header *construct_ether(struct ether_header *ether)
{
	memcpy(ether->ether_shost, ether_src, sizeof(ether_src));
	memcpy(ether->ether_dhost, ether_dst, sizeof(ether_dst));
	ether->ether_type = htons(ETH_P_IP);
	return ether;
}

/* send a UDP packet. */
ssize_t sendudp(char *payload, ssize_t len, const struct sockaddr_in *dest)
{
	/* get free buffer */
	void *data = get_free_buffer();
	while (data == NULL) {
		usleep(1);
		data = get_free_buffer();
	}
	struct ether_header *ether = (struct ether_header *) data;
	struct iphdr *ip = (struct iphdr *) (data + sizeof (*ether));
	struct udphdr *udp = (struct udphdr *) (((char *) ip) + sizeof(struct iphdr));
	short packet_len = (short) len + sizeof (*ip) + sizeof (*udp);
	construct_ether(ether);
	construct_ip(ip, dest, packet_len);
	construct_udp(udp, dest->sin_port, packet_len - sizeof(struct iphdr));
	// Calculate the checksum for integrity
	ip->check = csum((unsigned short *)ip, packet_len);

	struct tpacket_hdr *ps_header = (struct tpacket_hdr *) (data - data_offset);
	/* update packet len */
	ps_header->tp_len = c_packet_sz;
	/* set header flag to USER (trigs xmit)*/
	ps_header->tp_status = TP_STATUS_SEND_REQUEST;
	int ec_send = task_send(0);
	return len;
}
