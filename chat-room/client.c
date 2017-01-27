#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CLIENT_BUFFER 1000
extern int errno;
static char buffer[CLIENT_BUFFER + 1];

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

void printusage(char *clientName) {
  printf("Usage: %s <SERVER IP> <SERVER PORT>\n", clientName);
}

int isExit(char *command)
{
    static char* cmd = "exit";
    char* cmd_pos = cmd;
    while(isblank(*command))command++;
    while(*command && *cmd && (*cmd_pos == *command))command++, cmd_pos++;
    if (*cmd_pos ==0) {
        while(isblank(*command)) command++;
        if(*command == '\n') return 1;
    }
    return 0;

}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printusage(*argv);
    exit(1);
  }

  /* Setup connection socket info */
  struct sockaddr_in server_sockaddr;
  bzero(&server_sockaddr, sizeof((server_sockaddr)));
  server_sockaddr.sin_family = AF_INET;
  server_sockaddr.sin_port = htons(atoi(argv[2]));

  /* find ip address from hostname or ip */
  struct hostent *hostentity;
  hostentity = gethostbyname(argv[1]);
  if (hostentity == NULL) {
    ERROR();
  }
  memcpy((void *)&server_sockaddr.sin_addr, (void *)hostentity->h_addr_list[0],
         sizeof(struct in_addr));

  /* parse ip from string to numeric form*/
  /* if (inet_pton(AF_INET, argv[1], (void *)&server_sockaddr.sin_addr) ==
     -1) ERROR();*/

  /* try to connect to server */
  int socket_fd;
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == 0) ERROR();

  if (connect(socket_fd, (struct sockaddr *)&server_sockaddr,
              sizeof(server_sockaddr)) == -1)
    ERROR();

  /* connected to server, accept user input and pass to server */
  fd_set select_fds;
  int select_val;
  while (1) {
    FD_ZERO(&select_fds);
    FD_SET(0, (&select_fds));
    FD_SET(socket_fd, (&select_fds));

    select_val = select(socket_fd + 1, &select_fds, 0, 0, 0);

    if (select_val == -1) {
      if (errno == EINTR) continue;
      ERROR();
    }

    if (FD_ISSET(0, &select_fds)) {
      /* Handle user input */
      if (fgets(buffer, sizeof(buffer), stdin) == 0) {
        continue;
      }
      if (isExit(buffer)) {
        break;
      }
      size_t len = strlen(buffer);
      buffer[len++] = '\n';
      write(socket_fd, buffer, len);
    }

    if (FD_ISSET(socket_fd, &select_fds)) {
      ssize_t n = read(socket_fd, &buffer, CLIENT_BUFFER);
      /* Since all of the data between client and server end with \n*/
      if (n > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
      }
      if(n == 0){
        /* server closed */
        break;
      }
    }
  }

  /* close resource */
  close(socket_fd);

  return 0;
}

