# Sample output

Values below are illustrative; PIDs and resource usage will differ.

## Default dashboard

```text
$ ./build/bin/syswatch
SYSTEM
Load average: 0.42 0.37 0.31
Memory:       3.8 GiB used / 15.5 GiB total

PROCESSES (84 visible)
PID     PPID    S        RSS        VSZ      FDs     FAULTS    CPU(s)  COMMAND
1       0       S    12.4 MiB   22.0 MiB       10       2147      3.44  systemd
842     1       S     8.7 MiB   16.2 MiB        7        921      0.14  sshd
3912    2104    R     1.8 MiB    2.5 MiB        4         88      0.01  syswatch
... 81 more processes (use --all to show every process)

WARNINGS
No obvious zombie or high-FD conditions detected.
```

## Process details and maps

```text
$ ./build/bin/syswatch --pid 4217 --maps 4217
PROCESS 4217 (memory_leak)
Parent PID:       1820
State:            S
RSS / VSZ:        64.9 MiB / 66.4 MiB
Minor/major fault:16622 / 0
Context switches: 18 voluntary, 2 involuntary
Open FDs:         3
CPU time:         0.02 s user, 0.05 s system

MEMORY MAPS FOR PID 4217
Mappings:   23
Mapped:     66.4 MiB
Anonymous:  64.3 MiB
File-backed:  2.1 MiB
Heap:       64.0 MiB
Stack(s):   132.0 KiB
Executable: 1.5 MiB
```

## Process tree with an injected zombie

```text
$ ./build/bin/zombie 60 &
$ ./build/bin/syswatch --tree
PROCESS TREE
|- systemd(1) [S]
  |- bash(1820) [S]
    |- zombie(4330) [S]
      |- zombie(4331) [Z] ZOMBIE
    |- syswatch(4332) [R]
```

## TCP inode-to-process correlation

```text
$ ./build/bin/tcp_server 9090 &
$ ./build/bin/syswatch --net
TCP SOCKETS
LOCAL                          REMOTE                         STATE        INODE      PID     COMMAND
0.0.0.0:9090                   0.0.0.0:0                      LISTEN       493812     4450    tcp_server
127.0.0.1:9090                 127.0.0.1:51944                ESTABLISHED  493901     4450    tcp_server
127.0.0.1:51944                127.0.0.1:9090                 ESTABLISHED  493900     4452    tcp_client
Note: '?' owners may require root or ptrace permission to inspect.
```

## Failure fixture

```text
$ ./build/bin/tcp_failure
attempting nonblocking connect to 203.0.113.1:65000 (timeout 3000 ms)
diagnosis: connection timed out
```
