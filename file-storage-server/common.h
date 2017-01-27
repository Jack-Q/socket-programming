#ifndef COMMON_HEADER
#define COMMON_HEADER

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
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

/**************************************************
 * Trim leading and following space characters
 * the string should be terminate with \0
 **************************************************/
void strtrim(char *str) {
  char *c = str, *p = 0;
  while (*str && isspace(*str))
    str++;
  while (*str)
    p = isspace(*c++ = *str++) ? p ? p : c - 1 : 0;
  *(p ? p : c) = 0;
}

/**************************************************
 * Print a progress bar
 * should be used in a empty line consecutively
 **************************************************/
#define PROGRESS_WIDTH 50
void showProgress(int percentage) {
  static int progress = -1;
  if (percentage == progress)
    return;
  progress = percentage;
  printf("\rProgress : [");
  for (int i = 0; i < PROGRESS_WIDTH; i++) {
    printf("%c", PROGRESS_WIDTH * percentage > i * 100 ? '#' : ' ');
  }
  printf("]");
  fflush(stdout);
}

int get_socket() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;
  // Set to non-blocking mode
  int flag = fcntl(sock, F_GETFL, 0);
  if (flag < 0)
    return -1;

  if (fcntl(sock, F_SETFL, flag | O_NONBLOCK) < 0)
    return -1;

  return sock;
}

/**********************************************
 * Protocol Definition
 **********************************************/
#define PKG_TYPE_INIT 0x71 /* [INIT][0][SIZE][NAME][0] */
#define PKG_TYPE_FILE 0x72 /* [FILE][0][SIZE][FSIZE][FNAME][0] */
#define PKG_TYPE_DATA 0x73 /* [DATA][0][SIZE][DATA] */
// #define PKG_TYPE_FEND 0x74 /* [FEND][0][SIZE] */
#define PKG_SIZE_GET(pkg) (*((uint16_t *)pkg + 1))
#define BUFFER_SIZE 1400
typedef enum {
  FILE_RECEIVING,
  FILE_CLOSED,
} file_status_t;

typedef struct {
  char filename[100];
  char localfile[100];
  int size;
  int fd;
  file_status_t status;
} file_t;

typedef enum {
  TSK_RECV_FILE,
  TSK_SEND_FILE,
} task_type_t;

typedef struct __task_t {
  task_type_t type;
  file_t *file;
  int pos;
  struct __task_t *nxt;
} task_t;

struct __user_t;
typedef struct __client_t {
  int sock;
  int port;
  char ip[INET_ADDRSTRLEN];
  struct __user_t *user;

  int send_buffer_pos;
  char *send_buffer;

  int recv_buffer_pos;
  char *recv_buffer;

  task_t *tasks;

  struct __client_t *prev;
  struct __client_t *next;
} client_t;

typedef struct __user_t {
  char username[100];
  int file_count;
  file_t *files[100];
  int client_count;
  client_t *client_list;
} user_t;

typedef struct {
  int fd;
  int size;
  int pos;
  char name[100];
} client_file_t;

#endif
