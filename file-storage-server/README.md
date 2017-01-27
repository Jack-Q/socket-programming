File Storage Service
====================


In this homework, you have to practice Nonblocking IO in a network program.
You should write a server and a client, the server has to be a single process & single thread non-blocking server, and all connections are TCP in this homework.

Description
-----------

The concept of this program is like dropbox

* an user can save his files on the server.
* the clients of the user are running on different hosts.
* when any of client of the user upload a file, the server have to transmit it to all the other clients of the user.
* when a new client of the user connects to the server, the server should transmit all the files, which are uploaded by the other clients of the user, to the new client immediately.
* if one of the client is sleeping, server has to send the data to the other clients of the user in a non-blocking way.
Seventh, the uploading data only need to send to the clients which are belong to the user.

Note: In this server program, the following lines of code are used to achieve the non-blocking function.
```c
int flag = fcntl(sock, F_GETFL, 0);
fcnctl(sock, F_SETTFL, flag | O_NONBLOCK);
```

Input Format
------------

1. `./client <ip> <port> <username>``
  Please make sure that the you should excute the client program in this format.

2. `./server <port>`

  Please make sure that the you should excute the server program in this format.

3. `/put <filename>`
  This command, which is used on client side by users, is to upload your files to the server side.

  Users can transmit any files they want, but it has to be put in the same directory of client.

  The filename should send to the other clients.

4. `/sleep <seconds>`
  This command is to let client to sleep.

5. `/exit`
  This command is to disconnect with server and terminate the program.


Output Format
-------------

1. Welcome message. 

```
Welcome to the dropbox-like server! : <username>
```

2. Uploading progess bar.

```
    Downloading file : <filename>
    Progress : [############                      ]
    Download <filename> complete!
```

3. Downloading progess bar.

```
Uploading file : <filename>
Progress : [######################]
Upload <filename> complete!
```

4. Sleeping count down.

```
/sleep 20
Client starts to sleep
Sleep 1
.
.
Sleep 19
Sleep 20
Client wakes up  
```
