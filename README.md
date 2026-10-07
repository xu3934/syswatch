# syswatch

`syswatch` is a dependency-free Linux process and network diagnostic agent written in C. It turns
raw kernel interfaces under `/proc` into a compact troubleshooting view and ships with controlled
fault fixtures for observing memory, process, file-descriptor, CPU, and TCP failure behavior.

It is a portfolio project aimed at systems/Linux engineering roles. It demonstrates sockets,
virtual memory, process lifecycle, C, Linux introspection, race-aware error handling, and practical
troubleshooting. It is not affiliated with Jane Street.

## What it inspects

- Process identity, parent, state, total CPU time, RSS, VSZ, minor/major page faults
- Voluntary and involuntary context switches from `/proc/<pid>/status`
- Open file descriptor counts and zombie/high-FD warnings
- Parent/child process tree with explicit zombie markers
- `/proc/<pid>/maps` totals: anonymous, file-backed, heap, stack, and executable mappings
- IPv4 and IPv6 TCP endpoints, connection state, socket inode, owning PID, and command
- System load average and memory availability
- Continuous refresh for observing a fault as it develops

## Build

Requirements: Linux, GNU Make, and a C11 compiler such as GCC or Clang.

```bash
make
make check
```

Executables are written to `build/bin/`. Build with Clang or sanitizers like this:

```bash
make clean
make CC=clang CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined"
```

> `syswatch` reads Linux `/proc`; it is not intended to run natively on macOS or Windows. On a
> Windows host, build it inside WSL 2 or a Linux VM/container with a host-like `/proc` view.

## Quick start

```bash
# Dashboard: system summary, first 20 processes, warnings
./build/bin/syswatch

# All visible processes and the parent/child tree
./build/bin/syswatch --all
./build/bin/syswatch --tree

# Deep dive into a process
./build/bin/syswatch --pid $$
./build/bin/syswatch --maps $$

# TCP sockets joined to owning processes through socket inode
./build/bin/syswatch --net
sudo ./build/bin/syswatch --net   # reveals owners hidden by ptrace/proc permissions

# Refresh every two seconds; Ctrl-C exits
./build/bin/syswatch --all --net --interval 2
```

Run `./build/bin/syswatch --help` for the complete command reference.

## TCP client/server demo

Use three terminals so the listening and established sockets remain easy to inspect:

```bash
# Terminal 1
./build/bin/tcp_server 9090

# Terminal 2
./build/bin/syswatch --net --interval 1

# Terminal 3
./build/bin/tcp_client 127.0.0.1 9090 "hello over TCP"
```

The server exercises `socket`, `setsockopt`, `bind`, `listen`, `accept`, `recv`, and `send`. The
client uses `getaddrinfo`, `socket`, `connect`, `send`, and `recv`. `syswatch --net` then joins the
kernel TCP row to a process by matching the row's inode against `/proc/<pid>/fd/*` symlinks.

## Fault injection lab

These fixtures intentionally behave badly. Run them only in a disposable shell, keep their default
limits, and stop them with Ctrl-C. A container or VM is recommended.

| Fixture | Run | Observe |
|---|---|---|
| Memory leak | `./build/bin/memory_leak 4 1` | RSS, VSZ, page faults, and anonymous mappings grow |
| Zombie | `./build/bin/zombie 60` | child state `Z`, warning, process-tree parent |
| CPU hog | `./build/bin/cpu_hog 30` | accumulated user CPU time and involuntary switches |
| FD leak | `./build/bin/fd_leak 2048 10` | FD count climbs; warning begins at 1024 |
| TCP timeout/refusal | `./build/bin/tcp_failure` | nonblocking connect, `poll`, timeout/error diagnosis |
| TCP listener | `./build/bin/tcp_server 9090` | LISTEN socket, inode, owning PID |

Example observation loop:

```bash
./build/bin/memory_leak 8 1 &
leaker=$!
./build/bin/syswatch --pid "$leaker" --maps "$leaker" --interval 2
kill "$leaker"
wait "$leaker" 2>/dev/null
```

The default `tcp_failure` target is `203.0.113.1`, an address reserved for documentation. Depending
on the local network, the result may be an immediate routing/refusal error or a timeout; both are
valid diagnostic paths.

## Reading the output correctly

- **RSS** is the process's currently resident memory, derived from resident pages. **VSZ** is the
  total virtual address-space size; a large VSZ alone does not prove physical-memory pressure.
- **Minor faults** resolve without disk I/O, while **major faults** require storage access.
- **CPU(s)** is accumulated user + system CPU time, not an instantaneous percentage.
- **FDs = `?`** or **PID = `?`** means the kernel exposed the row but denied access to enough
  per-process information to attribute it. Retry as root only when appropriate.
- `/proc` is a moving snapshot. Processes and descriptors can disappear during a scan; the program
  treats that race as expected behavior.

## Repository layout

```text
syswatch/
├── include/syswatch.h       shared data types and interfaces
├── src/main.c               CLI, dashboard, warning policy, refresh loop
├── src/proc.c               process, maps, tree, and system-memory collectors
├── src/net.c                TCP parser and socket-inode-to-PID join
├── src/util.c               formatting and validation helpers
├── examples/                TCP demo and controlled fault fixtures
├── docs/ARCHITECTURE.md     design decisions, complexity, extensions
├── docs/SAMPLE_OUTPUT.md    portfolio-ready command transcripts
└── Makefile
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for parsing choices, data flow, limitations, and
production extensions. See [docs/SAMPLE_OUTPUT.md](docs/SAMPLE_OUTPUT.md) for complete examples.

## Known limitations

- Linux-only by design; `/proc` layout and ptrace policy determine visibility.
- CPU is accumulated time rather than sampled utilization percentage.
- TCP inspection covers `/proc/net/tcp` and `tcp6`, not UDP or Unix-domain sockets.
- Network namespaces can make another process's sockets invisible from the current namespace.
- This is an educational diagnostic agent, not a hardened privileged production daemon.

## License

MIT. See `LICENSE`.
