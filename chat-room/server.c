#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>

#define SERVER_PORT 7777
#define SERVER_BACKLOG 24
#define SERVER_BUFFER 1000

extern int errno;
static char buffer[SERVER_BUFFER + 1];

#ifdef DEBUG
#define ERROR() error(__FILE__, __LINE__)
void error(char *fileName, int line)
{
    fprintf(stderr, "[%-10s:%-3d]Error: [%d] %s\n", fileName, line, errno, strerror(errno));
    exit(1);
}
#else
#define ERROR() error();
void error()
{
    fprintf(stderr, "Error: [%d] %s\n", errno, strerror(errno));
    exit(1);
}
#endif

typedef struct _chat_client
{
    int connection_fd;        /* Connection File Discriptor */
    char ip[INET_ADDRSTRLEN]; /* client ip in string */
    int port;                 /* client port */
    char username[13];
    struct _chat_client *next;
    struct _chat_client *prev;
} chat_client;

/* Forward declaration */
void strtrim(char *str);
void chat_msg_who(chat_client *client_cur, chat_client *client_list);
void chat_msg_err(chat_client *client_cur);
int chat_name_valid(char *name);
int chat_name_unique(chat_client *client_cur, chat_client *client_list, char *name);
void chat_msg_name(chat_client *client_cur, chat_client *client_list, char *name);
void chat_msg_tell(chat_client *client_cur, chat_client *client_list, char *command);
void chat_msg_yell(chat_client *client_cur, chat_client *client_list, char *message);
void chat_handle(char *command, chat_client *client_cur, chat_client *client_list);
void chat_msg_new(chat_client *client_cur, chat_client *client_list);
void chat_msg_delete(chat_client *client_cur, chat_client *client_list);
chat_client *chat_client_new(int socket_fd);
void chat_client_delete(chat_client **client_ptr);
int chat_startserver();

int main(int argc, char *argv[])
{
    int socket_fd, conn_n, max_fd, connection_fd;
    fd_set server_fds;
    chat_client *client_list = NULL, *client_cur = NULL;
    char cmdbuffer[SERVER_BUFFER + 1];

    /* start server */
    socket_fd = chat_startserver();

    while (1)
    {
        FD_ZERO(&server_fds);
        FD_SET(socket_fd, &server_fds); /* add the server listening file discriptor */

        max_fd = socket_fd;
        for (client_cur = client_list; client_cur; client_cur = client_cur->next)
        {
            FD_SET(client_cur->connection_fd, &server_fds);
            max_fd = max_fd > client_cur->connection_fd ? max_fd : client_cur->connection_fd;
        }

        conn_n = select(max_fd + 1, &server_fds, NULL, NULL, NULL);
        if (conn_n == -1)
        {
            if (errno == EINTR)
                continue;
            ERROR();
        }

        if (FD_ISSET(socket_fd, &server_fds))
        {
            /* New user enter */
            client_cur = chat_client_new(socket_fd);
            if (client_cur == NULL)
                ERROR();

            if (client_list)
                client_list->prev = client_cur;
            client_cur->next = client_list;
            client_list = client_cur;

            chat_msg_new(client_cur, client_list);

            if(--conn_n == 0)
                continue;
        }

        for (client_cur = client_list; client_cur; client_cur = client_cur->next)
        {
            connection_fd = client_cur->connection_fd;
            if (!FD_ISSET(connection_fd, &server_fds))
                continue;

            int n = read(connection_fd, &cmdbuffer, SERVER_BUFFER);
            if (n > 0)
            {
                /* handle client request */
                cmdbuffer[n] = '\0';
                chat_handle(cmdbuffer, client_cur, client_list);
                break;
            }

            /* client offline */
            if (client_cur->prev)
                client_cur->prev->next = client_cur->next;
            else
                client_list = client_cur->next;

            if (client_cur->next)
                client_cur->next->prev = client_cur->prev;

            chat_msg_delete(client_cur, client_list);
            chat_client_delete(&client_cur);

            if(--conn_n == 0)
                break; /* Break the user select loop */
        }
    }


    close(socket_fd);
    return 0;
}

