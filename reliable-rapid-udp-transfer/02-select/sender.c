/**************************************
 * Network Programming Homework II
 *  Jack Q (0540017) qiaobo@outlook.com
 ****************************************/

#include "../common/commom.h"

#define SELECT_DELAY_USEC (100*1000)
#define BUFFER_SEND 5120

FileHeaderSender *file = NULL;
int sock_fd;
struct sockaddr_in recv_addr;
pthread_t fileThread;
char buffer[BUFFER_SEND];
int headerAck = 0;

int sendHeader() {
  // Send file info (three times)
  int32_t info[1] = {(int32_t)0x40000000 | (int32_t)file->size};
  for (int i = 0; i < 5; i++) {
    if (sendto(sock_fd, info, sizeof(info), 0, (struct sockaddr *)&recv_addr,
               sizeof(recv_addr)) == -1)
      ERROR();
    usleep(50);
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
  // Wait for a package
  int recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);

  if (recv_len == -1) {
    ERROR();
  }

  // Data format: {HEAD&LEN, BITS}
  uint32_t *head = (uint32_t *)buffer;
  if (*head == 0xffffffff) {
    return -1;
  }

  if (!(*head & 0x40000000)) {
    printf("header lost, resend header");
    sendHeader();
  } else {
    *head &= 0x3fffffff;
    headerAck = 1;
  }
  int ackBase = *head >> 16, ackCount = *head & 0xffff;
  int update = 0;
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

  // printf("[ACK%d]", update);
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
  setupAddr(&recv_addr, argv[1], atoi(argv[2]));

  fd_set fds;
  struct timeval select_timeout;

  file = setupFileSender(argv[3]);
  if (pthread_create(&fileThread, NULL, &readFile, file) < 0)
    ERROR();

  sendHeader();


  // Send file content
  size_t currentPos = 0;

  int turn = 0;
  while (1) {
    if (turn == 1) {
      FD_ZERO(&fds);
      FD_SET(sock_fd, &fds);
      select_timeout.tv_sec = 0;
      select_timeout.tv_usec = SELECT_DELAY_USEC;
      int status = select(sock_fd + 1, &fds, NULL, NULL, &select_timeout);
      if(status == -1) ERROR();
      if(status == 0) turn = 0;
      else {
        status = receiveAck();
        if(status == -1) break; // Finished
        else {
          // updated count
          if(file->sent == file->size)
          break;
          continue;
        }
      }
    }

    if (currentPos < file->read) {
      sendData(currentPos);
      usleep(100);
      if(file->size - file->sent < 50)
        sendData(currentPos), usleep(100);
      if(!headerAck && currentPos % 700 == 0) sendHeader();
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
  #ifdef DEBUG
  printf("data count: %d * %d = %d\n", dataCount, FILE_CHUNK_SIZE, dataCount * FILE_CHUNK_SIZE);
  printf("required:   %d * %d = %d (%.2f%%)\n", (int)file->size, FILE_CHUNK_SIZE,
    (int)file->size * FILE_CHUNK_SIZE, dataCount * 100.0f / file->size);
  #endif
  return 0;
}
