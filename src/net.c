#define _GNU_SOURCE
#include "syswatch.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    unsigned long long inode;
    pid_t pid;
    char comm[SW_COMM_LEN];
} socket_owner;

static int compare_owner_inode(const void *a, const void *b)
{
    const socket_owner *oa = a;
    const socket_owner *ob = b;
    if (oa->inode != ob->inode)
        return (oa->inode > ob->inode) - (oa->inode < ob->inode);
    return (oa->pid > ob->pid) - (oa->pid < ob->pid);
}

static void read_comm(pid_t pid, char *comm, size_t size)
{
    char path[64];
    FILE *fp;
    snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
    fp = fopen(path, "r");
    if (fp == NULL) {
        snprintf(comm, size, "?");
        return;
    }
    if (fgets(comm, (int)size, fp) == NULL)
        snprintf(comm, size, "?");
    comm[strcspn(comm, "\r\n")] = '\0';
    fclose(fp);
}

static int collect_socket_owners(socket_owner **owners, size_t *owner_count)
{
    DIR *proc = opendir("/proc");
    struct dirent *proc_entry;
    socket_owner *list = NULL;
    size_t used = 0, capacity = 0;

    *owners = NULL;
    *owner_count = 0;
    if (proc == NULL)
        return -1;
    while ((proc_entry = readdir(proc)) != NULL) {
        char fd_dir_path[64];
        char comm[SW_COMM_LEN];
        DIR *fd_dir;
        struct dirent *fd_entry;
        pid_t pid;
        if (!is_numeric_name(proc_entry->d_name))
            continue;
        pid = (pid_t)strtol(proc_entry->d_name, NULL, 10);
        snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%ld/fd", (long)pid);
        fd_dir = opendir(fd_dir_path);
        if (fd_dir == NULL)
            continue;
        read_comm(pid, comm, sizeof(comm));
        while ((fd_entry = readdir(fd_dir)) != NULL) {
            char link_path[PATH_MAX];
            char target[256];
            ssize_t length;
            unsigned long long inode;
            socket_owner *grown;
            if (!is_numeric_name(fd_entry->d_name))
                continue;
            if (snprintf(link_path, sizeof(link_path), "%s/%s", fd_dir_path,
                         fd_entry->d_name) >= (int)sizeof(link_path))
                continue;
            length = readlink(link_path, target, sizeof(target) - 1);
            if (length < 0)
                continue;
            target[length] = '\0';
            if (sscanf(target, "socket:[%llu]", &inode) != 1)
                continue;
            if (used == capacity) {
                capacity = capacity == 0 ? 128 : capacity * 2;
                grown = realloc(list, capacity * sizeof(*list));
                if (grown == NULL) {
                    free(list);
                    closedir(fd_dir);
                    closedir(proc);
                    return -1;
                }
                list = grown;
            }
            list[used].inode = inode;
            list[used].pid = pid;
            snprintf(list[used].comm, sizeof(list[used].comm), "%s", comm);
            used++;
        }
        closedir(fd_dir);
    }
    closedir(proc);
    if (used > 1)
        qsort(list, used, sizeof(*list), compare_owner_inode);
    *owners = list;
    *owner_count = used;
    return 0;
}

static const socket_owner *find_owner(const socket_owner *owners, size_t count,
                                      unsigned long long inode)
{
    size_t low = 0, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (owners[middle].inode < inode)
            low = middle + 1;
        else
            high = middle;
    }
    return low < count && owners[low].inode == inode ? &owners[low] : NULL;
}

static const char *tcp_state(unsigned state)
{
    static const char *states[] = {
        "UNKNOWN", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1",
        "FIN_WAIT2", "TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK",
        "LISTEN", "CLOSING", "NEW_SYN_RECV"
    };
    return state < sizeof(states) / sizeof(states[0]) ? states[state] : "UNKNOWN";
}

static int decode_address(const char *hex, int ipv6, char *out, size_t out_size)
{
    if (!ipv6) {
        unsigned value;
        struct in_addr addr;
        if (sscanf(hex, "%8x", &value) != 1)
            return -1;
        addr.s_addr = value; /* /proc/net/tcp exposes IPv4 in host byte order. */
        return inet_ntop(AF_INET, &addr, out, out_size) == NULL ? -1 : 0;
    } else {
        struct in6_addr addr6;
        unsigned group[4];
        int i;
        if (sscanf(hex, "%8x%8x%8x%8x", &group[0], &group[1], &group[2], &group[3]) != 4)
            return -1;
        for (i = 0; i < 4; i++) {
            uint32_t proc_group = (uint32_t)group[i];
            /* Each 32-bit word in /proc/net/tcp6 is shown in host byte order. */
            memcpy(&addr6.s6_addr[i * 4], &proc_group, sizeof(proc_group));
        }
        return inet_ntop(AF_INET6, &addr6, out, out_size) == NULL ? -1 : 0;
    }
}

static size_t print_tcp_file(const char *path, int ipv6, const socket_owner *owners,
                             size_t owner_count)
{
    FILE *fp = fopen(path, "r");
    char line[1024];
    size_t rows = 0;
    if (fp == NULL)
        return 0;
    if (fgets(line, sizeof(line), fp) == NULL) { /* heading */
        fclose(fp);
        return 0;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        char local_hex[65], remote_hex[65], local_ip[INET6_ADDRSTRLEN], remote_ip[INET6_ADDRSTRLEN];
        char local[128], remote[128];
        unsigned local_port, remote_port, state;
        unsigned long long inode;
        const socket_owner *owner;
        int matched = sscanf(line,
            " %*d: %64[0-9A-Fa-f]:%x %64[0-9A-Fa-f]:%x %x %*s %*s %*s %*u %*u %llu",
            local_hex, &local_port, remote_hex, &remote_port, &state, &inode);
        if (matched != 6 || decode_address(local_hex, ipv6, local_ip, sizeof(local_ip)) != 0 ||
            decode_address(remote_hex, ipv6, remote_ip, sizeof(remote_ip)) != 0)
            continue;
        if (ipv6) {
            snprintf(local, sizeof(local), "[%s]:%u", local_ip, local_port);
            snprintf(remote, sizeof(remote), "[%s]:%u", remote_ip, remote_port);
        } else {
            snprintf(local, sizeof(local), "%s:%u", local_ip, local_port);
            snprintf(remote, sizeof(remote), "%s:%u", remote_ip, remote_port);
        }
        owner = find_owner(owners, owner_count, inode);
        if (owner != NULL)
            printf("%-30s %-30s %-12s %-10llu %-7ld %s\n", local, remote,
                   tcp_state(state), inode, (long)owner->pid, owner->comm);
        else
            printf("%-30s %-30s %-12s %-10llu %-7s %s\n", local, remote,
                   tcp_state(state), inode, "?", "?");
        rows++;
    }
    fclose(fp);
    return rows;
}

void print_tcp_sockets(void)
{
    socket_owner *owners = NULL;
    size_t owner_count = 0;
    size_t rows;
    (void)collect_socket_owners(&owners, &owner_count);
    printf("TCP SOCKETS\n");
    printf("%-30s %-30s %-12s %-10s %-7s %s\n",
           "LOCAL", "REMOTE", "STATE", "INODE", "PID", "COMMAND");
    rows = print_tcp_file("/proc/net/tcp", 0, owners, owner_count);
    rows += print_tcp_file("/proc/net/tcp6", 1, owners, owner_count);
    if (rows == 0)
        printf("(no TCP sockets visible)\n");
    if (geteuid() != 0)
        printf("Note: '?' owners may require root or ptrace permission to inspect.\n");
    free(owners);
}
