#define _POSIX_C_SOURCE 200809L
#include "syswatch.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    int show_all;
    int show_tree;
    int show_net;
    int have_pid;
    int have_maps;
    pid_t pid;
    pid_t maps_pid;
    unsigned interval;
} options;

static volatile sig_atomic_t stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
        "Usage: %s [OPTIONS]\n"
        "Inspect Linux processes, virtual memory, file descriptors, and TCP sockets.\n\n"
        "  -a, --all             show every visible process (default: first 20)\n"
        "  -t, --tree            show the process parent/child tree\n"
        "  -n, --net             inspect IPv4/IPv6 TCP sockets and resolve owners\n"
        "  -p, --pid PID         show detailed metrics for one process\n"
        "  -m, --maps PID        summarize /proc/PID/maps\n"
        "  -i, --interval SEC    refresh continuously every SEC seconds\n"
        "  -h, --help            show this help\n\n"
        "With no options, syswatch prints a system summary, process table, and warnings.\n",
        program);
}

static int parse_pid(const char *text, pid_t *pid)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || *text == '\0' || *end != '\0' || value <= 0 || value > INT_MAX)
        return -1;
    *pid = (pid_t)value;
    return 0;
}

static int parse_options(int argc, char **argv, options *opts)
{
    static const struct option long_options[] = {
        {"all", no_argument, NULL, 'a'},
        {"tree", no_argument, NULL, 't'},
        {"net", no_argument, NULL, 'n'},
        {"pid", required_argument, NULL, 'p'},
        {"maps", required_argument, NULL, 'm'},
        {"interval", required_argument, NULL, 'i'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;

    memset(opts, 0, sizeof(*opts));
    while ((option = getopt_long(argc, argv, "atnp:m:i:h", long_options, NULL)) != -1) {
        switch (option) {
        case 'a': opts->show_all = 1; break;
        case 't': opts->show_tree = 1; break;
        case 'n': opts->show_net = 1; break;
        case 'p':
            if (parse_pid(optarg, &opts->pid) != 0) {
                fprintf(stderr, "syswatch: invalid PID: %s\n", optarg);
                return -1;
            }
            opts->have_pid = 1;
            break;
        case 'm':
            if (parse_pid(optarg, &opts->maps_pid) != 0) {
                fprintf(stderr, "syswatch: invalid PID: %s\n", optarg);
                return -1;
            }
            opts->have_maps = 1;
            break;
        case 'i': {
            char *end;
            unsigned long value = strtoul(optarg, &end, 10);
            if (*optarg == '\0' || *end != '\0' || value == 0 || value > 86400) {
                fprintf(stderr, "syswatch: interval must be between 1 and 86400 seconds\n");
                return -1;
            }
            opts->interval = (unsigned)value;
            break;
        }
        case 'h': usage(stdout, argv[0]); exit(EXIT_SUCCESS);
        default: return -1;
        }
    }
    if (optind != argc) {
        fprintf(stderr, "syswatch: unexpected argument: %s\n", argv[optind]);
        return -1;
    }
    return 0;
}

static void print_warnings(const process_info *items, size_t count)
{
    size_t i, warnings = 0;
    printf("\nWARNINGS\n");
    for (i = 0; i < count; i++) {
        if (items[i].state == 'Z') {
            printf("[!] PID %ld (%s) is a zombie; inspect parent PID %ld.\n",
                   (long)items[i].pid, items[i].comm, (long)items[i].ppid);
            warnings++;
        }
        if (items[i].open_fds >= 1024) {
            printf("[!] PID %ld (%s) has %ld open file descriptors.\n",
                   (long)items[i].pid, items[i].comm, items[i].open_fds);
            warnings++;
        }
    }
    if (warnings == 0)
        printf("No obvious zombie or high-FD conditions detected.\n");
}

static int render(const options *opts)
{
    process_info *items = NULL;
    size_t count = 0;
    int explicit_view = opts->show_tree || opts->show_net || opts->have_pid || opts->have_maps;
    int need_processes = !explicit_view || opts->show_all || opts->show_tree;

    if (need_processes && collect_processes(&items, &count) != 0) {
        fprintf(stderr, "syswatch: cannot scan /proc: %s\n", strerror(errno));
        return -1;
    }
    if (!explicit_view || opts->show_all) {
        print_system_summary();
        printf("\nPROCESSES (%zu visible)\n", count);
        print_process_table(items, count, opts->show_all ? 0 : 20);
        print_warnings(items, count);
    }
    if (opts->show_tree) {
        if (!opts->show_all && !opts->have_pid && !opts->have_maps && !opts->show_net)
            printf("\n");
        print_process_tree(items, count);
    }
    if (opts->have_pid) {
        if (opts->show_tree || opts->show_all)
            printf("\n");
        print_process_detail(opts->pid);
    }
    if (opts->have_maps)
        print_maps_summary(opts->maps_pid);
    if (opts->show_net) {
        if (opts->show_tree || opts->show_all || opts->have_pid || opts->have_maps)
            printf("\n");
        print_tcp_sockets();
    }
    free(items);
    return 0;
}

int main(int argc, char **argv)
{
    options opts;
    if (parse_options(argc, argv, &opts) != 0) {
        usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    do {
        struct timespec remaining;
        if (opts.interval != 0)
            printf("\033[2J\033[H");
        if (render(&opts) != 0)
            return EXIT_FAILURE;
        if (opts.interval == 0)
            break;
        fflush(stdout);
        remaining.tv_sec = opts.interval;
        remaining.tv_nsec = 0;
        while (!stop_requested && nanosleep(&remaining, &remaining) != 0 && errno == EINTR)
            ;
    } while (!stop_requested);
    return EXIT_SUCCESS;
}
