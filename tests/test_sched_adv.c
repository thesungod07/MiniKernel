#include <stdio.h>
#include "../kernel/schedrun.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static int fin(int i) { return (int)proc_table[i].finish_time; }

int main(void)
{
    sched_stats_t st;

    printf("MLQ: B1(0,p1,20) F1(5,p0,6) F2(6,p0,6)\n");
    {
        job_t jobs[] = { {0, 1, 20, 0, 0}, {5, 0, 6, 0, 0}, {6, 0, 6, 0, 0} };
        sched_run_workload(&sched_mlq, jobs, 3, &st, 0);
        CHECK(fin(1) == 15 && fin(2) == 17 && fin(0) == 32, "F1 15, F2 17, background B1 resumes and ends at 32");
    }

    printf("MLFQ: A(20) B(3) C(3) all at 0\n");
    {
        job_t jobs[] = { {0, 1, 20, 0, 0}, {0, 1, 3, 0, 0}, {0, 1, 3, 0, 0} };
        sched_run_workload(&sched_mlfq, jobs, 3, &st, 0);
        CHECK(fin(1) == 11 && fin(2) == 12 && fin(0) == 26, "short jobs B 11, C 12 finish early, long A 26");
        sched_stats_t fc;
        sched_run_workload(&sched_fcfs, jobs, 3, &fc, 0);
        CHECK(st.avg_response < fc.avg_response, "MLFQ response time beats FCFS");
    }

    printf("Starvation and aging: L(0,p6,10) vs a stream of 12 high priority jobs\n");
    {
        job_t jobs[13];
        jobs[0] = (job_t){0, 6, 10, 0, 0};
        for (int i = 0; i < 12; i++) jobs[1 + i] = (job_t){i * 5, 1, 5, 0, 0};
        sched_run_workload(&sched_prio_p, jobs, 13, &st, 0);
        int starved = fin(0);
        sched_run_workload(&sched_prio_aging, jobs, 13, &st, 0);
        int aged = fin(0);
        printf("        low priority job finishes at %d without aging, %d with aging\n", starved, aged);
        CHECK(starved == 70, "without aging the low priority job waits for the whole stream (70)");
        CHECK(aged < starved, "with aging it finishes earlier");
    }

    printf("Convoy effect: one long job followed by short jobs\n");
    {
        job_t jobs[] = { {0, 1, 40, 0, 0}, {1, 1, 4, 0, 0}, {2, 1, 4, 0, 0}, {3, 1, 4, 0, 0}, {4, 1, 4, 0, 0} };
        sched_stats_t a, b;
        sched_run_workload(&sched_fcfs, jobs, 5, &a, 0);
        sched_run_workload(&sched_srtf, jobs, 5, &b, 0);
        printf("        average waiting: FCFS %.1f, SRTF %.1f\n", a.avg_wait, b.avg_wait);
        CHECK(a.avg_wait > 30.0 && b.avg_wait < a.avg_wait / 2, "FCFS suffers the convoy effect, SRTF avoids it");
    }

    printf("Batch vs multiprogramming vs time-sharing (jobs that do I/O)\n");
    {
        job_t jobs[] = { {0, 1, 30, 5, 20}, {0, 1, 30, 5, 20}, {0, 1, 30, 5, 20} };
        sched_stats_t b, m, t;
        sched_run_workload(&sched_batch, jobs, 3, &b, 0);
        sched_run_workload(&sched_multiprog, jobs, 3, &m, 0);
        sched_run_workload(&sched_timeshare, jobs, 3, &t, 0);
        printf("        CPU utilisation: batch %.0f%%, multiprogramming %.0f%%, time-sharing %.0f%%\n",
               b.cpu_util * 100, m.cpu_util * 100, t.cpu_util * 100);
        printf("        makespan:        batch %llu, multiprogramming %llu, time-sharing %llu\n",
               (unsigned long long)b.makespan, (unsigned long long)m.makespan, (unsigned long long)t.makespan);
        CHECK(b.nproc == 3 && m.nproc == 3 && t.nproc == 3, "all jobs finish in every mode");
        CHECK(m.cpu_util > b.cpu_util, "multiprogramming keeps the CPU busier than batch");
        CHECK(m.makespan < b.makespan, "and finishes sooner");
        CHECK(b.cpu_util > 0 && t.cpu_util <= 1.0, "utilisation values are sane");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
