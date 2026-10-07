#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "127.0.0.1";
    const char *port = argc > 2 ? argv[2] : "9090";
    const char *message = argc > 3 ? argv[3] : "hello from syswatch";
    struct addrinfo hints, *addresses, *it;
    int sock = -1, result;
    char reply[1024];
    ssize_t received;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    result = getaddrinfo(host, port, &hints, &addresses);
    if (result != 0) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(result)); return 1; }
    for (it = addresses; it != NULL; it = it->ai_next) {
        sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sock >= 0 && connect(sock, it->ai_addr, it->ai_addrlen) == 0)
            break;
        if (sock >= 0) close(sock);
        sock = -1;
    }
    freeaddrinfo(addresses);
    if (sock < 0) { perror("connect"); return 1; }
    if (send(sock, message, strlen(message), 0) < 0) { perror("send"); close(sock); return 1; }
    received = recv(sock, reply, sizeof(reply) - 1, 0);
    if (received < 0) { perror("recv"); close(sock); return 1; }
    reply[received] = '\0';
    printf("echo reply: %s\n", reply);
    close(sock);
    return 0;
}