/* Remove space character before and after string */
void strtrim(char *str)
{
    char *c = str, *p = 0;
    while (*str && isspace(*str))
        str++;
    while (*str)
        p = isspace(*c++ = *str++) ? p ? p : c - 1 : 0;
    *(p ? p : c) = 0;
}


void chat_msg_who(chat_client *client_cur, chat_client *client_list)
{
    chat_client *client;
    snprintf(buffer, SERVER_BUFFER, "[Server] Someone is coming!\n");
    for (client = client_list; client; client = client->next)
    {
        if (client == client_cur)
            snprintf(buffer, SERVER_BUFFER, "[Server] %s %s/%d ->me\n",
                     client->username,
                     client->ip,
                     client->port);
        else
            snprintf(buffer, SERVER_BUFFER, "[Server] %s %s/%d\n",
                     client->username,
                     client->ip,
                     client->port);
        write(client_cur->connection_fd, buffer, strlen(buffer));
    }
}

void chat_msg_err(chat_client *client_cur)
{
    snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: Error command.\n");
    write(client_cur->connection_fd, buffer, strlen(buffer));
}

int chat_name_valid(char *name)
{
    size_t len = strlen(name);
    if (len < 2 || len > 12)
        return 0;
    while (*name)
        if (!isalpha(*name++))
            return 0;
    return 1;
}

int chat_name_unique(chat_client *client_cur, chat_client *client_list, char *name)
{
    chat_client *client;
    for (client = client_list; client; client = client->next)
        if (client != client_cur && !strcmp(name, client->username))
            return 0;
    return 1;
}

void chat_msg_name(chat_client *client_cur, chat_client *client_list, char *name)
{

    chat_client *client;
    char oldname[13];

    if (!strcmp(name, "anonymous"))
    {
        snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: Username cannot be anonymous. \n");
        write(client_cur->connection_fd, buffer, strlen(buffer));
        return;
    }
    if (!chat_name_valid(name))
    {
        snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: Username can only consists of 2~12 English letters. \n");
        write(client_cur->connection_fd, buffer, strlen(buffer));
        return;
    }
    if (!chat_name_unique(client_cur, client_list, name))
    {
        snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: %s has been used by others. \n", name);
        write(client_cur->connection_fd, buffer, strlen(buffer));
        return;
    }

    strcpy(oldname, client_cur->username);
    strcpy(client_cur->username, name);

    snprintf(buffer, SERVER_BUFFER, "[Server] You're now known as %s\n", client_cur->username);
    write(client_cur->connection_fd, buffer, strlen(buffer));

    snprintf(buffer, SERVER_BUFFER, "[Server] %s is now known as %s.\n", oldname, client_cur->username);
    for (client = client_list; client; client = client->next)
    {
        if (client == client_cur)
            continue;
        write(client->connection_fd, buffer, strlen(buffer));
    }
}
void chat_msg_tell(chat_client *client_cur, chat_client *client_list, char *command)
{
    static char username[20];
    int pos;
    chat_client *client;

    if (sscanf(command, "%20s%n", username, &pos) == EOF)
    {
        chat_msg_err(client_cur);
        return;
    }

    strcpy(command, command + pos);
    strtrim(command);

    if (!strcmp(client_cur->username, "anonymous"))
    {
        snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: You are anonymous.\n");
        write(client_cur->connection_fd, buffer, strlen(buffer));
        return;
    }

    if (!strcmp(username, "anonymous"))
    {
        snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: The client to which you sent is anonymous.\n");
        write(client_cur->connection_fd, buffer, strlen(buffer));
        return;
    }

    for (client = client_list; client; client = client->next)
    {
        if (!strcmp(username, client->username))
        {
            snprintf(buffer, SERVER_BUFFER, "[Server] %s tell you %s\n", client_cur->username, command);
            write(client->connection_fd, buffer, strlen(buffer));

            snprintf(buffer, SERVER_BUFFER, "[Server] SUCCESS: Your message has been sent.\n");
            write(client_cur->connection_fd, buffer, strlen(buffer));
            return;
        }
    }

    snprintf(buffer, SERVER_BUFFER, "[Server] ERROR: The receiver doesn't exist.\n");
    write(client_cur->connection_fd, buffer, strlen(buffer));
}
void chat_msg_yell(chat_client *client_cur, chat_client *client_list, char *message)
{
    chat_client *client;
    snprintf(buffer, SERVER_BUFFER, "[Server] %s yell %s\n",
             client_cur->username, message);
    for (client = client_list; client; client = client->next)
        write(client->connection_fd, buffer, strlen(buffer));
}

