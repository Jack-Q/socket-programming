#include "../common/commom.h"

#define BUFFER_SEND 1024

FileHeaderSender *file = NULL;

void *readFile(void *ptr) {
  while (file->read < file->size) {
    int readSize = read(file->fd, file->chunks[file->read].data, FILE_CHUNK_SIZE);
    if (readSize == -1) {
      if (errno == EINTR)
        continue;
      else
        ERROR();
    }
    file->chunks[file->read].index = file->read;
    file->chunks[file->read].size = readSize;
    file->chunks[file->read].status = FILE_CHUNK_UNSENT;
    file->read++;
    printf("Read chunk %ld with size %d\n", file->read, readSize);
  }
  printf("Load file finished\n");
  pthread_exit(0);
}

int main(int argc, char **argv) {
  // argv: prog_name, host, port, fname
  if (argc != 4) {
    printUsageSender(argv[0]);
    exit(1);
  }

  // Setup connection structure
  int sock_fd = setupSocketSender();
  struct sockaddr_in recv_addr;
  setupAddr(&recv_addr, argv[1], atoi(argv[2]));

  pthread_t fileThread;
  file = setupFileSender(argv[3]);
  if (pthread_create(&fileThread, NULL, &readFile, NULL) < 0)
    ERROR();

  char buffer[BUFFER_SEND];
  bzero(&buffer, sizeof(buffer));

  // Send data
  snprintf(buffer, sizeof(buffer), "Send data to server ");
  if (sendto(sock_fd, buffer, strlen(buffer), 0, (struct sockaddr *)&recv_addr,
             sizeof(recv_addr)) == -1)
    ERROR();

  size_t recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);
  if (recv_len == -1ul)
    ERROR();

  buffer[recv_len] = 0;
  printf("Receiver acknownlwdged: %s\n", buffer);

  return 0;
}
