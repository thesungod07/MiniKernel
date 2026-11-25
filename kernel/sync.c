#include <string.h>
#include "sync.h"

#define MAX_SEMS 8
#define MAX_MUTEXES 8

typedef struct { int value, used; } ksem_t;
typedef struct { int used, locked, owner_pid; } kmutex_t;

static ksem_t sems[MAX_SEMS];
static kmutex_t mutexes[MAX_MUTEXES];
static int inheritance;

int ksem_create(int initial)
{
    for (int i = 0; i < MAX_SEMS; i++)
    {
        if (!sems[i].used) { sems[i].used = 1; sems[i].value = initial; return i; }
    }
    return -1;
}

int kmutex_create(void)
{
    for (int i = 0; i < MAX_MUTEXES; i++)
    {
        if (!mutexes[i].used) { mutexes[i].used = 1; mutexes[i].locked = 0; mutexes[i].owner_pid = 0; return i; }
    }
    return -1;
}

void sync_set_inheritance(int on) { inheritance = on; }

/* wait(): decrement; if the value went negative, sleep until a signal hands over. */
static void sys_sem_wait(cpu_t *c)
{
    uint32_t id = c->reg[1];
    if (id >= MAX_SEMS || !sems[id].used) { c->reg[0] = (uint32_t)-1; return; }

    ksem_t *s = &sems[id];
    s->value--;
    c->reg[0] = 0;                              /* the result is final even if we block */
    if (s->value < 0) { sched_block_on(c, s); }
}

static void sys_sem_signal(cpu_t *c)
{
    uint32_t id = c->reg[1];
    if (id >= MAX_SEMS || !sems[id].used) { c->reg[0] = (uint32_t)-1; return; }

    ksem_t *s = &sems[id];
    s->value++;
    c->reg[0] = 0;
    if (s->value <= 0) { sched_wake_one(s); }
}

static void sys_mutex_lock(cpu_t *c)
{
    uint32_t id = c->reg[1];
    if (id >= MAX_MUTEXES || !mutexes[id].used) { c->reg[0] = (uint32_t)-1; return; }

    kmutex_t *m = &mutexes[id];
    if (!m->locked)
    {
        m->locked = 1;
        m->owner_pid = current_proc->pid;
        c->reg[0] = 0;
        return;
    }
    if (m->owner_pid == current_proc->pid) { c->reg[0] = (uint32_t)-1; return; }   /* not recursive */

    if (inheritance)
    {
        pcb_t *owner = sched_find_pid(m->owner_pid);
        int mine = sched_get_eff_prio(current_proc);
        if (owner != NULL && mine < sched_get_eff_prio(owner)) { sched_set_eff_prio(owner, mine); }
    }

    c->reg[0] = 0;                              /* ownership is handed over by unlock */
    sched_block_on(c, m);
}

static void sys_mutex_unlock(cpu_t *c)
{
    uint32_t id = c->reg[1];
    if (id >= MAX_MUTEXES || !mutexes[id].used) { c->reg[0] = (uint32_t)-1; return; }

    kmutex_t *m = &mutexes[id];
    if (!m->locked || m->owner_pid != current_proc->pid) { c->reg[0] = (uint32_t)-1; return; }

    c->reg[0] = 0;
    if (inheritance) { sched_set_eff_prio(current_proc, current_proc->priority); }

    int next = sched_wake_one(m);
    if (next != 0) { m->owner_pid = next; }     /* direct hand-over to the oldest waiter */
    else { m->locked = 0; m->owner_pid = 0; }
}

void sync_init(void)
{
    memset(sems, 0, sizeof sems);
    memset(mutexes, 0, sizeof mutexes);
    inheritance = 0;

    syscall_register(SYS_SEM_WAIT, sys_sem_wait);
    syscall_register(SYS_SEM_SIGNAL, sys_sem_signal);
    syscall_register(SYS_MUTEX_LOCK, sys_mutex_lock);
    syscall_register(SYS_MUTEX_UNLOCK, sys_mutex_unlock);
}
