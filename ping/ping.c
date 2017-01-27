#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PING_OPTION_TIMEOUT 3

extern int errno;
#define ERROR() error(__FILE__, __LINE__)
void error(const char *filename, int line) {
  fprintf(stderr, "[%-10s:%-3d]ERROR: [%d] %s\n", filename, line, errno,
          strerror(errno));
  exit(1);
}

#define BUFFER_SIZE 1500

char send_buffer[BUFFER_SIZE];
char recv_buffer[BUFFER_SIZE];


// Options to be addded
// 1. package size (the pacakge that is used to transfer in the web)
// 2. socket timeout (the maximum time before a package is treated as timeout)
// 3. package count (the total number of package that will be send)
void printUsage() { printf("USAGE: ./ping <ADDR>\n"); }

// fill the checksum field according to the content of the header
// The algorithm used for the header filed is the summation of all
// data bits (including the pending check bit wehen this funciton is
// used to calculate the content of checksum field) ought to be 0
unsigned short checksum(void *data, int nbytes) {
  unsigned short *ptr = (unsigned short *)data;
  long sum;
  unsigned short oddbyte;
  short answer;

  sum = 0;
  while (nbytes > 1) {
    sum += *ptr++;
    nbytes -= 2;
  }
  if (nbytes == 1) {
    oddbyte = 0;
    *((u_char *)&oddbyte) = *(u_char *)ptr;
    sum += oddbyte;
  }

  sum = (sum >> 16) + (sum & 0xffff);
  sum = sum + (sum >> 16);
  answer = (short)~sum;

  return (answer);
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    printUsage();
    return 1;
  }

  // Get PID as part of the payload of ICMP
  int pid = getpid() & 0xffff;

  // package size to be echoed
  int datalen = 84;

  // Get the ip address of target
  struct hostent *hostentity;
  hostentity = gethostbyname(argv[1]);
  if (hostentity == NULL) {
    ERROR();
  }
  char p_ip[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, (void *)hostentity->h_addr_list[0], p_ip,
                INET_ADDRSTRLEN) == NULL)
    ERROR();

  printf("PING %s (%s) %x(%d) bytes of data\n",
         hostentity->h_name ? hostentity->h_name : p_ip, p_ip, datalen,
         datalen);

  // Setup send address
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  memcpy((void *)&addr.sin_addr, (void *)hostentity->h_addr_list[0],
         sizeof(struct in_addr));
  addr.sin_port = 0;

  // create socket to send package
  int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock == -1)
    ERROR();

  // set socket option to prevent unbounded waiting of loat package
  struct timeval ping_timeout = {PING_OPTION_TIMEOUT, 0};
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ping_timeout, sizeof(ping_timeout)) ==
      -1)
      ERROR();


  // send package
  struct icmp *icmp = (struct icmp *)send_buffer;
  icmp->icmp_type = ICMP_ECHO; // specify sub-protocol for ICMP package 
  icmp->icmp_code = 0; // for ICMP ECHO sub-protocol, there is only one valid code mode
  icmp->icmp_id = pid; // identifier to differ process
  icmp->icmp_seq = 1; // update each time a package is send
  memset(icmp->icmp_data, 0xa5, datalen); // fill echoed data with pattern
  // overwrite begin of data with time info
  if (gettimeofday((struct timeval *)icmp->icmp_data, NULL) == -1)
    ERROR();
  icmp->icmp_cksum = 0; // init checksum to 0
  // calculate checksum
  icmp->icmp_cksum = checksum((void *)icmp, 8 + datalen);

  // Send out package
  if (sendto(sock, send_buffer, 8 + datalen, 0, (struct sockaddr *)&addr,
             sizeof(addr)) < 0)
    ERROR();

  // Receive package
  struct sockaddr_in recv_addr;
  socklen_t recv_addr_size = sizeof(struct sockaddr_in);

  while (1) {
    int recv_len = recvfrom(sock, recv_buffer, BUFFER_SIZE, 0,
                            (struct sockaddr *)&recv_addr, &recv_addr_size);

    if (recv_len == -1)
      ERROR();

    struct ip *ip_ptr = (struct ip *)recv_buffer;
    int ip_len = ip_ptr->ip_hl << 2; // ip header len needs shift
    if (ip_ptr->ip_p != IPPROTO_ICMP) {
      printf("Received package other than ICMP\n");
      continue;
    }

    struct icmp *icmp_ptr = (struct icmp *)(recv_buffer + ip_len);
    int icmp_len = recv_len - ip_len;
    if (icmp_len < 8) {
      printf("Malformed package\n");
      continue;
    }

    if (icmp_ptr->icmp_type == ICMP_ECHOREPLY) {
      if (icmp_ptr->icmp_id != pid) {
        // package from other package
        printf("Package from other ping\n");
        continue;
      }
      if (icmp_len < 16) {
        printf("Incomplete data\n");
        continue;
      }
      struct timeval *icmp_time = (struct timeval *)icmp_ptr->icmp_data;
      struct timeval cur_time;
      if (gettimeofday(&cur_time, 0) == -1)
        ERROR();
      printf("%d bytes from %s (%s): icmp_seq=%d ttl=%d time=%.3fms\n",
             icmp_len, hostentity->h_name ? hostentity->h_name : p_ip, p_ip,
             icmp_ptr->icmp_seq, ip_ptr->ip_ttl,
             1000.0 * (cur_time.tv_sec - icmp_time->tv_sec) +
                 (cur_time.tv_usec - icmp_time->tv_usec) / 1000.0);
    }
  }

  return 0;
}
