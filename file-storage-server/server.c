/****************************************************
 * Network Programming: File Storage Server
 * Server Program (server.c)
 * Jack Q (qiaobo@outlook.com) 0540017, CS, NCTU
 ****************************************************/

#include "common.h"
#define SERVER_BACKLOG 30
#define SERVER_USER_LIMIT 100

int server_get_socket(int port) {
  struct sockaddr_in server_addr;
  int sock = get_socket();
  if (sock == -1)
    ERROR();

  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    ERROR();

  listen(sock, SERVER_BACKLOG);

  int flag = fcntl(sock, F_GETFL, 0);
  if (flag < 0)
    return -1;

  if (fcntl(sock, F_SETFL, flag | O_NONBLOCK) < 0)
    return -1;

  return sock;
}

client_t *server_new_client(int sock) {
  static struct sockaddr_in client_addr;
  unsigned int client_addr_len;
  int connection_fd;

  bzero(&client_addr, sizeof(client_addr));
  client_addr_len = sizeof(client_addr);
  connection_fd =
      accept(sock, (struct sockaddr *)&client_addr, &client_addr_len);
  if (connection_fd == -1)
    return NULL;
  int flag = fcntl(connection_fd, F_GETFL, 0);
  if (flag < 0)
    return NULL;

  if (fcntl(connection_fd, F_SETFL, flag | O_NONBLOCK) < 0)
    return NULL;

  client_t *client = (client_t *)malloc(sizeof(client_t));
  bzero(client, sizeof(client_t));

  client->sock = connection_fd;
  client->port = ntohs(client_addr.sin_port);
  client->user = NULL;
  client->send_buffer = (char *)malloc(BUFFER_SIZE);
  client->recv_buffer = (char *)malloc(BUFFER_SIZE);
  inet_ntop(AF_INET, (void *)&client_addr.sin_addr, client->ip,
            INET_ADDRSTRLEN);
  return client;
}

void server_client_add_task(client_t *cli, task_t *tsk) {
  tsk->nxt = 0;
  if (cli->tasks == 0) {
    cli->tasks = tsk;
  } else {
    task_t *task = cli->tasks;
    while (task->nxt)
      task = task->nxt;
    task->nxt = tsk;
  }
}
void server_client_add_task_first(client_t *cli, task_t *tsk) {
  tsk->nxt = cli->tasks;
  cli->tasks = tsk;
}

void server_client_remove_task(client_t *cli) {
  if (cli->tasks) {
    task_t *tsk = cli->tasks, *nxt;
    if (tsk->type == TSK_RECV_FILE) {
      // Close file
      if (tsk->file->fd > 1) {
        close(tsk->file->fd);

        // if only a fragment of file is received, delete file
        if (tsk->file->size < tsk->pos) {
          unlink(tsk->file->filename);
          free(tsk->file);
        }
      }
    }
    nxt = tsk->nxt;
    free(tsk);
    cli->tasks = nxt;
  }
}

void server_add_recv_task(client_t *cli, int filesize, char *filename) {
  printf("Add receive task: %s to user %s \n", filename, cli->user->username);

  task_t *tsk = (task_t *)malloc(sizeof(task_t));
  bzero(tsk, sizeof(task_t));
  file_t *file = (file_t *)malloc(sizeof(file_t));
  bzero(file, sizeof(file_t));

  tsk->type = TSK_RECV_FILE;
  tsk->file = file;
  tsk->pos = 0;
  tsk->nxt = 0;
  strcpy(file->filename, filename);
  snprintf(file->localfile, 100, "%s_%s", cli->user->username, filename);
  file->size = filesize;
  file->fd = open(file->localfile, O_RDWR | O_TRUNC | O_CREAT, 0644);
  if (file->fd == -1)
    ERROR();

  // Recv task should be processed in higher priority
  server_client_add_task_first(cli, tsk);
}

