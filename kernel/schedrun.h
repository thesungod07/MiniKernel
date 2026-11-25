#ifndef SCHEDRUN_H
#define SCHEDRUN_H

#include <stdint.h>
#include "sched.h"

/* A job for the workload runner. burst = total instructions the job runs.
 * If io_len > 0 the job sleeps for io_len cycles after io_at instructions. */
typedef struct
{
    int arrival;
    int priority;
    int burst;
    int io_at;
    int io_len;
} job_t;

typedef struct
{
    int nproc;
    double avg_wait, avg_turnaround, avg_response;
    double cpu_util;
    uint64_t makespan;
    uint64_t switches;
} sched_stats_t;

/* run the jobs under one scheduler on a fresh machine and collect statistics */
int sched_run_workload(const scheduler_t *s, const job_t *jobs, int n, sched_stats_t *out, int verbose);

/* run every scheduler in the list on the same jobs and print a comparison table */
void sched_compare(const scheduler_t *const *list, int k, const job_t *jobs, int n);

void sched_print_gantt(void);
void sched_print_report(const char *name, uint64_t total_cycles);
void sched_compute_stats(sched_stats_t *out, uint64_t total_cycles);

#endif
