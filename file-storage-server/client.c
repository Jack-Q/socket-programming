/****************************************************
 * Network Programming: File Storage Server
 * Client Program (client.c)
 * Jack Q (qiaobo@outlook.com) 0540017, CS, NCTU
 ****************************************************/
#include "common.h"

#define DELIMITER " \r\n\t"
int buffer_send_pos = 0;
char buffer_send[BUFFER_SIZE];
int buffer_read_pos = 0;
char buffer_read[BUFFER_SIZE];

#define CMD_BUFFER_SIZE 150
int cmdBuffer_pos = 0;
char cmdBuffer[CMD_BUFFER_SIZE];

void print_usage() { printf("USAGE: ./client <IP> <PORT> <USERNAME>"); }

void client_put_file_header(char *name, uint32_t filesize) {
  size_t size = 0;
  uint16_t len = strlen(name) + 1; // include the \0 + filesize
  char *p = buffer_send + buffer_send_pos;
  *p++ = PKG_TYPE_FILE, size++;                 // Package type
  *p++ = 0, size++;                             // Package subtype (NULL)
  *(uint16_t *)p = len + 4, p += 2, size += 2;  // Package size
  *(uint32_t *)p = filesize, p += 4, size += 4; // file size
  memcpy(p, name, len), size += len;            // file name
  buffer_send_pos += size;
}

void clinet_put_file(int sock, char *filename) {
  if (access(filename, R_OK) == -1) {
    printf("failed to read file %s: %s \n", filename, strerror(errno));
    return;
  }

  int file_fd = open(filename, O_RDONLY);
  if (file_fd == -1)
    ERROR();
  // Load file info
  struct stat fileStat;
  if (fstat(file_fd, &fileStat) == -1)
    ERROR();
  uint32_t filesize = (uint32_t)fileStat.st_size;

  if (filesize == 0) {
    printf("failed to send file %s: the file is empty!\n", filename);
    return;
  }

  client_put_file_header(filename, filesize);

  printf("Uploading file : %s\n", filename);
  uint32_t pos = 0;
  int val;
  while (1) {
    showProgress((unsigned long)pos * 100 / filesize);

    val = write(sock, buffer_send, buffer_send_pos);
    if (val == -1 && errno == EWOULDBLOCK) {
      usleep(100);
      continue;
    }
    if (val == -1)
      ERROR();
    if (val < buffer_send_pos) {
      memcpy(buffer_send, buffer_send + val, buffer_send_pos - val);
    }
    buffer_send_pos -= val;

    if (buffer_send_pos == 0) {
      if (pos == filesize)
        break;
      buffer_send[0] = PKG_TYPE_DATA;
      int val = read(file_fd, buffer_send + 4, BUFFER_SIZE - 4);
      if (val == -1)
        ERROR();
      *(uint16_t *)(buffer_send + 2) = val;
      buffer_send_pos = val + 4;
      pos += val;
    }
  }
  showProgress(100);
  printf("\n");
  printf("Upload %s complete!\n", filename);

  return;
}

int client_sleep(int length) {
  printf("Clinet starts to sleep\n");
  for (int i = 1; i <= length; i++) {
    printf("Sleep %d\n", i);
    sleep(1);
  }
  printf("Client wakes up\n");
  return 0;
}

int client_exit() {
  exit(0);
  return 0;
}

int client_handle_command(char *cmdStr) {
  strtrim(cmdStr);
  if (strlen(cmdStr) == 0)
    return -4;
  char *cmd = strtok(cmdStr, DELIMITER);
  if (strcmp(cmd, "/put") == 0) {
    if ((cmd = strtok(NULL, DELIMITER)) != NULL) {
      if (strtok(NULL, DELIMITER) == NULL) {
        strcpy(cmdStr, cmd);
        return -3;
      }
    }
    return -1;
  }
  if (strcmp(cmd, "/sleep") == 0) {
    if ((cmd = strtok(NULL, DELIMITER)) != NULL) {
      int time = atoi(cmd);
      if (time > 0 && strtok(NULL, DELIMITER) == NULL)
        return time;
    }
    return -1;
  }
  if (strcmp(cmd, "/exit") == 0) {
    if (strtok(NULL, DELIMITER) == NULL) {
      return -2;
    }
    return -1;
  }
  return -1;
}

