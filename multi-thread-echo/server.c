#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

extern int errno;

#define BACK_LOG 24

void error()
{
    fprintf(stderr, "Error %d: %s\n", errno, strerror(errno));
    exit(1);
}

void childProcess(int conn_fd){
    char buf[100];
    int n;
    while ((n = read(conn_fd, buf, sizeof(buf) - 1)))
    {
        buf[n - 1] = 0;
        if (strchr(buf, '\n'))
            *strchr(buf, '\n') = 0;
        printf("%s\n", buf);
        n = write(conn_fd, buf, strlen(buf));
        if(n < 0)
            error();
    }
    printf("{SVC} Close Connection\n");
    close(conn_fd);
    exit(0);
}

/**
 * Handler for close child correctly
 */
void SIGCHLD_handler(int sig)
{
    printf("{SVC} CHILD Terminated, SIG_id: %d\n", sig);
    pid_t pid;
    int stat;
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
    {
        printf("{SVC} CHILD with id %d termianted with status %d.\n", pid, stat);
    }
}
void SIGPIPE_handler(int sig){
    printf("{SVC} PIPE Catched, SIG_id: %d \n", sig);
}
void SIGINT_handler(int sig)
{
    printf("{SVC} ^C Catched, SIG_id: %d \n", sig);
    exit(0);
}
void SIGKILL_handler(int sig)
{
    printf("{SVC} KILL Catched, SIG_id: %d \n", sig);
}
void SIGTERM_handler(int sig)
{
    printf("{SVC} TERM Catched, SIG_id: %d \n", sig);
}

int main(int argc, char *argv[])
{
    int sock_fd, conn_fd;
    socklen_t addr_len;
    struct sockaddr_in addr_in, addr_re;

    signal(SIGINT, SIGINT_handler);
    signal(SIGKILL, SIGKILL_handler);
    signal(SIGTERM, SIGTERM_handler);
    signal(SIGCHLD, SIGCHLD_handler);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1)
        error();

    addr_in.sin_family = AF_INET;
    addr_in.sin_port = htons(2111);
    addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock_fd, (struct sockaddr *)&addr_in, sizeof(struct sockaddr_in)) == -1)
        error();

    if (listen(sock_fd, BACK_LOG) == -1)
        error();

    while (1)
    {
	addr_len = sizeof(addr_re);
        conn_fd = accept(sock_fd, (struct sockaddr *)&addr_re, &addr_len);
        if (conn_fd == -1)
        {
            if (errno == EINTR)
                continue;
            error();
        }
        pid_t child = fork();
        if(child == -1){
            close(sock_fd);
            error();
        }
        if(child == 0)
            childProcess(conn_fd);
        close(conn_fd);
    }

    return 0;
}
