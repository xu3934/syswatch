#define _GNU_SOURCE
#include "syswatch.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_stat(pid_t pid, process_info *out)
{
    char path[64];
    char line[8192];
    char fields[8192];
    char *left, *right, *token, *save = NULL;
    int field = 3;
    long page_size = sysconf(_SC_PAGESIZE);
    FILE *fp;

    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    left = strchr(line, '(');
    right = strrchr(line, ')');
    if (left == NULL || right == NULL || right <= left)
        return -1;

    out->pid = pid;
    {
        size_t len = (size_t)(right - left - 1);
        if (len >= sizeof(out->comm))
            len = sizeof(out->comm) - 1;
        memcpy(out->comm, left + 1, len);
        out->comm[len] = '\0';
    }

    snprintf(fields, sizeof(fields), "%s", right + 2);
    for (token = strtok_r(fields, " ", &save); token != NULL;
         token = strtok_r(NULL, " ", &save), field++) {
        switch (field) {
        case 3: out->state = token[0]; break;
        case 4: out->ppid = (pid_t)strtol(token, NULL, 10); break;
        case 10: out->minor_faults = strtoull(token, NULL, 10); break;
        case 12: out->major_faults = strtoull(token, NULL, 10); break;
        case 14: out->user_ticks = strtoull(token, NULL, 10); break;
        case 15: out->system_ticks = strtoull(token, NULL, 10); break;
        case 23: out->vsize_bytes = strtoull(token, NULL, 10); break;
        case 24:
            out->rss_bytes = strtoll(token, NULL, 10) * (long long)page_size;
            break;
        default: break;
        }
        if (field >= 24)
            break;
    }
    return field >= 24 ? 0 : -1;
}

static void read_status_context_switches(pid_t pid, process_info *out)
{
    char path[64];
    char line[512];
    FILE *fp;

    snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (sscanf(line, "voluntary_ctxt_switches: %llu", &out->voluntary_ctxt) == 1)
            continue;
        (void)sscanf(line, "nonvoluntary_ctxt_switches: %llu", &out->nonvoluntary_ctxt);
    }
    fclose(fp);
}

static long count_open_fds(pid_t pid)
{
    char path[64];
    DIR *dir;
    struct dirent *entry;
    long count = 0;

    snprintf(path, sizeof(path), "/proc/%ld/fd", (long)pid);
    dir = opendir(path);
    if (dir == NULL)
        return -1;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            count++;
    }
    closedir(dir);
    return count;
}

int read_process(pid_t pid, process_info *out)
{
    memset(out, 0, sizeof(*out));
    out->open_fds = -1;
    if (read_stat(pid, out) != 0)
        return -1;
    read_status_context_switches(pid, out);
    out->open_fds = count_open_fds(pid);
    return 0;
}