int client_get_socket(char *host, int port) {
  int sock = get_socket();
  if (sock < 0)
    ERROR();

  /* Setup connection socket info */
  struct sockaddr_in server_sockaddr;
  bzero(&server_sockaddr, sizeof(server_sockaddr));
  server_sockaddr.sin_family = AF_INET;
  server_sockaddr.sin_port = htons(port);

  /* find ip address from hostname or ip */
  struct hostent *hostentity;
  hostentity = gethostbyname(host);
  if (hostentity == NULL)
    ERROR();
  memcpy((void *)&server_sockaddr.sin_addr, (void *)hostentity->h_addr_list[0],
         sizeof(struct in_addr));

  /* connect to server */
  if (connect(sock, (struct sockaddr *)&server_sockaddr,
              sizeof(server_sockaddr)) == -1)
    if (errno != EINPROGRESS) // This will return in pregress
      ERROR();

  return sock;
}

// Write to buffer, send till the data is ready
void client_send_init(char *name) {
  size_t size = 0;
  uint16_t len = strlen(name) + 1; // include the \0
  char *p = buffer_send + buffer_send_pos;
  *p++ = PKG_TYPE_INIT, size++;            // Package type
  *p++ = 0, size++;                        // Package subtype (NULL)
  *(uint16_t *)p = len, p += 2, size += 2; // Package size
  memcpy(p, name, len), size += len;       // write data
  buffer_send_pos += size;
}

char client_user_name[100];

