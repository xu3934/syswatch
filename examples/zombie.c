#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

int main(int argc, char **argv)
{
    unsigned lifetime = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 60;
    pid_t child;
    struct timespec one_second = {1, 0};
    unsigned elapsed = 0;
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0)
        _exit(42);
    printf("parent PID=%ld left child PID=%ld as a zombie for %u seconds\n",
           (long)getpid(), (long)child, lifetime);
    while (running && elapsed++ < lifetime)
        (void)nanosleep(&one_second, NULL);
    if (waitpid(child, NULL, 0) < 0 && errno != ECHILD)
        perror("waitpid");
    puts("child reaped; zombie cleared");
    return 0;
}
