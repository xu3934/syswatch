#ifndef SYSWATCH_H
#define SYSWATCH_H

#include <stddef.h>
#include <sys/types.h>

#define SW_COMM_LEN 256
#define SW_PATH_LEN 4096

typedef struct {
    pid_t pid;
    pid_t ppid;
    char comm[SW_COMM_LEN];
    char state;
    unsigned long long user_ticks;
    unsigned long long system_ticks;
    unsigned long long minor_faults;
    unsigned long long major_faults;
    unsigned long long vsize_bytes;
    long long rss_bytes;
    unsigned long long voluntary_ctxt;
    unsigned long long nonvoluntary_ctxt;
    long open_fds;
} process_info;

typedef struct {
    unsigned long long total_bytes;
    unsigned long long anonymous_bytes;
    unsigned long long file_bytes;
    unsigned long long heap_bytes;
    unsigned long long stack_bytes;
    unsigned long long executable_bytes;
    size_t mappings;
} maps_summary;

int read_process(pid_t pid, process_info *out);
int collect_processes(process_info **items, size_t *count);
int compare_process_pid(const void *a, const void *b);
void print_process_table(const process_info *items, size_t count, size_t limit);
void print_process_detail(pid_t pid);
void print_process_tree(const process_info *items, size_t count);
int read_maps_summary(pid_t pid, maps_summary *out);
void print_maps_summary(pid_t pid);
void print_system_summary(void);

void print_tcp_sockets(void);

const char *format_bytes(unsigned long long bytes, char *buf, size_t size);
int is_numeric_name(const char *name);

#endif
