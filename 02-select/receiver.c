#include "../common/commom.h"

#define BUFFER_RECV 5120
#define CHUNK_BUFFER_SIZE 100
#define SELECT_DELAY_USEC (100 * 1000)

FileHeaderReceiver *file = NULL;
int chunkBufferPos = 0;
int maxIndex = 0;
FileChunk chunkBuffer[CHUNK_BUFFER_SIZE];

int sock_fd;
int send_addr_set = 0;
struct sockaddr_in send_addr;
pthread_t fileThread;
char buffer[BUFFER_RECV];

char *fileName;

int receiveData() {
  int32_t *head = (int32_t *)buffer;
  if (*head & 0x40000000) {
    if (file == NULL) {
      // process header package
      printf("[Header]\n");
      size_t file_size = *head & 0x3fffffff;
      file = setupFileReceiver(fileName, file_size);
      // merge chunk buffer
      while (chunkBufferPos) {
        chunkBufferPos--;
        FileChunk *tmpChunk = &chunkBuffer[chunkBufferPos];
        memcpy(file->chunks[tmpChunk->index].data, tmpChunk->data,
               tmpChunk->size);
        file->chunks[tmpChunk->index].size = tmpChunk->size;
        file->chunks[tmpChunk->index].status = FILE_CHUNK_RECEIVED;
        file->received++;
        updateReceiveIndexRange(file, tmpChunk->index);
      }
      // open write thread
      if (pthread_create(&fileThread, NULL, &writeFile, file) < 0)
        ERROR();
    }
  } else {
    size_t chunkSize = *head >> 16;
    int chunkIndex = *head & 0xffff;
    if (file == NULL) {
      // Place items to buffer first
      if (chunkBufferPos >= CHUNK_BUFFER_SIZE)
        printf("CHUNK_BUFFER_OVERFLOW"), ERROR();
      chunkBuffer[chunkBufferPos].size = chunkSize;
      chunkBuffer[chunkBufferPos].index = chunkIndex;
      memcpy(chunkBuffer[chunkBufferPos].data, buffer + sizeof(int32_t),
             chunkSize);
      chunkBufferPos++;
      maxIndex = maxIndex > chunkIndex ? maxIndex : chunkIndex;
    } else {
      // Put data to storage
      if (file->chunks[chunkIndex].status == FILE_CHUNK_UNRECEIVED) {
        memcpy(file->chunks[chunkIndex].data, buffer + sizeof(int32_t),
               chunkSize);
        file->chunks[chunkIndex].size = chunkSize;
        file->chunks[chunkIndex].status = FILE_CHUNK_RECEIVED;
        file->received++;
        updateReceiveIndexRange(file, chunkIndex);
      }
    }
  }
  return 0;
}

int sendAck() {
  if (file == NULL) {
    size_t size = sizeof(int32_t) + (maxIndex - 1) / 8 * sizeof(int8_t) + 1;
    bzero(buffer, size);

    int32_t *head = (int32_t *)buffer;
    *head = maxIndex;

    for (int i = 0; i < chunkBufferPos; i++) {
      int index = chunkBuffer[i].index;
      int8_t *k =
          (int8_t *)(buffer + sizeof(int32_t) + index / 8 * sizeof(int8_t));
      *k |= 1 << (index % 8);
    }

    if (sendto(sock_fd, buffer, size, 0, (struct sockaddr *)&send_addr,
               sizeof(send_addr)) < 0)
      ERROR();

  } else {
    printf("[l%d,h%d]", file->received_lo, file->received_hi);
    int ackBase = file->received_lo;
    int ackCount = file->received_hi - file->received_lo;
    size_t size = sizeof(int32_t) + (ackCount - 1) / 8 * sizeof(int8_t) + 1;
    bzero(buffer, size);

    int32_t *head = (int32_t *)buffer;
    *head = ackCount | (ackBase << 16) | 0x40000000;

    for (int i = 0; i * 8 < ackCount; i++) {
      int8_t *k = (int8_t *)(buffer + sizeof(int32_t) + i * sizeof(int8_t));
      for (int j = 0; j < 8 && j + i * 8 < ackCount; j++)
        *k |= file->chunks[ackBase + i * 8 + j].status == FILE_CHUNK_RECEIVED
                  ? (1 << j)
                  : 0;
    }

    if (sendto(sock_fd, buffer, size, 0, (struct sockaddr *)&send_addr,
               sizeof(send_addr)) < 0)
      ERROR();
  }
  return 0;
}

int sendFin() {
  int32_t *fin = (int32_t *)buffer;
  *fin = 0xffffffff;
  for (int i = 0; i < 5; i++) {
    if (sendto(sock_fd, buffer, sizeof(int32_t), 0,
               (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
      ERROR();
    usleep(1000);
  }
  return 0;
}

int main(int argc, char **argv) {
  // argv: prog_name, port, fname
  if (argc != 3) {
    printUsageReceiver(argv[0]);
    exit(1);
  }
  fileName = argv[2];

  sock_fd = setupSocketReceiver(atoi(argv[1]));

  bzero(&send_addr, sizeof(send_addr));
  socklen_t addrlen = sizeof(send_addr);

  fd_set fds;
  struct timeval select_timeout;

  int ackCount = 0;
  while (1) {

    FD_ZERO(&fds);
    FD_SET(sock_fd, &fds);
    select_timeout.tv_sec = 0;
    select_timeout.tv_usec = SELECT_DELAY_USEC;
    int status = select(sock_fd + 1, &fds, NULL, NULL, &select_timeout);
    if (status == -1) ERROR();
    if (status == 0) {
      // Timeout
      ackCount = -1;
    } else {
      size_t recv_len = recvfrom(sock_fd, (void *)buffer, sizeof(buffer), 0,
                                 (struct sockaddr *)&send_addr, &addrlen);
      if (recv_len == -1ul) {
        ERROR();
      }
      receiveData();
      send_addr_set = 1;
      if (ackCount >= 0)
        ackCount++;
    }

    if (file && (file->received == file->size)) {
      sendFin();
      break;
    }

    if (send_addr_set &&
        (ackCount > (file ? (int)(file->size - file->received) : 0) + 50 ||
         ackCount < 0)) {
      sendAck();
      ackCount = 0;
    }
  }

  // Send multiple finish data
  pthread_join(fileThread, NULL);
  return 0;
}
