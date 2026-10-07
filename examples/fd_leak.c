#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
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
    unsigned target = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 2048;
    unsigned delay_ms = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 10;
    unsigned count = 0;
    struct timespec delay = {(time_t)(delay_ms / 1000), (long)(delay_ms % 1000) * 1000000L};
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("fd_leak PID=%ld; opening up to %u descriptors\n", (long)getpid(), target);
    while (running && count < target) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "open stopped at %u FDs: %s\n", count, strerror(errno));
            break;
        }
        count++;
        if (count % 100 == 0) {
            printf("opened %u descriptors\n", count);
            fflush(stdout);
        }
        (void)nanosleep(&delay, NULL);
        /* fd intentionally remains open until process exit. */
    }
    printf("holding %u descriptors; press Ctrl-C to exit\n", count);
    while (running)
        pause();
    return 0;
}
