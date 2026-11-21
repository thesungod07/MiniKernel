#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "proc.h"

typedef struct
{
    const char *name;
    pcb_t *(*pick)(void);
    int preemptive;
    uint64_t quantum;
} scheduler_t;

extern const scheduler_t sched_fcfs;
extern const scheduler_t sched_sjf;
extern const scheduler_t sched_srtf;
extern const scheduler_t sched_prio;
extern const scheduler_t sched_prio_p;
extern const scheduler_t sched_rr;

void sched_init(cpu_t *c, const scheduler_t *s);
void sched_start(cpu_t *c);

#endif