int main(int argc, char *argv[]) {
  // command line format: name ip port username
  if (argc != 4) {
    print_usage();
    exit(0);
  }
  if (strlen(argv[3]) > 80) {
    printf("ERROR: username to long\n");
    exit(1);
  }
  int val;
  if ((val = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1)
    ERROR();
  if (fcntl(STDIN_FILENO, F_SETFL, val | O_NONBLOCK) == -1)
    ERROR();
  // if ((val = fcntl(STDOUT_FILENO, F_GETFL, 0)) == -1)
  //   ERROR();
  // if (fcntl(STDOUT_FILENO, F_SETFL, val | O_NONBLOCK) == -1)
  //   ERROR();

  // Set user name
  strcpy(client_user_name, argv[3]);
  strtrim(client_user_name);

  int sock = client_get_socket(argv[1], atoi(argv[2]));

  client_send_init(client_user_name);
  client_file_t *file = (client_file_t *)malloc(sizeof(client_file_t));
  fd_set fds_read, fds_write;
  int isReceiving = 0;
  printf("Welcome to the dropbox-like server! : %s\n", client_user_name);
  while (1) {
    FD_ZERO(&fds_read);
    FD_ZERO(&fds_write);
    // process user input after uploading progress
    if (!isReceiving)
      FD_SET(STDIN_FILENO, &fds_read);
    // FD_SET(STDOUT_FILENO, &fds_write);
    FD_SET(sock, &fds_read);
    FD_SET(sock, &fds_write);
    if (select(sock + 1, &fds_read, &fds_write, NULL, NULL) < 0) {
      if (errno != EWOULDBLOCK)
        ERROR();
      usleep(100);
      continue;
    }

    // Handle user input
    if (FD_ISSET(STDIN_FILENO, &fds_read)) {
      val = read(STDIN_FILENO, cmdBuffer + cmdBuffer_pos,
                 CMD_BUFFER_SIZE - cmdBuffer_pos);
      if (val == -1 && errno == EWOULDBLOCK)
        continue;
      if (val == -1)
        ERROR();
      while (1) {
        int pos = 0;
        cmdBuffer_pos += val;
        while (pos < cmdBuffer_pos) {
          if (cmdBuffer[pos] == '\n') {
            // Handle command
            cmdBuffer[pos++] = 0;
            switch (val = client_handle_command(cmdBuffer)) {
            case -1: // Error command
              printf("UNKNOWN COMMAND\n");
              break;
            case -2: // QUIT
              client_exit();
              break;
            case -3: // SEND FILE
              clinet_put_file(sock, cmdBuffer);
              break;
            case -4: // Empty
              break;
            default:
              client_sleep(val);
            }
            if (pos != cmdBuffer_pos) {
              memcpy(cmdBuffer, cmdBuffer + pos, cmdBuffer_pos - pos);
            }
            cmdBuffer_pos -= pos;
            pos = -1;
            break;
          }
          pos++;
        }
        if (pos == cmdBuffer_pos)
          break;
        val = 0;
      }
    }

    // Handle socket data receive
    if (FD_ISSET(sock, &fds_read)) {
      val = read(sock, buffer_read + buffer_read_pos,
                 BUFFER_SIZE - buffer_read_pos);
      if (val == -1 && errno == EWOULDBLOCK)
        continue;
      if (val == -1 && errno == ECONNRESET) {
        printf("Connection reset, client quit...\n");
      }

      if (val == 0 && BUFFER_SIZE - buffer_read_pos != 0) {
        printf("Server closed the connection, quit..\n");
        exit(0);
      }
      if (val == -1)
        ERROR();
      while (1) {
        buffer_read_pos += val;
        if (buffer_read_pos > 4 &&
            PKG_SIZE_GET(buffer_read) + 4 <= buffer_read_pos) {
          // received an complete package
          int pkg_size = PKG_SIZE_GET(buffer_read) + 4;

          switch (buffer_read[0]) {
          case PKG_TYPE_FILE:
            // Create file stub
            file->size = *(uint32_t *)(buffer_read + 4);
            strcpy(file->name, buffer_read + 8);
            file->pos = 0;
            file->fd = open(file->name, O_RDWR | O_TRUNC | O_CREAT, 0644);
            if (file->fd == -1)
              ERROR();
            printf("Downloading file: %s\n", file->name);
            isReceiving = 1;
            break;
          case PKG_TYPE_DATA:
            if (file->fd == 0) {
              printf("%d\n", PKG_SIZE_GET(buffer_read));
              break;
            }
            if (write(file->fd, buffer_read + 4, PKG_SIZE_GET(buffer_read)) ==
                -1) {
              printf("BAD FD: %d \n", file->fd);
              ERROR();
            }
            file->pos += PKG_SIZE_GET(buffer_read);
            if (file->pos == file->size) {
              // finish
              showProgress(100);
              printf("\n");
              printf("Download %s complete!\n", file->name);
              close(file->fd);
              *file->name = 0;
              file->size = 0;
              file->pos = 0;
              file->size = 0;
              isReceiving = 0;
            } else {
              // Progress
              showProgress((unsigned long)file->pos * 100 / file->size);
            }
            break;
          }

          if (pkg_size < buffer_read_pos) {
            memcpy(buffer_read, buffer_read + pkg_size,
                   buffer_read_pos - pkg_size);
          }
          buffer_read_pos -= pkg_size;

        } else if (buffer_read_pos == BUFFER_SIZE) {
          printf("Error, receive buffer overflow");
          errno = ENOBUFS;
          ERROR();
        } else {
          // no complete package received
          break;
        }
        val = 0;
      }
    }

    // Handle socket available to write
    if (FD_ISSET(sock, &fds_write)) {
      if (buffer_send_pos > 0) {
        // Contains data to send
        val = write(sock, buffer_send, buffer_send_pos);
        if (val == -1 && errno == EWOULDBLOCK)
          continue;
        if (val == -1)
          ERROR();
        if (val < buffer_send_pos) {
          memcpy(buffer_send, buffer_send + val, buffer_send_pos - val);
        }
        buffer_send_pos -= val;
      } else {
        usleep(100);
      }
    }
  }

  if (sock) {
    close(sock);
  }
  return 0;
}