int compare_process_pid(const void *a, const void *b)
{
    const process_info *pa = a;
    const process_info *pb = b;
    return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

int collect_processes(process_info **items, size_t *count)
{
    DIR *proc = opendir("/proc");
    struct dirent *entry;
    process_info *list = NULL;
    size_t used = 0, capacity = 0;

    *items = NULL;
    *count = 0;
    if (proc == NULL)
        return -1;

    while ((entry = readdir(proc)) != NULL) {
        process_info info;
        process_info *grown;
        if (!is_numeric_name(entry->d_name))
            continue;
        if (read_process((pid_t)strtol(entry->d_name, NULL, 10), &info) != 0)
            continue; /* Processes can disappear while /proc is scanned. */
        if (used == capacity) {
            capacity = capacity == 0 ? 128 : capacity * 2;
            grown = realloc(list, capacity * sizeof(*list));
            if (grown == NULL) {
                free(list);
                closedir(proc);
                return -1;
            }
            list = grown;
        }
        list[used++] = info;
    }
    closedir(proc);
    qsort(list, used, sizeof(*list), compare_process_pid);
    *items = list;
    *count = used;
    return 0;
}

void print_process_table(const process_info *items, size_t count, size_t limit)
{
    size_t i;
    long ticks = sysconf(_SC_CLK_TCK);
    if (limit == 0 || limit > count)
        limit = count;
    printf("%-7s %-7s %-1s %10s %10s %8s %10s %9s  %s\n",
           "PID", "PPID", "S", "RSS", "VSZ", "FDs", "FAULTS", "CPU(s)", "COMMAND");
    for (i = 0; i < limit; i++) {
        char rss[32], vsz[32], fds[24];
        unsigned long long faults = items[i].minor_faults + items[i].major_faults;
        double cpu = ticks > 0 ? (double)(items[i].user_ticks + items[i].system_ticks) / ticks : 0.0;
        if (items[i].open_fds < 0)
            snprintf(fds, sizeof(fds), "?");
        else
            snprintf(fds, sizeof(fds), "%ld", items[i].open_fds);
        printf("%-7ld %-7ld %c %10s %10s %8s %10llu %9.2f  %s\n",
               (long)items[i].pid, (long)items[i].ppid, items[i].state,
               format_bytes(items[i].rss_bytes > 0 ? (unsigned long long)items[i].rss_bytes : 0, rss, sizeof(rss)),
               format_bytes(items[i].vsize_bytes, vsz, sizeof(vsz)), fds, faults, cpu, items[i].comm);
    }
    if (limit < count)
        printf("... %zu more processes (use --all to show every process)\n", count - limit);
}

int read_maps_summary(pid_t pid, maps_summary *out)
{
    char path[64];
    char *line = NULL;
    size_t line_cap = 0;
    FILE *fp;

    memset(out, 0, sizeof(*out));
    snprintf(path, sizeof(path), "/proc/%ld/maps", (long)pid);
    fp = fopen(path, "r");
    if (fp == NULL)
        return -1;

    while (getline(&line, &line_cap, fp) >= 0) {
        unsigned long long start, end;
        char perms[5] = {0};
        char mapped_path[SW_PATH_LEN] = {0};
        unsigned long long bytes;
        int fields = sscanf(line, "%llx-%llx %4s %*s %*s %*s %4095[^\n]",
                            &start, &end, perms, mapped_path);
        char *name = mapped_path;
        if (fields < 3 || end < start)
            continue;
        while (*name == ' ')
            name++;
        bytes = end - start;
        out->mappings++;
        out->total_bytes += bytes;
        if (strchr(perms, 'x') != NULL)
            out->executable_bytes += bytes;
        if (*name == '\0' || *name == '[')
            out->anonymous_bytes += bytes;
        else
            out->file_bytes += bytes;
        if (strcmp(name, "[heap]") == 0)
            out->heap_bytes += bytes;
        if (strncmp(name, "[stack", 6) == 0)
            out->stack_bytes += bytes;
    }
    free(line);
    fclose(fp);
    return 0;
}

void print_maps_summary(pid_t pid)
{
    maps_summary summary;
    char total[32], anon[32], file[32], heap[32], stack[32], exec[32];
    if (read_maps_summary(pid, &summary) != 0) {
        fprintf(stderr, "syswatch: cannot read /proc/%ld/maps: %s\n", (long)pid, strerror(errno));
        return;
    }
    printf("\nMEMORY MAPS FOR PID %ld\n", (long)pid);
    printf("Mappings:   %zu\n", summary.mappings);
    printf("Mapped:     %s\n", format_bytes(summary.total_bytes, total, sizeof(total)));
    printf("Anonymous:  %s\n", format_bytes(summary.anonymous_bytes, anon, sizeof(anon)));
    printf("File-backed:%s%s\n", summary.file_bytes < 1024 ? " " : "  ",
           format_bytes(summary.file_bytes, file, sizeof(file)));
    printf("Heap:       %s\n", format_bytes(summary.heap_bytes, heap, sizeof(heap)));
    printf("Stack(s):   %s\n", format_bytes(summary.stack_bytes, stack, sizeof(stack)));
    printf("Executable: %s\n", format_bytes(summary.executable_bytes, exec, sizeof(exec)));
}

void print_process_detail(pid_t pid)
{
    process_info p;
    char rss[32], vsz[32];
    long ticks = sysconf(_SC_CLK_TCK);
    if (read_process(pid, &p) != 0) {
        fprintf(stderr, "syswatch: cannot inspect PID %ld: %s\n", (long)pid, strerror(errno));
        return;
    }
    printf("PROCESS %ld (%s)\n", (long)p.pid, p.comm);
    printf("Parent PID:       %ld\n", (long)p.ppid);
    printf("State:            %c%s\n", p.state, p.state == 'Z' ? " (zombie)" : "");
    printf("RSS / VSZ:        %s / %s\n",
           format_bytes(p.rss_bytes > 0 ? (unsigned long long)p.rss_bytes : 0, rss, sizeof(rss)),
           format_bytes(p.vsize_bytes, vsz, sizeof(vsz)));
    printf("Minor/major fault:%llu / %llu\n", p.minor_faults, p.major_faults);
    printf("Context switches: %llu voluntary, %llu involuntary\n",
           p.voluntary_ctxt, p.nonvoluntary_ctxt);
    if (p.open_fds < 0)
        printf("Open FDs:         unavailable (permission denied)\n");
    else
        printf("Open FDs:         %ld\n", p.open_fds);
    printf("CPU time:         %.2f s user, %.2f s system\n",
           ticks > 0 ? (double)p.user_ticks / ticks : 0.0,
           ticks > 0 ? (double)p.system_ticks / ticks : 0.0);
}

static int pid_is_present(const process_info *items, size_t count, pid_t pid)
{
    size_t i;
    for (i = 0; i < count; i++)
        if (items[i].pid == pid)
            return 1;
    return 0;
}

static void print_tree_node(const process_info *items, size_t count, pid_t pid,
                            unsigned depth, unsigned char *seen)
{
    size_t i;
    if (depth > 64)
        return;
    for (i = 0; i < count; i++) {
        if (items[i].pid == pid && !seen[i]) {
            unsigned d;
            seen[i] = 1;
            for (d = 0; d < depth; d++)
                printf("  ");
            printf("|- %s(%ld) [%c]%s\n", items[i].comm, (long)items[i].pid,
                   items[i].state, items[i].state == 'Z' ? " ZOMBIE" : "");
            break;
        }
    }
    for (i = 0; i < count; i++)
        if (items[i].ppid == pid && items[i].pid != pid)
            print_tree_node(items, count, items[i].pid, depth + 1, seen);
}

void print_process_tree(const process_info *items, size_t count)
{
    unsigned char *seen = calloc(count, 1);
    size_t i;
    if (seen == NULL)
        return;
    printf("PROCESS TREE\n");
    for (i = 0; i < count; i++) {
        if (items[i].ppid == 0 || !pid_is_present(items, count, items[i].ppid))
            print_tree_node(items, count, items[i].pid, 0, seen);
    }
    /* Handle unusual cycles or namespace roots that were not reached. */
    for (i = 0; i < count; i++)
        if (!seen[i])
            print_tree_node(items, count, items[i].pid, 0, seen);
    free(seen);
}

void print_system_summary(void)
{
    FILE *fp;
    double one = 0.0, five = 0.0, fifteen = 0.0;
    unsigned long long total_kb = 0, available_kb = 0;
    char line[256], total[32], used[32];

    fp = fopen("/proc/loadavg", "r");
    if (fp != NULL) {
        if (fscanf(fp, "%lf %lf %lf", &one, &five, &fifteen) != 3)
            one = five = fifteen = 0.0;
        fclose(fp);
    }
    fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            (void)sscanf(line, "MemTotal: %llu kB", &total_kb);
            (void)sscanf(line, "MemAvailable: %llu kB", &available_kb);
        }
        fclose(fp);
    }
    printf("SYSTEM\n");
    printf("Load average: %.2f %.2f %.2f\n", one, five, fifteen);
    printf("Memory:       %s used / %s total\n",
           format_bytes((total_kb - available_kb) * 1024ULL, used, sizeof(used)),
           format_bytes(total_kb * 1024ULL, total, sizeof(total)));
}
