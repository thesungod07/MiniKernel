#include <string.h>
#include "proc.h"

pcb_t proc_table[MAX_PROCS];
pcb_t *current_proc;
static int next_pid;

void proc_init(void)
{
    memset(&proc_table, 0, sizeof(proc_table));
    next_pid = 1;
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

    memset(&proc_table[index], 0, sizeof(pcb_t));
    
    proc_table[index].pid = next_pid;
    next_pid++;

    proc_table[index].parent_pid = -1;
    proc_table[index].state = PROC_READY;
    proc_table[index].base = base;
    proc_table[index].limit = PROC_MEM_WORDS;
    proc_table[index].priority = priority;
    proc_table[index].arrival_time = c->cycles;
    proc_table[index].burst_time = burst;
    proc_table[index].remaining_time = burst;
    proc_table[index].first_run_time = -1;

    return proc_table[index].pid;
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
