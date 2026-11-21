#ifndef SCHED_H
#define SCHED_H

#include "proc.h"

typedef struct
{
    const char *name;
    pcb_t *(*pick)(void);
    int preemptive;
    uint64_t quantum;
} scheduler_t;

extern const scheduler_t sched_fcfs;

void sched_init(cpu_t *c, const scheduler_t *s);
void sched_start(cpu_t *c);

#endif
