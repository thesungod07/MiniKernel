#include <string.h>
#include "proc.h"

pcb_t proc_table[MAX_PROCS];
pcb_t *current_proc;
static int next_pid;

void proc_init(void)
{
    memset(proc_table, 0, sizeof(proc_table));
    next_pid = 1;
    current_proc = NULL;
}

int proc_create(cpu_t *c, const uint32_t *image, size_t nwords, int priority, uint64_t burst)
{
    if (nwords > PROC_MEM_WORDS) { return -1; }

    int index = -1;
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (proc_table[i].state == PROC_UNUSED) { index = i; break; }
    }
    if (index == -1) { return -1; }

    uint32_t base = USER_BASE + (index * PROC_MEM_WORDS);

    memset(&c->ram[base], 0, PROC_MEM_WORDS * sizeof(uint32_t));
    memcpy(&c->ram[base], image, nwords * sizeof(uint32_t));

    pcb_t *p = &proc_table[index];
    memset(p, 0, sizeof(*p));

    p->pid = next_pid;
    next_pid++;
    p->parent_pid = -1;
    p->state = PROC_READY;
    p->base = base;
    p->limit = PROC_MEM_WORDS;
    p->priority = priority;
    p->arrival_time = c->cycles;
    p->burst_time = burst;
    p->remaining_time = burst;
    p->first_run_time = -1;

    return p->pid;
}

void context_save(cpu_t *c, pcb_t *p)
{
    for (int i = 0; i < NREGS; i++) { p->reg[i] = c->reg[i]; }
    p->zf = c->zf;
    p->pc = c->saved_pc;
}

void context_load(cpu_t *c, pcb_t *p)
{
    for (int i = 0; i < NREGS; i++) { c->reg[i] = p->reg[i]; }
    c->zf = p->zf;
    c->base = p->base;
    c->limit = p->limit;
    c->saved_pc = p->pc;
    c->saved_mode = MODE_USER;
    c->saved_int = true;
}

void context_switch(cpu_t *c, pcb_t *to)
{
    if (current_proc != NULL && current_proc->state == PROC_RUNNING)
    {
        context_save(c, current_proc);
        current_proc->state = PROC_READY;
    }

    context_load(c, to);
    to->state = PROC_RUNNING;
    if (to->first_run_time == -1) { to->first_run_time = (int64_t)c->cycles; }

    current_proc = to;
}
