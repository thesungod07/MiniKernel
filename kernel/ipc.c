#include <string.h>
#include "ipc.h"

#define PIPE_CAP 8
#define MAX_PIPES 4
#define MAX_SEGS 4
#define SEG_WORDS 64
#define MAX_QUEUES 4
#define QUEUE_CAP 4
#define NSIG 10

typedef struct { uint32_t buf[PIPE_CAP]; int head, count, closed, used; } pipe_t;
typedef struct { uint32_t words[SEG_WORDS]; int size, used; } shm_t;
typedef struct { uint32_t type, value; } msg_t;
typedef struct { msg_t m[QUEUE_CAP]; int count, used; } msgq_t;

static pipe_t pipes[MAX_PIPES];
static shm_t segs[MAX_SEGS];
static msgq_t queues[MAX_QUEUES];

static uint32_t sig_handler[MAX_PROCS][NSIG];
static unsigned sig_pending[MAX_PROCS];
static int in_handler[MAX_PROCS];
static uint32_t sig_regs[MAX_PROCS][NREGS];
static uint32_t sig_pc[MAX_PROCS];
static bool sig_zf[MAX_PROCS];

static int slot_of(const pcb_t *p) { return (int)(p - proc_table); }

/* A blocking syscall that cannot finish yet backs the PC up one instruction,
 * so the SYSCALL runs again when the process is woken (the "retry" style). */
static void block_and_retry(cpu_t *c, const void *chan)
{
    c->saved_pc -= 1;
    sched_block_on(c, chan);
}

/* ---------------- pipes ---------------- */

int pipe_create(void)
{
    for (int i = 0; i < MAX_PIPES; i++)
    {
        if (!pipes[i].used) { memset(&pipes[i], 0, sizeof pipes[i]); pipes[i].used = 1; return i; }
    }
    return -1;
}

static void sys_pipe_write(cpu_t *c)
{
    uint32_t id = c->reg[1], value = c->reg[2];
    if (id >= MAX_PIPES || !pipes[id].used || pipes[id].closed) { c->reg[0] = (uint32_t)-1; return; }

    pipe_t *p = &pipes[id];
    if (p->count == PIPE_CAP) { block_and_retry(c, p); return; }

    p->buf[(p->head + p->count) % PIPE_CAP] = value;
    p->count++;
    c->reg[0] = 0;
    sched_wake_all(p);
}

static void sys_pipe_read(cpu_t *c)
{
    uint32_t id = c->reg[1];
    if (id >= MAX_PIPES || !pipes[id].used) { c->reg[0] = (uint32_t)-1; return; }

    pipe_t *p = &pipes[id];
    if (p->count > 0)
    {
        c->reg[0] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE_CAP;
        p->count--;
        sched_wake_all(p);
        return;
    }
    if (p->closed) { c->reg[0] = PIPE_EOF; return; }
    block_and_retry(c, p);
}

static void sys_pipe_close(cpu_t *c)
{
    uint32_t id = c->reg[1];
    if (id >= MAX_PIPES || !pipes[id].used) { c->reg[0] = (uint32_t)-1; return; }
    pipes[id].closed = 1;
    c->reg[0] = 0;
    sched_wake_all(&pipes[id]);
}

/* ---------------- shared memory ---------------- */
/* A segment is owned by the kernel and accessed through syscalls (the simulated
 * MMU only has one base/limit pair per process, so it cannot map two processes
 * onto the same frames). */

int shm_create(int words)
{
    if (words <= 0 || words > SEG_WORDS) { return -1; }
    for (int i = 0; i < MAX_SEGS; i++)
    {
        if (!segs[i].used) { memset(&segs[i], 0, sizeof segs[i]); segs[i].used = 1; segs[i].size = words; return i; }
    }
    return -1;
}

uint32_t shm_peek(int seg, int index)
{
    if (seg < 0 || seg >= MAX_SEGS || index < 0 || index >= SEG_WORDS) { return 0; }
    return segs[seg].words[index];
}

static void sys_shm_read(cpu_t *c)
{
    uint32_t id = c->reg[1], idx = c->reg[2];
    if (id >= MAX_SEGS || !segs[id].used || idx >= (uint32_t)segs[id].size) { c->reg[0] = (uint32_t)-1; return; }
    c->reg[0] = segs[id].words[idx];
}

static void sys_shm_write(cpu_t *c)
{
    uint32_t id = c->reg[1], idx = c->reg[2], value = c->reg[3];
    if (id >= MAX_SEGS || !segs[id].used || idx >= (uint32_t)segs[id].size) { c->reg[0] = (uint32_t)-1; return; }
    segs[id].words[idx] = value;
    c->reg[0] = 0;
}

/* ---------------- message queues ---------------- */

