#include "../common/commom.h"

#define ALARM_DELAY 1
#define BUFFER_SEND 10240
#define MAX_SENDING 1000

FileHeaderSender *file = NULL;
int sock_fd;
struct sockaddr_in recv_addr;
pthread_t fileThread;
char buffer[BUFFER_SEND];

int sendHeader() {
  // Send file info (three times)
  int32_t info[1] = {(int32_t)0x40000000 | (int32_t)file->size};
  for (int i = 0; i < 5; i++) {
    if (sendto(sock_fd, info, sizeof(info), 0, (struct sockaddr *)&recv_addr,
               sizeof(recv_addr)) == -1)
      ERROR();
    usleep(1000);
  }
  return 0;
}

int sendData(size_t position){
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
  printf("[SNT%ld]", position);
  return 0;
}

int receiveAck(){
  // Wait for a package
  alarm(ALARM_DELAY);
  // ualarm(1000 * 100, 0);
  int recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);

  if (recv_len == -1) {
    if (errno != EINTR)
    ERROR();

    printf("Alarm arrival\n");
    return -2;
  }

  // Receive response
  //ualarm(0, 0);
  alarm(0);

  // Data format: {HEAD&LEN, BITS}
  uint32_t *head = (uint32_t *)buffer;
  if (*head == 0xffffffff) {
    printf("Finish\n");
    return -1;
  }

  if (!(*head & 0x40000000)) {
    printf("header lost, resend header");
    sendHeader();
  } else {
    *head &= 0x3fffffff;
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
        file->chunks[i].status = FILE_CHUNK_RECEIVED;
        file->sent++;
        update++;
      }
    }
  }

  printf("[ACK%d]", update);
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

  signal(SIGALRM, SIGALRM_handler);
  siginterrupt(SIGALRM, 1);

  file = setupFileSender(argv[3]);
  if (pthread_create(&fileThread, NULL, &readFile, file) < 0)
    ERROR();

  sendHeader();

  // Send file content
  size_t currentPos = 0;


  int turn = 0;
  while (1) {
    if (turn == 1) {
      int status = receiveAck();
      if(status == -1) break; // Finished
      else if(status == -2) turn = 0;// Out of time
      else {
        // updated count
        if(file->sent == file->size)
          break;
        continue;
      }
    }

    do {
      currentPos++;
      if(currentPos == file->size){
        turn = 1;
        currentPos = 0;
      }
    } while (file->chunks[currentPos].status == FILE_CHUNK_RECEIVED);

    if (currentPos < file->read) {
      sendData(currentPos);
      usleep(100);
      if(file->size - file->sent < 50)
        sendData(currentPos), usleep(100);
    }
  }
  pthread_join(fileThread, NULL);
  return 0;
}
