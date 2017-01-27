#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>

extern int errno;
int error()
{
    fprintf(stderr, "Error: [%d] %s\n", errno, strerror(errno));
    exit(1);
}

int main(int argc, char *argv[])
{
    int sock_fd;
    pid_t child_pid;
    struct sockaddr_in addr_in;

    if (argc < 2)
    {
        printf("Usage: %s <SERVER IP> <SERVER PORT>\n", argv[0]);
        exit(1);
    }

    bzero(&addr_in, sizeof(addr_in));
    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[2], (void *)&addr_in.sin_addr) == -1)
        error();

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1)
        error();

    if (connect(sock_fd, (struct sockaddr *)&addr_in, sizeof(struct sockaddr_in)) == -1)
        error();
    
    char buf[100];
    if(!(child_pid = fork()))
    {
        int len;
        while ((len = read(sock_fd, buf, sizeof(buf) - 1)) > 0)
        {
            buf[len] = 0;
            printf("%s\n", buf);
        }
        exit(0);
    }
    while (fgets(buf, sizeof(buf), stdin) != NULL)
    {
        int len = write(sock_fd, buf, sizeof(buf));
        if (len == -1)
            error();

    }
    close(sock_fd);
    kill(child_pid, SIGKILL);
    wait(NULL);
    return 0;
}