int msgq_create(void)
{
    for (int i = 0; i < MAX_QUEUES; i++)
    {
        if (!queues[i].used) { memset(&queues[i], 0, sizeof queues[i]); queues[i].used = 1; return i; }
    }
    return -1;
}

static void sys_msg_send(cpu_t *c)
{
    uint32_t id = c->reg[1], type = c->reg[2], value = c->reg[3];
    if (id >= MAX_QUEUES || !queues[id].used) { c->reg[0] = (uint32_t)-1; return; }

    msgq_t *q = &queues[id];
    if (q->count == QUEUE_CAP) { block_and_retry(c, q); return; }

    q->m[q->count].type = type;
    q->m[q->count].value = value;
    q->count++;
    c->reg[0] = 0;
    sched_wake_all(q);
}

static void sys_msg_recv(cpu_t *c)
{
    uint32_t id = c->reg[1], type = c->reg[2];
    if (id >= MAX_QUEUES || !queues[id].used) { c->reg[0] = (uint32_t)-1; return; }

    msgq_t *q = &queues[id];
    for (int i = 0; i < q->count; i++)
    {
        if (type == 0 || q->m[i].type == type)
        {
            uint32_t value = q->m[i].value;
            for (int j = i; j + 1 < q->count; j++) { q->m[j] = q->m[j + 1]; }
            q->count--;
            c->reg[0] = value;
            sched_wake_all(q);
            return;
        }
    }
    block_and_retry(c, q);
}

/* ---------------- signals ---------------- */

static void sys_sigaction(cpu_t *c)
{
    uint32_t sig = c->reg[1], handler = c->reg[2];
    if (sig == 0 || sig >= NSIG || sig == SIGKILL) { c->reg[0] = (uint32_t)-1; return; }
    sig_handler[slot_of(current_proc)][sig] = handler;
    c->reg[0] = 0;
}

static void sys_kill(cpu_t *c)
{
    uint32_t pid = c->reg[1], sig = c->reg[2];
    pcb_t *t = sched_find_pid((int)pid);

    if (t == NULL || t->state == PROC_TERMINATED || sig == 0 || sig >= NSIG) { c->reg[0] = (uint32_t)-1; return; }

    c->reg[0] = 0;                              /* set the result before anything can switch process */
    int s = slot_of(t);

    if (sig == SIGKILL || sig_handler[s][sig] == 0)
    {
        sched_terminate(c, t, -1);              /* default action: terminate */
        return;
    }

    sig_pending[s] |= 1u << sig;
    sched_interrupt_sleep(t);                   /* a signal ends a sleep early */
}

/* Called whenever a process is about to resume: if a signal is pending, run its
 * handler first. The interrupted registers and PC are kept until SIGRETURN. */
static void signal_deliver(cpu_t *c, pcb_t *p)
{
    int s = slot_of(p);
    if (in_handler[s] || sig_pending[s] == 0) { return; }

    int sig = 1;
    while (!(sig_pending[s] & (1u << sig))) { sig++; }
    sig_pending[s] &= ~(1u << sig);

    memcpy(sig_regs[s], c->reg, sizeof(c->reg));
    sig_zf[s] = c->zf;
    sig_pc[s] = c->saved_pc;
    in_handler[s] = 1;

    c->saved_pc = sig_handler[s][sig];
    c->reg[1] = (uint32_t)sig;
}

static void sys_sigreturn(cpu_t *c)
{
    int s = slot_of(current_proc);
    if (!in_handler[s]) { c->reg[0] = (uint32_t)-1; return; }

    memcpy(c->reg, sig_regs[s], sizeof(c->reg));
    c->zf = sig_zf[s];
    c->saved_pc = sig_pc[s];
    in_handler[s] = 0;
}

/* ---------------- setup ---------------- */

void ipc_init(void)
{
    memset(pipes, 0, sizeof pipes);
    memset(segs, 0, sizeof segs);
    memset(queues, 0, sizeof queues);
    memset(sig_handler, 0, sizeof sig_handler);
    memset(sig_pending, 0, sizeof sig_pending);
    memset(in_handler, 0, sizeof in_handler);

    syscall_register(SYS_PIPE_WRITE, sys_pipe_write);
    syscall_register(SYS_PIPE_READ, sys_pipe_read);
    syscall_register(SYS_PIPE_CLOSE, sys_pipe_close);
    syscall_register(SYS_SHM_READ, sys_shm_read);
    syscall_register(SYS_SHM_WRITE, sys_shm_write);
    syscall_register(SYS_MSG_SEND, sys_msg_send);
    syscall_register(SYS_MSG_RECV, sys_msg_recv);
    syscall_register(SYS_SIGACTION, sys_sigaction);
    syscall_register(SYS_KILL, sys_kill);
    syscall_register(SYS_SIGRETURN, sys_sigreturn);
    sched_set_resume_hook(signal_deliver);
}
