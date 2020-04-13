#include <array>
#include <map>
#include <set>
#include <vector>
#include <errno.h>
#include <stdlib.h>

#include <fcntl.h>
#include <error.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>


static const int PORT = 12345;
static const int MAXEVENTS = 32;
static const int BUFFERSIZE = 1024;
static std::set<int> activeUsers;
static std::vector<std::string> messages;
static std::map<int, std::string> ipAddresses;


void error_exit(const char *msg)
{
    error(EXIT_FAILURE, errno, "%s\n", msg);
}


int set_nonblock(int fd)
{
#if defined(O_NONBLOCK)
    int flags;
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    int flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}


void configure_server_socket(int serverSocket)
{
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


void register_in_epoll(int sock, int epollId)
{
    struct epoll_event event;
    event.data.fd = sock;
    event.events = EPOLLIN;
    if (epoll_ctl(epollId, EPOLL_CTL_ADD, sock, &event) == -1)
        error_exit("cannot register in epoll");
}


void register_new_client(int serverSocket, int epollId)
{
    socklen_t clientAddrSize = sizeof(struct sockaddr_in);
    struct sockaddr_in socketAddress;
    int clientSocket = accept(serverSocket, (struct sockaddr *) &socketAddress, &clientAddrSize);
    if (clientSocket == -1)
        error_exit("cannot accept new client");

    if (set_nonblock(clientSocket))
        error_exit("cannot unblock");

    activeUsers.insert(clientSocket);
    char *ipAddress = inet_ntoa(socketAddress.sin_addr);
    ipAddresses.insert(std::make_pair(clientSocket, ipAddress));
    register_in_epoll(clientSocket, epollId);
}


void serve_client(int clientSocket)
{
    std::array<char, BUFFERSIZE> buffer = {};
    int recvSize = recv(clientSocket, buffer.data(), BUFFERSIZE, MSG_NOSIGNAL);
    if (recvSize <= 0 && errno != EAGAIN) {
        shutdown(clientSocket, SHUT_RDWR);
        close(clientSocket);
        activeUsers.erase(clientSocket);
    } else if (recvSize > 0) {
        std::string message = ipAddresses.find(clientSocket)->second + ": " + buffer.data();
        messages.push_back(message);
    }
}


void send_messages()
{
    std::string totalMessage = "";
    for (std::string &message : messages)
        totalMessage += message;
    size_t messageLength = totalMessage.size() + 1;

    for (int sock : activeUsers) {
        send(sock, totalMessage.c_str(), messageLength, MSG_NOSIGNAL);
    }
    messages.clear();
}


int main()
{
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
    while (true) {
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
        send_messages();
    }

    // stop the server
    if (shutdown(serverSocket, SHUT_RDWR) == -1)
        error_exit("error in shutdown()");

    if (close(serverSocket) == -1)
        error_exit("error in close()");

    return EXIT_SUCCESS;
}
