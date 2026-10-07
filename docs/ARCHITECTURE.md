# Architecture and engineering notes

## Data flow

```text
/proc/<pid>/stat ────────┐
/proc/<pid>/status ──────┼─> process_info[] ─> table / detail / tree / warnings
/proc/<pid>/fd ──────────┘          │
                                    └─ socket:[inode] ─┐
/proc/net/tcp + tcp6 ─────────────────────────────────┼─> TCP table
/proc/<pid>/comm ─────────────────────────────────────┘

/proc/<pid>/maps ─> maps_summary ─> memory map report
/proc/loadavg + /proc/meminfo ─> system summary
```

## Important implementation choices

- `/proc/<pid>/stat` is parsed by finding the final `)` around `comm`, rather than splitting the
  whole line on spaces. Linux process names can contain spaces and parentheses.
- A process may exit between listing `/proc` and opening one of its files. Those races are treated
  as normal and skipped instead of aborting the scan.
- Resident pages from field 24 of `stat` are multiplied by the runtime page size from
  `sysconf(_SC_PAGESIZE)`. CPU ticks use `sysconf(_SC_CLK_TCK)`.
- TCP endpoints come from `/proc/net/tcp` and `/proc/net/tcp6`. Each socket inode is joined to
  symlink targets of the form `socket:[inode]` found under every visible `/proc/<pid>/fd`.
- Visibility follows Linux permissions. Running as root reveals more file descriptors and socket
  owners, but normal process inspection does not require root.
- Process-tree traversal has a depth guard and visited set so malformed or surprising parent data
  cannot recurse forever.

## Complexity

- Process collection is linear in visible processes plus visible file descriptors.
- Tree rendering currently searches the process array for children, making it O(P²). That is a
  deliberate clarity tradeoff for a small diagnostic tool; a production version would build a
  parent-to-children adjacency index.
- Socket ownership is sorted by inode and looked up with binary search.

## Extensions worth discussing in an interview

1. Sample `/proc/stat` twice to calculate per-process CPU percentages.
2. Read `TCP_INFO` for RTT, retransmits, congestion window, and delivery-rate diagnostics.
3. Replace periodic scans with netlink (`NETLINK_SOCK_DIAG`) and proc connector events.
4. Add mount-namespace awareness and cgroup v2 CPU/memory/IO metrics.
5. Split collection from presentation and expose a Unix-domain socket or Prometheus endpoint.
6. Add privilege separation: a minimal collector plus an unprivileged UI.

The project intentionally uses only Linux/POSIX interfaces and the C standard library. This keeps
the systems concepts visible and avoids hiding `/proc`, sockets, process lifecycle, or memory
accounting behind third-party libraries.
