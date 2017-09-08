#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>


int set_nonblock(int fd) {
    int flags;
    #if defined(O_NONBLOCK)
        if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
            flags = 0;
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    #else
        flags = 1;
        return ioctl(fd, FIOBIO, &flags);
    #endif
}


void error_exit(const char *msg) {
    error(EXIT_FAILURE, errno, msg);
}


#define PORT 12345
#define MAXEVENTS 64
#define BUFFERSIZE 1024


int main() {
    // open the master socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == -1)
        error_exit("error in socket()");

    // bind the socket to the address
    struct sockaddr_in socketAddress;
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(PORT);
    socketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(serverSocket, (struct sockaddr *)(&socketAddress), sizeof(socketAddress)) == -1)
        error_exit("error in bind()");

    // unblock the master socket
    if (set_nonblock(serverSocket) == -1)
        error_exit("cannot unblock the master socket");

    // listen sockets
    if (listen(serverSocket, SOMAXCONN) == -1)
        error_exit("cannot listen");

    // create epoll struct
    int ePollId = epoll_create1(0);
    if (ePollId == -1)
        error_exit("error in epoll_create1()");

    struct epoll_event serverEvent;
    serverEvent.data.fd = serverSocket;
    serverEvent.events = EPOLLIN;
    if (epoll_ctl(ePollId, EPOLL_CTL_ADD, serverSocket, &serverEvent) == -1)
        error_exit("error in epoll_ctl()");

    while (1) {
        struct epoll_event currentEvents[MAXEVENTS];
        int nChanges = epoll_wait(ePollId, currentEvents, MAXEVENTS, -1);
        for (int i = 0; i < nChanges; ++i) {
            if (currentEvents[i].data.fd == serverSocket) {
                // server socket get request to join
                int clientSocket = accept(serverSocket, 0, 0);
                set_nonblock(clientSocket);
                struct epoll_event clientEvent;
                clientEvent.data.fd = clientSocket;
                clientEvent.events = EPOLLIN;
                epoll_ctl(ePollId, EPOLL_CTL_ADD, clientSocket, &clientEvent);
                printf("registered: %d\n", clientSocket);
            } else {
                // client socket sends data
                char buffer[BUFFERSIZE] = {};
                int clientSocket = currentEvents[i].data.fd;
                int recvSize = recv(clientSocket, buffer, BUFFERSIZE, MSG_NOSIGNAL);
                if (recvSize <= 0 && errno != EAGAIN) {
                    shutdown(clientSocket, SHUT_RDWR);
                    close(clientSocket);
                    printf("closed connection: %d\n", clientSocket);
                } else if (recvSize > 0) {
                    printf("get data from: %d\n", clientSocket);
                    send(clientSocket, buffer, recvSize, MSG_NOSIGNAL);
                }
            }
        }
    }

    // stop socket operations
    if (shutdown(serverSocket, SHUT_RDWR) == -1)
        error_exit("error in shutdown()");

    if (close(serverSocket) == -1)
        error_exit("error in close()");

    return 0;
}
