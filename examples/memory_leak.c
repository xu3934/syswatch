#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv)
{
    size_t megabytes = argc > 1 ? (size_t)strtoul(argv[1], NULL, 10) : 4;
    unsigned interval = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 1;
    size_t total = 0;
    struct timespec pause_time = {(time_t)interval, 0};
    if (megabytes == 0 || interval == 0) {
        fprintf(stderr, "usage: %s [MiB-per-step] [seconds]\n", argv[0]);
        return 2;
    }
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("memory_leak PID=%ld; adding %zu MiB every %u second(s)\n", (long)getpid(), megabytes, interval);
    while (running) {
        size_t bytes = megabytes * 1024U * 1024U;
        unsigned char *block = malloc(bytes);
        if (block == NULL) {
            fprintf(stderr, "malloc failed after %zu MiB: %s\n", total, strerror(errno));
            break;
        }
        memset(block, 0xA5, bytes); /* Touch pages so RSS, not just VSZ, grows. */
        total += megabytes;
        printf("allocated and touched: %zu MiB\n", total);
        fflush(stdout);
        (void)nanosleep(&pause_time, NULL);
        /* Intentionally lose block: this program is a diagnostic fixture. */
    }
    return 0;
}
