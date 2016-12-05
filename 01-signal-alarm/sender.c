#include "../common/commom.h"

#define ALARM_DELAY 1
#define BUFFER_SEND 10240
#define MAX_SENDING 1000

FileHeaderSender *file = NULL;

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
  if (pthread_create(&fileThread, NULL, &readFile, file) < 0)
    ERROR();

  // Send file info (three times)
  int32_t info[1] = {(int32_t)0x40000000 | (int32_t)file->size};
  for (int i = 0; i < 5; i++) {
    if (sendto(sock_fd, info, sizeof(info), 0, (struct sockaddr *)&recv_addr,
               sizeof(recv_addr)) == -1)
      ERROR();
    usleep(1000);
  }

  // Send file content
  int sending = 0;
  int turn = 0;
  size_t currentPos = 0;
  char buffer[BUFFER_SEND];
  bzero(&buffer, sizeof(buffer));

  int k =0;
  while (file->sent < file->size) {
    k++;
    if (sending < MAX_SENDING && currentPos < file->read) {
      // Send a package
      FileChunk *chunk = file->chunks + currentPos;
      size_t buf = sizeof(int32_t) + chunk->size;
      int32_t *head = (int32_t *)buffer;
      *head = (chunk->size << 16) | chunk->index;
      memcpy(buffer + sizeof(int32_t), chunk->data, chunk->size);

      if (sendto(sock_fd, buffer, buf, 0, (struct sockaddr *)&recv_addr,
                 sizeof(recv_addr)) == -1)
        ERROR();
      chunk->status = FILE_CHUNK_SENT;
      sending++;
      printf("[SNT%ld]", currentPos);
      if (turn == 0) {
        currentPos++;
        if (currentPos == file->size)
          turn = 1, currentPos = 0;
      }
      usleep(1000);
    }
    if (sending >= MAX_SENDING || k % 100 == 0) {
      // Wait for a package
      sighandler_t orig_handler = signal(SIGALRM, SIGALRM_handler);
      siginterrupt(SIGALRM, 1);
      alarm(ALARM_DELAY);
      int recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);
      if (recv_len == -1) {
        if (errno == EINTR)
          printf("Alarm arrival\n"), sending = 0;
        else
          ERROR();
      } else {
        // Receive response
        alarm(0);
        // Data format: {HEAD&LEN, BITS}
        uint32_t *head = (uint32_t *)buffer;
        if(*head == 0xffffffff){
          printf("Finish\n");
          break;
        }
        if (!(*head & 0x40000000)) {
          printf("header lost, resend header");
        } else {
          *head &= 0x3fffffff;
        }

          printf("[ACK-PKG]");
        for (uint32_t i = 0; i < *head; i++) {
          uint8_t k =
              *(int8_t *)(buffer + sizeof(int32_t) + i / 8 * sizeof(int8_t));
          if ((k >> (i % 8)) & 1) {
            if (file->chunks[i].status == FILE_CHUNK_SENT) {
              file->chunks[i].status = FILE_CHUNK_RECEIVED;
              file->sent++;
            }
          }
        }
        sending = sending / 4 * 3;
      }
      signal(SIGALRM, orig_handler);
    }
    if (turn == 0)
      continue;
    if (file->sent < file->size)
      do {
        currentPos++;
        currentPos = currentPos == file->size ? 0 : currentPos;
      } while (file->chunks[currentPos].status == FILE_CHUNK_RECEIVED);
  }
  pthread_join(fileThread, NULL);
  return 0;
}
