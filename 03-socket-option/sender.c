#include "../common/commom.h"

#define SOCKET_OPTION_TIMEOUT_USEC (15 * 1000)
#define BUFFER_SEND 5120

FileHeaderSender *file = NULL;
int sock_fd;
struct sockaddr_in recv_addr;
pthread_t fileThread;
char buffer[BUFFER_SEND];
int headerAck = 0;

int sendHeader(int times) {
  // Send file info (three times)
  int32_t info[1] = {(int32_t)0x40000000 | (int32_t)file->size};
  for (int i = 0; i < times; i++) {
    if (sendto(sock_fd, info, sizeof(info), 0, (struct sockaddr *)&recv_addr,
               sizeof(recv_addr)) == -1)
      ERROR();
    usleep(5);
  }
  return 0;
}

int dataCount = 0;
int sendData(size_t position){
  dataCount ++;
  // Send a package
  FileChunk *chunk = file->chunks + position;
  size_t buf = sizeof(int32_t) + chunk->size;
  int32_t *head = (int32_t *)buffer;
  *head = (chunk->size << 16) | chunk->index;
  memcpy(buffer + sizeof(int32_t), chunk->data, chunk->size);

  if (sendto(sock_fd, buffer, buf, 0, (struct sockaddr *)&recv_addr,
             sizeof(recv_addr)) == -1)
    ERROR();
  chunk->status = FILE_CHUNK_SENT;
  return 0;
}

int receiveAck(){
  int recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);

  if (recv_len == -1) {
    if (errno != EWOULDBLOCK)
      ERROR();
    // Timeout
    printf("[SENDER_TIMOUT]\n");
    return -2;
  }

  // Data format: {HEAD&LEN, BITS}
  uint32_t *head = (uint32_t *)buffer;
  if (*head == 0xffffffff) {
    return -1;
  }

  if (!(*head & 0x40000000)) {
    printf("header lost, resend header");
    sendHeader(2);
  } else {
    headerAck = 1;
    *head &= 0x3fffffff;
  }
  int ackBase = *head >> 16, ackCount = *head & 0xffff;
  int update = 0;
  if(ackBase == 0x3fff){
    for(size_t i = 0, j = 0; i < file->size; i++){
      uint16_t *k = (uint16_t *)(buffer + sizeof(int32_t) + j * sizeof(int16_t));
      if(*k == i){
        // Ignore this index since this is not received
        j++;
      }else{
        if(file->chunks[i].status == FILE_CHUNK_SENT) {
          file->chunks[i].status = FILE_CHUNK_RECEIVED;
          file->sent++;
          update++;
        }
      }
    }
    printf("[NEW%d]", update);
  }else{
    for(int i = 0; i < ackBase; i++){
      if(file->chunks[i].status == FILE_CHUNK_SENT) {
        file->chunks[i].status = FILE_CHUNK_RECEIVED;
        file->sent++;
        update++;
      }
    }
    for (int i = 0; i < ackCount; i++) {
      uint8_t k =
      *(int8_t *)(buffer + sizeof(int32_t) + i / 8 * sizeof(int8_t));
      if ((k >> (i % 8)) & 1) {
        if (file->chunks[ackBase + i].status == FILE_CHUNK_SENT) {
          file->chunks[ackBase + i].status = FILE_CHUNK_RECEIVED;
          file->sent++;
          update++;
        }
      }
    }

    printf("[ACK%d]", update);
  }

  return update;
}

int main(int argc, char **argv) {
  // argv: prog_name, host, port, fname
  if (argc != 4) {
    printUsageSender(argv[0]);
    exit(1);
  }

  // Setup connection structure
  sock_fd = setupSocketSender();
  struct timeval timeout = {0, SOCKET_OPTION_TIMEOUT_USEC};
  if(setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) ERROR();
  setupAddr(&recv_addr, argv[1], atoi(argv[2]));

  file = setupFileSender(argv[3]);
  if (pthread_create(&fileThread, NULL, &readFile, file) < 0)
    ERROR();

  sendHeader(2);

  // Send file content
  size_t currentPos = 0;

  int turn = 0;
  int acks = 1;
  int timeouts = 0;
  while (1) {
    if (turn == 1) {
      int status = receiveAck();
      if(status == -1) break; // Finished
      else if(status == -2){
        if(acks > 0 || timeouts > 5){
          turn = 0;// Out of time
          acks = 0;
        }else{
          timeouts++;
        }
      } else {
        // updated count
        if(file->sent == file->size)
          break;
        acks++;
        continue;
      }
    }

    if (currentPos < file->read) {
      sendData(currentPos);
      usleep(10);
      if(file->size - file->sent < 50)
        sendData(currentPos), usleep(3);
      if(!headerAck && currentPos % 300 == 0) sendHeader(1);
    }

    do {
      currentPos++;
      if(currentPos == file->size){
        turn = 1;
        currentPos = 0;
      }
    } while (file->chunks[currentPos].status == FILE_CHUNK_RECEIVED);

  }
  pthread_join(fileThread, NULL);

  printf("data count: %d * %d = %d\n", dataCount, FILE_CHUNK_SIZE, dataCount * FILE_CHUNK_SIZE);
  printf("required:   %d * %d = %d (%.2f%%)\n", (int)file->size, FILE_CHUNK_SIZE,
    (int)file->size * FILE_CHUNK_SIZE, dataCount * 100.0f / file->size);
  return 0;
}
