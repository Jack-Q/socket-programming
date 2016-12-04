#include "../common/commom.h"

#define BUFFER_RECV 1024

int main(int argc, char **argv) {
  // argv: prog_name, port, fname
  if (argc != 3) {
    printUsageReceiver(*argv);
    exit(1);
  }

  int sock_fd = setupSocketReceiver(atoi(argv[1]));

  struct sockaddr_in send_addr;
  bzero(&send_addr, sizeof(send_addr));
  socklen_t addrlen = sizeof(send_addr);

  char buffer[BUFFER_RECV];
  bzero(buffer, sizeof(buffer));

  size_t recv_len = recvfrom(sock_fd, (void *) buffer, sizeof(buffer), 0,
                            (struct sockaddr*) & send_addr, &addrlen);
  if (recv_len == -1ul) ERROR();
  buffer[recv_len] = 0;
  printf("Received %s \n", buffer);

  // Send back
  snprintf(buffer, sizeof(buffer), "Data received from %d\n", ntohs(send_addr.sin_port));
  sendto(sock_fd, buffer, strlen(buffer) - 1, 0, (struct sockaddr*) &send_addr, sizeof(send_addr));

  return 0;
}
