#include "../common/commom.h"

#define BUFFER_RECV 5120
#define CHUNK_BUFFER_SIZE 1200
#define SOCKET_OPTION_TIMEOUT_USEC (15 * 1000)

FileHeaderReceiver *file = NULL;
int chunkBufferPos = 0;
int maxIndex = 0;
FileChunk* chunkBuffer[CHUNK_BUFFER_SIZE];

int sock_fd;
int send_addr_set = 0;
struct sockaddr_in send_addr;
pthread_t fileThread;
char buffer[BUFFER_RECV];

char *fileName;

int received = 0;
int receiveData() {
  int32_t *head = (int32_t *)buffer;
  if (*head & 0x40000000) {
    if (file == NULL) {
      // process header package
      size_t file_size = *head & 0x3fffffff;
      file = setupFileReceiver(fileName, file_size);
      // merge chunk buffer
      while (chunkBufferPos) {
        chunkBufferPos--;
        FileChunk *tmpChunk = chunkBuffer[chunkBufferPos];
        memcpy(file->chunks[tmpChunk->index].data, tmpChunk->data,
               tmpChunk->size);
        file->chunks[tmpChunk->index].size = tmpChunk->size;
        file->chunks[tmpChunk->index].status = FILE_CHUNK_RECEIVED;
        file->received++;
        updateReceiveIndexRange(file, tmpChunk->index);
        // Free Memory
        free(tmpChunk);
        chunkBuffer[chunkBufferPos] = NULL;
      }
      // open write thread
      if (pthread_create(&fileThread, NULL, &writeFile, file) < 0)
        ERROR();
    }
  } else {
    size_t chunkSize = *head >> 16;
    int chunkIndex = *head & 0xffff;

    received++;

    if (file == NULL) {
      // Place items to buffer first
      if (chunkBufferPos >= CHUNK_BUFFER_SIZE)
        printf("CHUNK_BUFFER_OVERFLOW"), ERROR();
      chunkBuffer[chunkBufferPos] = (FileChunk *) malloc(sizeof(FileChunk));
      chunkBuffer[chunkBufferPos]->size = chunkSize;
      chunkBuffer[chunkBufferPos]->index = chunkIndex;
      memcpy(chunkBuffer[chunkBufferPos]->data, buffer + sizeof(int32_t),
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
      int index = chunkBuffer[i]->index;
      int8_t *k =
          (int8_t *)(buffer + sizeof(int32_t) + index / 8 * sizeof(int8_t));
      *k |= 1 << (index % 8);
    }

    if (sendto(sock_fd, buffer, size, 0, (struct sockaddr *)&send_addr,
               sizeof(send_addr)) < 0)
      ERROR();

  } else {
    // printf("[l%d,h%d]", file->received_lo, file->received_hi);
    if(file->size - file->received < 100 && file->received_hi - file->received_lo > 800){
      // New schema
      // [HEAD|LABEL|COUNT][POSITION]
      int ackCount = file->size - file->received;
      size_t size = sizeof(int32_t) + (ackCount) * sizeof(int16_t);
      bzero(buffer, size);


      int32_t *head = (int32_t *)buffer;
      *head = ackCount | (0x3fff << 16) |0x40000000;

      size_t i,j;
      for(i = file->received_lo, j = 0; i < file->size; i++){
        if(file->chunks[i].status == FILE_CHUNK_UNRECEIVED){
          int16_t *k = (int16_t *)(buffer + sizeof(int32_t) + j * sizeof(int16_t));
          *k = i;
          j++;
        }
      }

      printf("[NEW,%ld,%d]", j, ackCount);

      if (sendto(sock_fd, buffer, size, 0, (struct sockaddr *)&send_addr,
      sizeof(send_addr)) < 0)
      ERROR();
    }else{
      // Old schema
      // [HEAD|BASE|SIZE][BIT MAPS]
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
    usleep(10);
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
  struct timeval timeout = {0, SOCKET_OPTION_TIMEOUT_USEC};
  if(setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) ERROR();

  bzero(&send_addr, sizeof(send_addr));
  socklen_t addrlen = sizeof(send_addr);


  int ackCount = 0;
  int lastSend = 0;
  int lastRecv = 0;
  while (1) {
    size_t recv_len = recvfrom(sock_fd, (void *)buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&send_addr, &addrlen);
    if (recv_len == -1ul) {
      if (errno != EWOULDBLOCK) {
        ERROR();
      }
      // Timeout
      printf("[RECV_TIMEOUT]");
      lastSend -= 100;
    } else {
      receiveData();
      send_addr_set = 1;
      if (ackCount >= 0)
        ackCount++;
      else
        ackCount=0;
    }

    if(file && (file->received == file->size)) {
      sendFin();
      break;
    }
    if(file && (file->size - file->received < 200)){
      lastSend -= 20 - (file->size - file->received) / 10;
    }
    if(file && (file->received - lastRecv > 600)){
      lastSend = -1;
    }
    if (send_addr_set && (
      ackCount > (file ? (int)(file->size - file->received) * 2 : 0) + 50 
      || lastSend < 0)) {
      sendAck();
      ackCount = 0;
      lastSend = 300;
      lastRecv = file ? file->received : 0;
    }
  }

  // Send multiple finish data
  pthread_join(fileThread, NULL);
  printf("\n[RCV%.2f]\n", file ? 100.0f * received / file->size : 0);
  return 0;
}
