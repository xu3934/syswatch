#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv)
{
    unsigned seconds = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 30;
    struct timespec start, now;
    volatile unsigned long long value = 0x12345678ULL;
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        perror("clock_gettime");
        return 1;
    }
    printf("cpu_hog PID=%ld; busy for %u seconds\n", (long)getpid(), seconds);
    do {
        unsigned i;
        for (i = 0; i < 1000000; i++)
            value = value * 6364136223846793005ULL + 1;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            break;
    } while (running && (unsigned)(now.tv_sec - start.tv_sec) < seconds);
    printf("done (checksum=%llu)\n", value);
    return 0;
}
