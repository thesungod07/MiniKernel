# MiniKernel

A small operating system written in C that runs on a simulated CPU. The CPU, interrupts,
timer, user/kernel mode and address translation are simulated in `hw/`; the kernel in
`kernel/` runs real (hand-assembled) programs on top of it.

## Build and run

    make            # builds every test and the demo
    make test       # runs all test suites (prints only failures and "all ok")
    make demo       # runs the demonstration of every module
    make clean

Needs gcc and pthreads. Tests are in `tests/`, the demo is `demos/demo.c`.

## What is where

| Area | Lectures | Files |
|---|---|---|
| Instruction set, CPU, interrupts, timer, modes, address translation | L1, L2, L4, L29 (part) | `hw/isa.h`, `hw/cpu.[ch]`, `kernel/traps.c`, `kernel/boot.c` |
| Processes, PCB, context switch | L8 | `kernel/proc.[ch]` |
| Scheduler framework, FCFS, SJF, SRTF, Priority (both kinds), Round Robin | L9-L13 | `kernel/sched.[ch]` |
| MLQ, MLFQ, aging, convoy and starvation demos, batch vs multiprogramming vs time-sharing, Gantt charts and metrics | L3, L14-L16 | `kernel/sched.c`, `kernel/schedrun.[ch]` |
| Multi-CPU scheduling (global vs per-CPU queues, push/pull migration, affinity) and CFS | L17 | `kernel/smp.[ch]` |
| Threads: user-level (many-to-one), kernel-level (one-to-one), thread pool | L18 | `kernel/threads.[ch]` |
| IPC: pipes, message queues, shared memory, signals | L19 | `kernel/ipc.[ch]` |
| Race condition, semaphores, mutex, priority inversion and inheritance | L20-L24 | `kernel/sync.[ch]`, `tests/test_sync.c` |
| Deadlock: Banker's algorithm, detection, wait-for graph, recovery, prevention | L25-L28 | `kernel/deadlock.[ch]` |

`kernel/syscalls.h` lists the system call numbers and the calling convention
(number in R0, arguments in R1-R3, result in R0). `kernel/progbuild.h` is a tiny helper
for writing test programs for the simulated CPU.

## Design notes and simplifications

* One tick = one instruction = one time unit; the scheduler timer fires after every instruction,
  so Gantt charts are exact and match textbook answers.
* Blocking system calls either hand over directly (semaphores, mutexes) or back the PC up one
  instruction and retry when woken (pipes, queues, resources).
* The multi-CPU scheduler is a discrete-time simulator of run queues, balancing, affinity and CFS.
  The instruction-level machine is a single core.
* Shared memory segments are owned by the kernel and accessed through system calls (the
  address translation is one base/limit pair per process, so two processes cannot be mapped onto
  the same memory).
* CFS picks the smallest virtual runtime with a linear scan instead of a red-black tree; the
  scheduling order is the same.
* Priority inheritance is single level (it does not follow chains of blocked owners).
* Threads (`kernel/threads.c`) run on the host (ucontext and pthreads) so they can be measured directly.
