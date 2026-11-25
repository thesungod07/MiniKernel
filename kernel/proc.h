#ifndef PROC_H
#define PROC_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "kernel.h"

#define MAX_PROCS 16
#define PROC_MEM_WORDS 240

typedef enum { PROC_UNUSED, PROC_NEW, PROC_READY, PROC_RUNNING, PROC_WAITING, PROC_TERMINATED } proc_state_t;

typedef struct
{
    int pid;
    int parent_pid;
    proc_state_t state;
    int exit_code;
    uint32_t reg[NREGS];
    uint32_t pc;
    bool zf;
    uint32_t base;
    uint32_t limit;
    int priority;
    uint64_t arrival_time;
    uint64_t burst_time;
    uint64_t remaining_time;
    int64_t first_run_time;
    uint64_t finish_time;
} pcb_t;

extern pcb_t proc_table[MAX_PROCS];
extern pcb_t *current_proc;

void proc_init(void);
int proc_create(cpu_t *c, const uint32_t *image, size_t nwords, int priority, uint64_t burst);
void context_save(cpu_t *c, pcb_t *p);
void context_load(cpu_t *c, pcb_t *p);
void context_switch(cpu_t *c, pcb_t *to);
#endif