void chat_handle(char *command, chat_client *client_cur, chat_client *client_list)
{
    static char handleBuffer[20];
    int pos;
    /*Ensure ended with newline*/
    if(command[strlen(command)-1]!='\n')
    {
        printf("'%s'\n", command);
	chat_msg_err(client_cur);
        return;
    }

    strtrim(command);

    /*Ignore empty line (ended with new line)*/
    if(strlen(command) == 0) return;

    sscanf(command, "%20s %n", handleBuffer, &pos);
    strcpy(command, command + pos);
    strtrim(command);

    if (!strcmp(handleBuffer, "who"))
    {
        if (*command == '\0')
            chat_msg_who(client_cur, client_list);
        else
            chat_msg_err(client_cur);
        return;
    }

    if (!strcmp(handleBuffer, "name"))
    {
        if (*command != '\0')
            chat_msg_name(client_cur, client_list, command);
        else
            chat_msg_err(client_cur);
        return;
    }

    if (!strcmp(handleBuffer, "tell"))
    {
        if (*command != '\0')
            chat_msg_tell(client_cur, client_list, command);
        else
            chat_msg_err(client_cur);
        return;
    }

    if (!strcmp(handleBuffer, "yell"))
    {
        if (*command != '\0')
            chat_msg_yell(client_cur, client_list, command);
        else
            chat_msg_err(client_cur);
        return;
    }

    chat_msg_err(client_cur);
}

void chat_msg_new(chat_client *client_cur, chat_client *client_list)
{
    chat_client *client;

    snprintf(buffer, SERVER_BUFFER, "[Server] Hello, anonymous! From: %s/%d\n",
             client_cur->ip,
             client_cur->port);
    write(client_cur->connection_fd, buffer, strlen(buffer));

    snprintf(buffer, SERVER_BUFFER, "[Server] Someone is coming!\n");
    for (client = client_list; client; client = client->next)
    {
        if (client == client_cur)
            continue;
        write(client->connection_fd, buffer, strlen(buffer));
    }
}

void chat_msg_delete(chat_client *client_cur, chat_client *client_list)
{
    chat_client *client;
    snprintf(buffer, SERVER_BUFFER, "[Server] %s is offline.\n", client_cur->username);
    for (client = client_list; client; client = client->next)
    {
        if (client == client_cur)
            continue;
        write(client->connection_fd, buffer, strlen(buffer));
    }
}

chat_client *chat_client_new(int socket_fd)
{
    static struct sockaddr_in client_addr;
    unsigned int client_addr_len;
    int connection_fd;

    bzero(&client_addr, sizeof(client_addr));
    client_addr_len = sizeof(client_addr);
    connection_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (connection_fd == -1)
        return 0;

    chat_client *client = (chat_client *)malloc(sizeof(chat_client));
    bzero(client, sizeof(chat_client));

    client->connection_fd = connection_fd;
    client->port = ntohs(client_addr.sin_port);
    strcpy(client->username, "anonymous");
    inet_ntop(AF_INET, (void *)&client_addr.sin_addr, client->ip, INET_ADDRSTRLEN);
    return client;
}

void chat_client_delete(chat_client **client_ptr)
{
    close((*client_ptr)->connection_fd);
    if (*client_ptr)
        free(*client_ptr);
    *client_ptr = NULL;
}

int chat_startserver()
{
    struct sockaddr_in server_addr;
    int socket_fd;

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == 0)
        ERROR();

    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
        ERROR();

    listen(socket_fd, SERVER_BACKLOG);

    return socket_fd;
}

