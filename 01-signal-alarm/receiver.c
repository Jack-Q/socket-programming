#include "../common/commom.h"

#define BUFFER_RECV 10240
#define CHUNK_BUFFER_SIZE 100

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
      printf("[HEADRCV]");
      // process header package
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
      }
      // open write thread
      if (pthread_create(&fileThread, NULL, &writeFile, file) < 0)
        ERROR();
    } else {
      printf("Extra header package received\n");
    }

  } else {
    // TODO: re-receive
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
      printf("[RCV%d]", chunkIndex);
      if (file->chunks[chunkIndex].status == FILE_CHUNK_UNRECEIVED) {
        memcpy(file->chunks[chunkIndex].data, buffer + sizeof(int32_t),
               chunkSize);
        file->chunks[chunkIndex].size = chunkSize;
        file->chunks[chunkIndex].status = FILE_CHUNK_RECEIVED;
        file->received++;
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

    size_t size = sizeof(int32_t) + (file->size - 1) / 8 * sizeof(int8_t) + 1;
    bzero(buffer, size);

    int32_t *head = (int32_t *)buffer;
    *head = file->size | 0x40000000;

    for (size_t i = 0; i * 8 < file->size; i++) {
      int8_t *k = (int8_t *)(buffer + sizeof(int32_t) + i * sizeof(int8_t));
      for (size_t j = 0; j < 8 && j + i * 8 < file->size; j++)
        *k |= file->chunks[i * 8 + j].status == FILE_CHUNK_RECEIVED ? (1 << j)
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

  signal(SIGALRM, SIGALRM_handler);
  siginterrupt(SIGALRM, 1);

  int ackCount = 0;
  while (1) {
    ualarm(1000 * 200, 0);
    size_t recv_len = recvfrom(sock_fd, (void *)buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&send_addr, &addrlen);
    if (recv_len == -1ul) {
      if (errno != EINTR){
        ERROR();
      }
      printf("ALARM\n");
      ackCount = -1;
    } else {
      ualarm(0, 0);
      receiveData();
      send_addr_set = 1;
      if(ackCount>=0) ackCount++;
    }


    if (file && (file->received == file->size)) {
      sendFin();
      break;
    }

    if(send_addr_set && ackCount > 100 && ackCount < 0){
      sendAck();
    }
  }

  // Send multiple finish data
  pthread_join(fileThread, NULL);
  return 0;
}
