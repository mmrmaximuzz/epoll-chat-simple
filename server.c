#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>


#define PORT 12345
#define MAXEVENTS 64
#define BUFFERSIZE 1024


void error_exit(const char *msg) {
    error(EXIT_FAILURE, errno, msg);
}


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


void configure_server_socket(int serverSocket) {
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
}


void register_in_epoll(int sock, int epollId) {
    struct epoll_event event;
    event.data.fd = sock;
    event.events = EPOLLIN;
    if (epoll_ctl(epollId, EPOLL_CTL_ADD, sock, &event) == -1)
        error_exit("cannot register in epoll");
}


void register_new_client(int serverSocket, int epollId) {
    int clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == -1)
        error_exit("cannot accept new client");

    if (set_nonblock(clientSocket))
        error_exit("cannot unblock");

    register_in_epoll(clientSocket, epollId);
}


void serve_client(int clientSocket) {
    char buffer[BUFFERSIZE] = {};
    int recvSize = recv(clientSocket, buffer, BUFFERSIZE, MSG_NOSIGNAL);
    if (recvSize <= 0 && errno != EAGAIN) {
        shutdown(clientSocket, SHUT_RDWR);
        close(clientSocket);
    } else if (recvSize > 0) {
        send(clientSocket, buffer, recvSize, MSG_NOSIGNAL);
    }
}


int main() {
    // open and configure the master socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == -1)
        error_exit("error in socket()");
    configure_server_socket(serverSocket);

    // create epoll id
    int epollId = epoll_create1(0);
    if (epollId == -1)
        error_exit("error in epoll_create1()");

    // register server in epoll struct
    register_in_epoll(serverSocket, epollId);

    // go to infinite cycle
    while (1) {
        struct epoll_event currentEvents[MAXEVENTS];
        int nEvents = epoll_wait(epollId, currentEvents, MAXEVENTS, -1);
        for (int i = 0; i < nEvents; ++i) {
            int sock = currentEvents[i].data.fd;
            if (sock == serverSocket) {
                // server socket gets request to join
                register_new_client(serverSocket, epollId);
            } else {
                // client socket is ready to something
                serve_client(sock);
            }
        }
    }

    // stop the server
    if (shutdown(serverSocket, SHUT_RDWR) == -1)
        error_exit("error in shutdown()");

    if (close(serverSocket) == -1)
        error_exit("error in close()");

    return 0;
}
