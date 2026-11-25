#ifndef SMP_H
#define SMP_H

#include <stdint.h>

/* A discrete-time multi-CPU scheduling simulator (one tick = one time unit on
 * every CPU at once). The instruction-level machine in hw/ is a single core, so
 * this module models the scheduling side of SMP directly: run queues, load
 * balancing, processor affinity and CFS. */

#define SMP_MAX_TASKS 16
#define SMP_MAX_CPUS 4

typedef struct
{
    /* inputs */
    int arrival, burst, nice, home_cpu;
    unsigned affinity;              /* bit mask of allowed CPUs, 0 = any */

    /* state and results */
    int remaining, state;           /* 0 new, 1 ready, 2 running, 3 done */
    int cpu;                        /* the run queue it sits on (per-CPU mode) */
    int last_cpu;
    unsigned weight;
    uint64_t vruntime;              /* CFS virtual runtime, in 1/1000 ticks */
    uint64_t enq;
    int first_run, finish, migrations;
    int ticks_on[SMP_MAX_CPUS];
} smp_task_t;

typedef struct
{
    int ncpus;
    int percpu;                     /* 1: one run queue per CPU, 0: a single global queue */
    int cfs;                        /* 1: pick the smallest vruntime, 0: round robin */
    int quantum;                    /* round robin time slice */
    int push_interval;              /* per-CPU mode: every N ticks move a task from the busiest to the idlest queue (0 = off) */
    int pull;                       /* per-CPU mode: an idle CPU steals from the busiest queue */
    int soft_affinity;              /* global mode: prefer the CPU a task last ran on */
    int max_ticks;                  /* stop early (0 = run until everything finishes) */
} smp_config_t;

typedef struct
{
    int ntasks;
    uint64_t makespan;
    uint64_t busy[SMP_MAX_CPUS];
    int migrations;
    double avg_turnaround;
} smp_result_t;

void smp_reset(void);
int  smp_add_task(int arrival, int burst, int nice, int home_cpu, unsigned affinity);
void smp_run(const smp_config_t *cfg, smp_result_t *out);
const smp_task_t *smp_task(int i);
unsigned smp_weight_of_nice(int nice);
void smp_print(const smp_config_t *cfg, const smp_result_t *r, const char *title);

#endif
