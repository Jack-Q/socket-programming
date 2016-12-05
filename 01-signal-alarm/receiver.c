#include "../common/commom.h"

#define BUFFER_RECV 10240
#define CHUNK_BUFFER_SIZE 100

FileHeaderReceiver *file = NULL;
int chunkBufferPos = 0;
int maxIndex = 0;
FileChunk chunkBuffer[CHUNK_BUFFER_SIZE];

int main(int argc, char **argv) {
  // argv: prog_name, port, fname
  if (argc != 3) {
    printUsageReceiver(argv[0]);
    exit(1);
  }

  int sock_fd = setupSocketReceiver(atoi(argv[1]));

  struct sockaddr_in send_addr;
  bzero(&send_addr, sizeof(send_addr));
  socklen_t addrlen = sizeof(send_addr);

  pthread_t fileThread;

  char buffer[BUFFER_RECV];

  while (1) {

    size_t recv_len = recvfrom(sock_fd, (void *)buffer, sizeof(buffer), 0,
                               (struct sockaddr *)&send_addr, &addrlen);
    if (recv_len == -1ul)
      ERROR();

    int32_t *head = (int32_t *)buffer;
    if (*head & 0x40000000) {
      if (file == NULL) {
        // process header package
        size_t file_size = *head & 0x3fffffff;
        file = setupFileReceiver(argv[2], file_size);
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
        memcpy(file->chunks[chunkIndex].data, buffer + sizeof(int32_t),
               chunkSize);
        file->chunks[chunkIndex].size = chunkSize;
        file->chunks[chunkIndex].status = FILE_CHUNK_RECEIVED;
        file->received++;
      }
    }

    if(!file || file->received % 1000 == 0){      
      if (file == NULL) {
        size_t size = sizeof(int32_t) + (maxIndex - 1) / 8 * sizeof(int8_t) + 1;
        bzero(buffer, size);

        int32_t *head = (int32_t *)buffer;
        *head = maxIndex;

        for (int i = 0; i < chunkBufferPos; i++) {
          int index = chunkBuffer[i].index;
          int8_t *k = (int8_t *)(buffer + sizeof(int32_t) + index / 8 * sizeof(int8_t));
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
          *k |= file->chunks[i * 8 + j].status == FILE_CHUNK_RECEIVED
          ? (1 << j)
          : 0;
        }

        if (sendto(sock_fd, buffer, size, 0, (struct sockaddr *)&send_addr,
        sizeof(send_addr)) < 0)
        ERROR();
      }
    }

    if(file->received == file->size){
      int32_t* fin = (int32_t*) buffer;
      *fin = 0xffffffff;
      for(int i = 0; i < 3; i++){
        if (sendto(sock_fd, buffer, sizeof(int32_t), 0, (struct sockaddr *)&send_addr,
                   sizeof(send_addr)) < 0)
          ERROR();
      }
      // Send multiple finish data
      pthread_join(fileThread, NULL);
      break;
    }
  }
  return 0;
}