int server_add_client(user_t *user_list, int user_count, client_t *cli,
                      char *username) {
  cli->prev = cli->next = 0;
  for (int i = 0; i < user_count; i++) {
    if (strcmp(user_list[i].username, username) == 0) {
      printf("Server add new client to user %s\n", username);
      if (user_list[i].client_count) {
        cli->next = user_list[i].client_list;
        user_list[i].client_list->prev = cli;
      }
      user_list[i].client_count++;
      user_list[i].client_list = cli;
      cli->user = &user_list[i];
      // Add task list
      for (int i = 0; i < cli->user->file_count; i++) {
        task_t *t = (task_t *)malloc(sizeof(task_t));
        bzero(t, sizeof(task_t));
        t->type = TSK_SEND_FILE;
        t->pos = -1;
        t->file = cli->user->files[i];
        server_client_add_task(cli, t);
      }
      return user_count;
    }
  }
  // Add new user
  printf("Server add new user %s\n", username);
  bzero(&user_list[user_count], sizeof(user_t));
  strcpy(user_list[user_count].username, username);

  user_list[user_count].client_count++;
  user_list[user_count].client_list = cli;
  cli->user = &user_list[user_count];
  return ++user_count;
}

void server_delete_client(client_t *cli) {
  while (cli->tasks)
    server_client_remove_task(cli);
  free(cli->send_buffer);
  free(cli->recv_buffer);
  free(cli);
}

void server_finish_recv_task(client_t *cur_cli) {
  printf("Finish receive file %s\n", cur_cli->tasks->file->filename);
  file_t *f = cur_cli->tasks->file;
  server_client_remove_task(cur_cli);

  f->fd = open(f->localfile, O_RDONLY);
  if (f->fd == -1)
    ERROR();

  // Add file to user account
  cur_cli->user->files[cur_cli->user->file_count++] = f;
  // Push file to all other device
  for (client_t *cli = cur_cli->user->client_list; cli; cli = cli->next) {
    if (cli != cur_cli) {
      task_t *task = (task_t *)malloc(sizeof(task_t));
      bzero(task, sizeof(task_t));
      task->type = TSK_SEND_FILE;
      task->file = f;
      task->pos = -1; // Set to -1 to prepare header package
      server_client_add_task(cli, task);
    }
  }
}

void server_recv_data(client_t *cli, int size, char *data) {
  if (write(cli->tasks->file->fd, data, size) == -1) {
    printf("FD: %d\n", cli->tasks->file->fd);
    ERROR();
  }

  cli->tasks->pos += size;

  // Finish file receiving
  if (cli->tasks->pos == cli->tasks->file->size)
    server_finish_recv_task(cli);
}

int server_send_file_init(char *buf, char *name, uint32_t filesize) {
  size_t size = 0;
  uint16_t len = strlen(name) + 1; // include the \0 + filesize
  char *p = buf;
  *p++ = PKG_TYPE_FILE, size++;                 // Package type
  *p++ = 0, size++;                             // Package subtype (NULL)
  *(uint16_t *)p = len + 4, p += 2, size += 2;  // Package size
  *(uint32_t *)p = filesize, p += 4, size += 4; // file size
  memcpy(p, name, len), size += len;            // file name
  return size;
}

