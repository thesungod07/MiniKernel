#include <string.h>
#include "proc.h"

pcb_t proc_table[MAX_PROCS];
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
