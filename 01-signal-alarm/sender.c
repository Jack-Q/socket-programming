#include "../common/commom.h"

#define ALARM_DELAY 1
#define BUFFER_SEND 1024
#define MAX_SENDING 100

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
  int32_t info[1] = {
    (int32_t) 0xff000000 & (int32_t)file->size
  };
  for(int i =0; i < 3; i++){
    if (sendto(sock_fd, info, sizeof(info), 0, (struct sockaddr *)&recv_addr,
               sizeof(recv_addr)) == -1)
      ERROR();
    usleep(5);
  }

  // Send file content
  int sending = 0;
  int turn = 0;
  size_t currentPos = 0;
  char buffer[BUFFER_SEND];
  bzero(&buffer, sizeof(buffer));

  while(file->sent < file->read){
    if(sending < MAX_SENDING && currentPos < file->read){
      // Send a package
      FileChunk *chunk = file->chunks + currentPos;
      size_t buf = sizeof(int32_t) + chunk->size;
      int32_t* head = (int32_t *)buffer;
      *head = chunk->size;
      memcpy(buffer + sizeof(int32_t), chunk->data, chunk->size);

      if (sendto(sock_fd, buffer, buf, 0, (struct sockaddr *)&recv_addr,
                 sizeof(recv_addr)) == -1)
        ERROR();
      chunk->status = FILE_CHUNK_SENT;
      sending++;
      printf("Sent chunk %ld\n", currentPos);
      if(turn == 0){
        currentPos++;
        if(currentPos == file->size) turn = 1, currentPos = 0;
      }
    }else if(sending >= MAX_SENDING){
      // Wait for a package
      sighandler_t orig_handler = signal(SIGALRM, SIGALRM_handler);
      siginterrupt(SIGALRM, 1);
      alarm(ALARM_DELAY);
      int recv_len = recvfrom(sock_fd, buffer, sizeof(buffer), 0, NULL, 0);
      if (recv_len == -1){
        if(errno == EINTR)
          printf("Alarm arrival\n");
        else
          ERROR();
      }else{
        // Receive response
        alarm(0);
        // Data format: {HEAD&LEN, BITS}
        int32_t* head = (int32_t*)buffer;
        if(!(*head & 0xff000000)){
          printf("header lost, resend header");
        }else{
          *head &= 0x00ffffff;
        }
        for(int i = 0; i < *head; i++){
          int8_t k = *(int8_t *)(buffer + sizeof(int32_t) + i / 8);
          if((k >> (i%8)) & 1){
            if(file->chunks[i].status == FILE_CHUNK_SENT){
              printf("FILE CHUNK %d ACK\n", i);
              file->chunks[i].status = FILE_CHUNK_RECEIVED;
              file->sent++;
            }
          }
        }
      }
      signal(SIGALRM, orig_handler);
    }else{
      // Wait disk read
      printf("[WARNING] WAITING FOR DISK\n");
    }
    usleep(10);
    if(turn == 0) continue;
    // TODO: Select next item
  }
  return 0;
}
