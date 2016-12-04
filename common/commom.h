#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <pthread.h>

extern int errno;

#ifdef DEBUG
#define ERROR() error(__FILE__, __LINE__)
void error(char *fileName, int line) {
  fprintf(stderr, "[%-10s:%-3d]Error: [%d] %s\n", fileName, line, errno,
          strerror(errno));
  exit(1);
}
#else
#define ERROR() error();
void error() {
  fprintf(stderr, "Error: [%d] %s\n", errno, strerror(errno));
  exit(1);
}
#endif

void printUsageSender(char *name) {
  printf("USAGE: %s <HostName> <Port> <ReadFrom>\n", name);
}

void printUsageReceiver(char *name) {
  printf("USAGE: %s <Port> <SaveAs>\n", name);
}

void setupAddr(struct sockaddr_in *addr, char *hostname, int port) {

  bzero(addr, sizeof(struct sockaddr_in));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);

  struct hostent *hostentity;
  hostentity = gethostbyname(hostname);
  if (hostentity == NULL)
    ERROR();
  memcpy((void *)&addr->sin_addr, (void *)hostentity->h_addr_list[0],
         sizeof(struct in_addr));
}

int setupSocketSender() {
  int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock_fd < 0)
    ERROR();
  return sock_fd;
}

int setupSocketReceiver(int port) {
  int sock_fd = setupSocketSender();
  int enable = 1;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) <
      0) {
    ERROR();
  }
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) <
      0) {
    ERROR();
  }
  struct sockaddr_in recv_addr;
  bzero(&recv_addr, sizeof(recv_addr));
  recv_addr.sin_family = AF_INET;
  recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  recv_addr.sin_port = htons(port);

  if (bind(sock_fd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
    ERROR();
  }

  return sock_fd;
}

#define FILE_CHUNK_SIZE 512

#define FILE_CHUNK_UNSENT   0
#define FILE_CHUNK_SENT     1
#define FILE_CHUNK_UNRECEIVED 0 
#define FILE_CHUNK_RECEIVED 2

typedef struct {
  int index;
  int status;
  size_t size;
  char data[FILE_CHUNK_SIZE];
} FileChunk;

typedef struct {
  int fd;
  int readIndex;
  size_t size;
  size_t written;
  size_t received;
  FileChunk chunks[0];
} FileHeaderReceiver;

typedef struct {
  int fd;
  size_t size;
  size_t read;
  size_t sent;
  FileChunk chunks[0];
} FileHeaderSender;

FileHeaderSender *setupFileSender(char *path) {
  // Open file
  int file_fd = open(path, O_RDONLY);
  if (file_fd == -1)
    ERROR();
  // Load file info
  struct stat fileStat;
  if (fstat(file_fd, &fileStat) == -1)
    ERROR();
  size_t fileSize = fileStat.st_size;
  size_t blockSize = fileStat.st_blksize;
  printf("file size: %ld;block size: %ld\n", fileSize, blockSize);
  // Allocate Memory for file storage
  size_t chunkFileSize = (fileSize - 1) / FILE_CHUNK_SIZE + 1;
  size_t sizeHeader =
      sizeof(FileHeaderSender) + chunkFileSize * sizeof(FileChunk);
  FileHeaderSender *header = (FileHeaderSender *)malloc(sizeHeader);
  bzero(header, sizeHeader);

  header->fd = file_fd;
  header->size = chunkFileSize;
  return header;
}

int setupFileReceiver(char *path) {
  int file_fd = open(path, O_RDWR | O_TRUNC | O_CREAT, 0644);
  if (file_fd == -1)
    ERROR();

  return file_fd;
}
