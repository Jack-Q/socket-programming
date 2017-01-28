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

void printUsage() { printf("USAGE: ./traceroute <ADDR>\n"); }

typedef struct {
  uint8_t seq;         // sequence number
  uint8_t ttl;         // time to live
  struct timeval time; // time sent
} trace_data_t;

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
  int max_hop = 64;
  int probes = 3;

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
  printf("traceroute to %s (%s), %d hops max \n", hostentity->h_name, p_ip,
         max_hop);

  // Create socket
  int sock_send = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_send == -1)
    ERROR();
  int sock_recv = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sock_recv == -1)
    ERROR();

  // Set recv Timeout
  struct timeval recv_timeout = {2, 0};
  if (setsockopt(sock_recv, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout,
                 sizeof(recv_timeout)) == -1)
    ERROR();

  // Prepare send target address
  struct sockaddr_in addr_send;
  addr_send.sin_family = AF_INET;
  memcpy((void *)&addr_send.sin_addr, (void *)hostentity->h_addr_list[0],
         sizeof(struct in_addr));
  addr_send.sin_port = 0;

  // Bind port and ip address
  struct sockaddr_in addr_bind;
  addr_bind.sin_family = AF_INET;
  addr_bind.sin_addr.s_addr = htonl(INADDR_ANY);
  addr_bind.sin_port =
      htons((pid & 0x0fff) | 0x8000); // use PID to solve confilct
  if (bind(sock_send, (struct sockaddr *)&addr_bind,
           sizeof(struct sockaddr_in)) == -1)
    ERROR();

  // Prepare recv address

  struct sockaddr_in addr_recv, addr_curr;
  socklen_t recv_addr_size = sizeof(struct sockaddr_in);
  bzero(&addr_recv, recv_addr_size);


  for (int ttl = 1, sequence = 0, done = 0; ttl < max_hop && !done; ttl++) {
    // set the ttl ip header field
    setsockopt(sock_send, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
    printf("%2d ", ttl);
    fflush(stdout);

    for (int probe = 0; probe < probes; probe++) {
      // construct data and content of package
      trace_data_t *trace_data = (trace_data_t *)send_buffer;
      trace_data->ttl = ttl;
      trace_data->seq = ++sequence;
      if (gettimeofday(&trace_data->time, 0) == -1)
        ERROR();
      size_t len_send = sizeof(trace_data_t);

      // apply a different port for each package
      addr_send.sin_port = htons((32768 + sequence) & 0xffff);
      if (sendto(sock_send, send_buffer, len_send, 0,
                 (struct sockaddr *)&addr_send,
                 sizeof(struct sockaddr_in)) == -1)
        ERROR();

      // keep receiving data package until timeout or package
      // received
      while (1) {
        recv_addr_size = sizeof(struct sockaddr_in);
        bzero(&addr_curr, recv_addr_size);
        int recv_len = recvfrom(sock_recv, recv_buffer, sizeof(recv_buffer), 0,
                                (struct sockaddr *)&addr_curr, &recv_addr_size);
        if (recv_len == -1) {
          if (errno == EINTR || errno == EWOULDBLOCK) {
            printf(" *");
            break;
          }
          ERROR();
        }
        struct ip *ip_ptr = (struct ip *)recv_buffer;
        size_t ip_len = ip_ptr->ip_hl << 2;

        struct icmp *icmp_ptr = (struct icmp *)(recv_buffer + ip_len);
        size_t icmp_len = recv_len - ip_len;
        if (icmp_len < 8)
          continue; // incomplete header of icmp
        if (icmp_ptr->icmp_type == ICMP_TIMXCEED &&
            icmp_ptr->icmp_code == ICMP_TIMXCEED_INTRANS) {
          if (icmp_len < 8 + sizeof(struct ip))
            continue;

          struct ip *oip_ptr = (struct ip *)(recv_buffer + ip_len + 8);
          size_t oip_len = oip_ptr->ip_hl << 2;
          if (icmp_len < 8 + oip_len + 4)
            continue; // header size of udp

          struct udphdr *udp_ptr =
              (struct udphdr *)(recv_buffer + ip_len + 8 + oip_len);
          if (oip_ptr->ip_p == IPPROTO_UDP &&
              udp_ptr->uh_sport == htons((pid & 0xfff) | 0x8000) &&
              udp_ptr->uh_dport == htons(32768 + sequence)) {
            // hit an intermediate route
            if (addr_recv.sin_family == addr_curr.sin_family &&
                memcmp(&addr_recv.sin_addr, &addr_curr.sin_addr,
                       sizeof(struct in_addr)) == 0) {
              // Same address
            } else {
              // replace latest address
              char p_ip[INET_ADDRSTRLEN];
              if (inet_ntop(AF_INET, (void *)&addr_curr.sin_addr, p_ip,
                            INET_ADDRSTRLEN) == NULL)
                ERROR();
              printf("%s ", p_ip);
              memcpy(&addr_recv, &addr_curr, sizeof(struct sockaddr_in));
            }
            struct timeval t;
            if (gettimeofday(&t, 0) == -1)
              ERROR();
            printf("%.3f ms ",
                   1000.0 * (t.tv_sec - trace_data->time.tv_sec) +
                       0.001 * (t.tv_usec - trace_data->time.tv_usec));
            break;
          }
        } else if (icmp_ptr->icmp_type == ICMP_UNREACH) {
          if (icmp_len < 8 + sizeof(struct ip))
            continue;

          struct ip *oip_ptr = (struct ip *)(recv_buffer + ip_len + 8);
          size_t oip_len = oip_ptr->ip_hl << 2;
          if (icmp_len < 8 + oip_len + 4)
            continue; // header size of udp

          struct udphdr *udp_ptr =
              (struct udphdr *)(recv_buffer + ip_len + 8 + oip_len);

          if (oip_ptr->ip_p == IPPROTO_UDP &&
              udp_ptr->uh_sport == htons((pid & 0xfff) | 0x8000) &&
              udp_ptr->uh_dport == htons(32768 + sequence)) {

            if (addr_recv.sin_family == addr_curr.sin_family &&
                memcmp(&addr_recv.sin_addr, &addr_curr.sin_addr,
                       sizeof(struct in_addr)) == 0) {
              // Same address
            } else {
              // replace latest address
              char p_ip[INET_ADDRSTRLEN];
              if (inet_ntop(AF_INET, (void *)&addr_curr.sin_addr, p_ip,
                            INET_ADDRSTRLEN) == NULL)
                ERROR();
              printf("%s ", p_ip);
              memcpy(&addr_recv, &addr_curr, sizeof(struct sockaddr_in));
            }
            struct timeval t;
            if (gettimeofday(&t, 0) == -1)
              ERROR();
            printf("%.3f ms",
                   1000.0 * (t.tv_sec - trace_data->time.tv_sec) +
                       0.001 * (t.tv_usec - trace_data->time.tv_usec));
            if (icmp_ptr->icmp_code == ICMP_UNREACH_PORT) {
              // reach destination
              done = 1;
              break;
            } else {
              // other problem
              printf(" x");
              break;
            }
          }
        }
        continue;
      }
      fflush(stdout);
    }

    printf("\n");
  }

  return 0;
}
