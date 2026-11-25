#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include "proc.h"
#include "syscalls.h"

typedef struct
{
    const char *name;
    pcb_t *(*pick)(void);
    int preemptive;
    uint64_t quantum;
    int io_hold;        /* 1: the CPU sits idle while a job does I/O (batch mode) */
    int aging;          /* 1: waiting processes slowly gain priority */
} scheduler_t;

extern const scheduler_t sched_fcfs;
extern const scheduler_t sched_sjf;
extern const scheduler_t sched_srtf;
extern const scheduler_t sched_prio;
extern const scheduler_t sched_prio_p;
extern const scheduler_t sched_prio_aging;
extern const scheduler_t sched_rr;
extern const scheduler_t sched_mlq;
extern const scheduler_t sched_mlfq;
extern const scheduler_t sched_batch;       /* FCFS, CPU held during I/O */
extern const scheduler_t sched_multiprog;   /* FCFS, switch on I/O */
extern const scheduler_t sched_timeshare;   /* RR, switch on I/O and on quantum */

void sched_init(cpu_t *c, const scheduler_t *s);
void sched_start(cpu_t *c);

/* ---- services for other kernel modules ---- */
void syscall_register(unsigned num, syscall_fn fn);
void sched_block_on(cpu_t *c, const void *chan);      /* current process waits on chan */
int  sched_wake_one(const void *chan);                /* oldest waiter -> READY, returns its pid or 0 */
int  sched_wake_all(const void *chan);                /* returns how many were woken */
const void *sched_wait_chan(const pcb_t *p);
int  sched_interrupt_sleep(pcb_t *p);                 /* wake a sleeper early */
void sched_terminate(cpu_t *c, pcb_t *p, int code);
int  sched_get_eff_prio(const pcb_t *p);
void sched_set_eff_prio(pcb_t *p, int v);
pcb_t *sched_find_pid(int pid);
void sched_set_monitor(void (*fn)(cpu_t *), uint64_t interval);
void sched_set_resume_hook(void (*fn)(cpu_t *, pcb_t *));
void sched_set_exit_hook(void (*fn)(pcb_t *));

/* ---- statistics ---- */
uint64_t sched_busy_cycles(void);
uint64_t sched_switches(void);
uint64_t sched_io_time(const pcb_t *p);
const int *sched_gantt(int *len);                     /* pid that ran in each cycle, 0 = idle */

#endif
