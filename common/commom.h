#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

extern int errno;

#ifdef DEBUG
#define ERROR() error(__FILE__, __LINE__)
void error(char *fileName, int line) {
  fprintf(stderr, "[%-10s:%-3d]Error: [%d] %s\n", fileName, line, errno,
          strerror(errno));
  exit(1);
}
#else
#define ERROR() error();
void error() {
  fprintf(stderr, "Error: [%d] %s\n", errno, strerror(errno));
  exit(1);
}
#endif

void printUsageSender(char *name) {
  printf("USAGE: %s <HostName> <Port> <ReadFrom>\n", name);
}

void printUsageReceiver(char *name) {
  printf("USAGE: %s <Port> <SaveAs>\n", name);
}

void setupAddr(struct sockaddr_in *addr, char *hostname, int port) {

  bzero(addr, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);

  struct hostent *hostentity;
  hostentity = gethostbyname(hostname);
  if (hostentity == NULL)
    ERROR();
  memcpy((void *)&addr->sin_addr, (void *)hostentity->h_addr_list[0],
         sizeof(struct in_addr));
}

int setupSocketSender() {
  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0)
    ERROR();
  return sock_fd;
}

int setupSocketReceiver(int port) {
  int sock_fd = setupSocketSender();
  struct sockaddr_in recv_addr;
  bzero(&recv_addr, sizeof(recv_addr));
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  recv_addr.sin_port = htons(port);
  
  if(bind(sock_fd, (struct sockaddr *) &recv_addr, sizeof(recv_addr)) < 0){
    ERROR();
  }

  return sock_fd;

}