int main(int argc, char const *argv[]) {
  if (argc != 2) {
    printf("USAGE: %s <port>", argv[0]);
    return 1;
  }
  printf("Server Started\n");

  signal(SIGPIPE, SIG_IGN);

  int sock = server_get_socket(atoi(argv[1]));
  int val;
  fd_set fds_read, fds_write;
  int maxfdp1;

  client_t *client_panding_list = NULL;
  int user_count = 0;
  user_t *user_list = (user_t *)malloc(sizeof(user_t) * SERVER_USER_LIMIT);

  while (1) {
    FD_ZERO(&fds_read);
    FD_ZERO(&fds_write);
    FD_SET(sock, &fds_read);
    maxfdp1 = sock + 1;
    // Checking pending list, waiting for its init data package
    for (client_t *cli = client_panding_list; cli; cli = cli->next) {
      FD_SET(cli->sock, &fds_read);
      if (cli->sock >= maxfdp1)
        maxfdp1 = cli->sock + 1;
    }

    for (int i = 0; i < user_count; i++) {
      for (client_t *cli = user_list[i].client_list; cli; cli = cli->next) {

        if (cli->tasks && cli->tasks->type == TSK_SEND_FILE) {
          FD_SET(cli->sock, &fds_write);
        } else {
          FD_SET(cli->sock, &fds_read);
        }
        if (cli->sock >= maxfdp1)
          maxfdp1 = cli->sock + 1;
      }
    }

    if (select(maxfdp1, &fds_read, &fds_write, NULL, NULL) < 0) {
      if (errno != EWOULDBLOCK)
        ERROR();
      usleep(100);
      continue;
    }

    // Check the listening socket
    if (FD_ISSET(sock, &fds_read)) {
      // Ready to read listening buffer
      client_t *cli = server_new_client(sock);
      if (cli == NULL)
        continue;

      // Add new client to pending list
      cli->prev = NULL;
      cli->next = client_panding_list;
      if (client_panding_list)
        client_panding_list->prev = cli;
      client_panding_list = cli;

      printf("New client connected, status: PENDING\n");
    }

    // Check the pending list
    for (client_t *cli = client_panding_list; cli; cli = cli->next) {
      if (FD_ISSET(cli->sock, &fds_read)) {
        printf("receive init data\n");
        val = read(cli->sock, cli->recv_buffer + cli->recv_buffer_pos,
                   BUFFER_SIZE - cli->recv_buffer_pos);
        if (val == -1 && errno == EWOULDBLOCK)
          continue;
        if (val == -1)
          ERROR();

        if (val == 0) {
          // client quit before sending first package
          printf("client quit before setup\n");
          close(cli->sock);
          if (cli->next) {
            cli->next->prev = cli->prev;
          }
          if (cli->prev) {
            cli->prev->next = cli->next;
          } else {
            client_panding_list = cli->next;
          }
          server_delete_client(cli);
          continue;
        }

        cli->recv_buffer_pos += val;
        char *buf = cli->recv_buffer;
        int *buf_pos = &cli->recv_buffer_pos;

        if (PKG_SIZE_GET(buf) + 4 <= *buf_pos) {
          // received an complete package
          int pkg_size = PKG_SIZE_GET(buf) + 4;

          switch (buf[0]) {
          case PKG_TYPE_INIT:
            if (cli->next) {
              cli->next->prev = cli->prev;
            }
            if (cli->prev) {
              cli->prev->next = cli->next;
            } else {
              client_panding_list = cli->next;
            }
            user_count = server_add_client(user_list, user_count, cli, buf + 4);
            break;
          default:
            printf("ERROR: Received package other than INIT for new clinet\n");
          }

          if (pkg_size < *buf_pos) {
            memcpy(buf, buf + pkg_size, *buf_pos - pkg_size);
          }
          *buf_pos -= pkg_size;
        } else if (*buf_pos == BUFFER_SIZE) {
          printf("Error, receive buffer overflow");
          errno = ENOBUFS;
          ERROR();
        }
      }
    }

    // Check the connected client
    for (int i = 0; i < user_count; i++) {
      for (client_t *cli = user_list[i].client_list; cli; cli = cli->next) {
        if (FD_ISSET(cli->sock, &fds_read)) {
          val = read(cli->sock, cli->recv_buffer + cli->recv_buffer_pos,
                     BUFFER_SIZE - cli->recv_buffer_pos);
          if (val == -1 && errno == EWOULDBLOCK)
            continue;

          if (val == 0 || (val == -1 && errno == ECONNRESET)) {
            // Client connection lost
            printf("Client of user %s leaves.\n", cli->user->username);

            close(cli->sock);

            if (cli->prev)
              cli->prev->next = cli->next;
            else
              cli->user->client_list = cli->next;
            if (cli->next)
              cli->next->prev = cli->prev;
            cli->user->client_count--;

            server_delete_client(cli);
            continue;
          }

          if (val == -1)
            ERROR();

          // Receive data
          cli->recv_buffer_pos += val;
          char *buf = cli->recv_buffer;
          int *buf_pos = &cli->recv_buffer_pos;
          if (PKG_SIZE_GET(buf) + 4 <= *buf_pos) {
            // received an complete package
            int pkg_size = PKG_SIZE_GET(buf) + 4;

            switch (buf[0]) {
            case PKG_TYPE_FILE:
              server_add_recv_task(cli, *(uint32_t *)(buf + 4), buf + 8);
              break;
            case PKG_TYPE_DATA:
              server_recv_data(cli, PKG_SIZE_GET(buf), buf + 4);
              break;
            default:
              printf("ERROR: unknown data \n");
            }

            if (pkg_size < *buf_pos) {
              memcpy(buf, buf + pkg_size, *buf_pos - pkg_size);
            }
            *buf_pos -= pkg_size;
          } else if (*buf_pos == BUFFER_SIZE) {
            printf("Error, receive buffer overflow");
            errno = ENOBUFS;
            ERROR();
          }
        }

        if (FD_ISSET(cli->sock, &fds_write)) {
          if (cli->tasks && cli->tasks->type == TSK_SEND_FILE) {
            if (cli->send_buffer_pos) {
              val = write(cli->sock, cli->send_buffer, cli->send_buffer_pos);
              if (val == -1 && errno == EWOULDBLOCK)
                continue;
              if (val == -1)
                ERROR();
              if (val < cli->send_buffer_pos) {
                memcpy(cli->send_buffer, cli->send_buffer + val,
                       cli->send_buffer_pos - val);
              }
              cli->send_buffer_pos -= val;
              if (cli->send_buffer_pos)
                continue; // fail to next stage till buffer empty
            }
            if (cli->tasks->pos == -1) {
              // Prep header
              val = server_send_file_init(cli->send_buffer,
                                          cli->tasks->file->filename,
                                          cli->tasks->file->size);
              cli->send_buffer_pos += val;
              cli->tasks->pos = 0;
            } else {
              task_t *t = cli->tasks;
              file_t *f = cli->tasks->file;

              if (t->pos == f->size) {
                // finish transfer
                server_client_remove_task(cli);
                continue;
              }

              cli->send_buffer[0] = PKG_TYPE_DATA;
              val = pread(f->fd, cli->send_buffer + 4, BUFFER_SIZE - 4, t->pos);
              if (val == -1)
                ERROR();
              *(uint16_t *)(cli->send_buffer + 2) = val;
              cli->send_buffer_pos = val + 4;
              t->pos += val;
            }
            val = write(cli->sock, cli->send_buffer, cli->send_buffer_pos);
            if (val == -1 &&
                (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR))
              continue;
            if (val == -1 && (errno == ECONNRESET || errno == EPIPE)) {
              // The user drops the connection firaly plitely
              printf("Client of user %s leaves.\n", cli->user->username);

              close(cli->sock);

              if (cli->prev)
                cli->prev->next = cli->next;
              else
                cli->user->client_list = cli->next;
              if (cli->next)
                cli->next->prev = cli->prev;
              cli->user->client_count--;

              server_delete_client(cli);
              continue;
            }
            if (val == -1)
              ERROR();
            if (val < cli->send_buffer_pos) {
              memcpy(cli->send_buffer, cli->send_buffer + val,
                     cli->send_buffer_pos - val);
            }
            cli->send_buffer_pos -= val;
          }
        }
      }
    }
  }

  return 0;
}
