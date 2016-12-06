#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
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

typedef void (*sighandler_t)(int);
void SIGALRM_handler(int sig) { return; }

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
  int buf_size = 200 * 1000;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) ==
      -1) {
    ERROR();
  }
  buf_size = 100 * 1000;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size)) ==
      -1) {
    ERROR();
  }
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

#define FILE_CHUNK_SIZE 1400

#define FILE_CHUNK_UNSENT 0
#define FILE_CHUNK_SENT 1
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
  size_t size;
  size_t written;
  size_t received;
  int received_lo; // first unfilled posotion
  int received_hi; // next position of last filled
  FileChunk chunks[0];
} FileHeaderReceiver;

typedef struct {
  int fd;
  size_t size;
  size_t read;
  size_t sent;
  FileChunk chunks[0];
} FileHeaderSender;

void updateReceiveIndexRange(FileHeaderReceiver *recv, int cur) {
  recv->received_hi = recv->received_hi > cur ? recv->received_hi : cur + 1;
  if (recv->received_lo == cur)
    while (recv->received_lo < (int)recv->size &&
           recv->chunks[recv->received_lo].status == FILE_CHUNK_RECEIVED)
      recv->received_lo++;
}

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

FileHeaderReceiver *setupFileReceiver(char *path, size_t file_size) {
  int file_fd = open(path, O_RDWR | O_TRUNC | O_CREAT, 0644);
  if (file_fd == -1)
    ERROR();
  size_t sizeHeader =
      sizeof(FileHeaderReceiver) + file_size * sizeof(FileChunk);
  FileHeaderReceiver *header = (FileHeaderReceiver *)malloc(sizeHeader);
  bzero(header, sizeHeader);

  header->fd = file_fd;
  header->size = file_size;
  for (size_t i = 0; i < header->size; i++) {
    header->chunks[i].index = i;
    header->chunks[i].status = FILE_CHUNK_UNRECEIVED;
    header->chunks[i].size = 0;
  }
  return header;
}

void *readFile(void *fileHeader) {
  FileHeaderSender *file = (FileHeaderSender *)fileHeader;
  while (file->read < file->size) {
    int readSize =
        read(file->fd, file->chunks[file->read].data, FILE_CHUNK_SIZE);
    if (readSize == -1) {
      if (errno == EINTR)
        continue;
      else
        ERROR();
    }
    file->chunks[file->read].index = file->read;
    file->chunks[file->read].size = readSize;
    file->chunks[file->read].status = FILE_CHUNK_UNSENT;
    file->read++;
    // printf("Read chunk %ld with size %d\n", file->read, readSize);
  }
  pthread_exit(0);
}

void *writeFile(void *fileHeader) {
  FileHeaderReceiver *file = (FileHeaderReceiver *)fileHeader;
  for (size_t i = 0; i < file->size; i++) {
    while (file->chunks[i].status != FILE_CHUNK_RECEIVED) {
      usleep(1000);
    }
    while (1) {
      int size = write(file->fd, file->chunks[i].data, file->chunks[i].size);
      if (size != -1)
        break;
      if (errno != EINTR)
        ERROR();
    }
  }
  pthread_exit(0);
}
