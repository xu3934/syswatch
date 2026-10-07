#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "203.0.113.1"; /* TEST-NET-3 */
    unsigned long port = argc > 2 ? strtoul(argv[2], NULL, 10) : 65000;
    int timeout_ms = argc > 3 ? atoi(argv[3]) : 3000;
    int sock, flags, result, error = 0;
    socklen_t error_size = sizeof(error);
    struct sockaddr_in address;
    struct pollfd watched;
    if (port == 0 || port > 65535 || timeout_ms <= 0) {
        fprintf(stderr, "usage: %s [IPv4] [port] [timeout-ms]\n", argv[0]);
        return 2;
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }
    flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) { perror("fcntl"); close(sock); return 1; }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) { fprintf(stderr, "invalid IPv4 address\n"); close(sock); return 2; }
    printf("attempting nonblocking connect to %s:%lu (timeout %d ms)\n", host, port, timeout_ms);
    result = connect(sock, (struct sockaddr *)&address, sizeof(address));
    if (result == 0) { puts("unexpectedly connected"); close(sock); return 0; }
    if (errno != EINPROGRESS) { printf("connect failed immediately: %s\n", strerror(errno)); close(sock); return 0; }
    watched.fd = sock;
    watched.events = POLLOUT;
    watched.revents = 0;
    result = poll(&watched, 1, timeout_ms);
    if (result == 0)
        puts("diagnosis: connection timed out");
    else if (result < 0)
        perror("poll");
    else if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &error_size) == 0 && error != 0)
        printf("diagnosis: connect failed: %s\n", strerror(error));
    else
        puts("connection became writable");
    close(sock);
    return result < 0 ? 1 : 0;
}
