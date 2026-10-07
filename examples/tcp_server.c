#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv)
{
    unsigned long port = argc > 1 ? strtoul(argv[1], NULL, 10) : 9090;
    int server, reuse = 1;
    struct sockaddr_in address;
    if (port == 0 || port > 65535) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 2;
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0) { perror("bind"); close(server); return 1; }
    if (listen(server, 16) < 0) { perror("listen"); close(server); return 1; }
    printf("tcp_server PID=%ld listening on 0.0.0.0:%lu\n", (long)getpid(), port);
    while (running) {
        struct sockaddr_in peer;
        socklen_t peer_size = sizeof(peer);
        int client = accept(server, (struct sockaddr *)&peer, &peer_size);
        char buffer[1024];
        ssize_t received;
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }
        printf("accepted %s:%u\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
        received = recv(client, buffer, sizeof(buffer), 0);
        if (received > 0)
            (void)send(client, buffer, (size_t)received, 0);
        close(client);
    }
    close(server);
    return 0;
}
