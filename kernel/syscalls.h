#ifndef SYSCALLS_H
#define SYSCALLS_H
#include "../hw/cpu.h"
#include "kernel.h"

/* Calling convention: syscall number in R0, arguments in R1..R3,
 * result comes back in R0. */
enum
{
    SYS_SLEEP = 2,          /* R1 = cycles to sleep (simulated I/O) */
    SYS_GETPID = 4,
    SYS_SIGACTION = 5,      /* R1 = signal, R2 = handler address (logical) */
    SYS_KILL = 6,           /* R1 = pid, R2 = signal */
    SYS_SIGRETURN = 7,

    SYS_PIPE_WRITE = 10,    /* R1 = pipe, R2 = value */
    SYS_PIPE_READ = 11,     /* R1 = pipe          -> R0 = value or EOF */
    SYS_PIPE_CLOSE = 12,
    SYS_SHM_READ = 13,      /* R1 = segment, R2 = index -> R0 = value */
    SYS_SHM_WRITE = 14,     /* R1 = segment, R2 = index, R3 = value */
    SYS_MSG_SEND = 15,      /* R1 = queue, R2 = type, R3 = value */
    SYS_MSG_RECV = 16,      /* R1 = queue, R2 = type (0 = any) -> R0 = value */

    SYS_SEM_WAIT = 20,      /* R1 = semaphore */
    SYS_SEM_SIGNAL = 21,
    SYS_MUTEX_LOCK = 22,    /* R1 = mutex */
    SYS_MUTEX_UNLOCK = 23,

    SYS_RES_REQUEST = 30,   /* R1 = resource type, R2 = count -> R0 = 0 or -1 */
    SYS_RES_RELEASE = 31
};

#define SYSCALL_MAX 40
#define PIPE_EOF 0xFFFFFFFFu
#define SIGUSR1 1
#define SIGUSR2 2
#define SIGTERM 3
#define SIGKILL 9

typedef void (*syscall_fn)(cpu_t *c);
void syscall_register(unsigned num, syscall_fn fn);
#endif
