#include "../common/commom.h"

#define BUFFER_SEND 1024

int main(int argc, char **argv) {
  // argv: prog_name, host, port, fname
  if (argc != 4) {
    printUsageSender(*argv);
    exit(1);
  }

  // Setup connection structure
  int sock_fd = setupSocketSender();
  struct sockaddr_in recv_addr;
  setupAddr(&recv_addr, argv[1], atoi(argv[2]));

  char buffer[BUFFER_SEND];
  bzero(&buffer, sizeof(buffer));

  // Send data
  snprintf(buffer, sizeof(buffer), "Send data to server ");
  if( sendto(sock_fd, buffer, strlen(buffer), 0, (struct sockaddr *) &recv_addr, sizeof(recv_addr)) == -1) ERROR();


  size_t recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);
  if(recv_len == -1ul) ERROR();

  buffer[recv_len] = 0;
  printf("Receiver acknownlwdged: %s", buffer);


  return 0;
}
