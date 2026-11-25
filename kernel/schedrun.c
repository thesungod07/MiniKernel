#include <stdio.h>
#include "schedrun.h"
#include "progbuild.h"

static cpu_t sim_cpu;

static void build_job(prog_t *p, const job_t *j)
{
    pb_init(p);
    int total = j->burst;
    int body_end = total - 2;                       /* last two instructions are the exit */

    if (j->io_len > 0)
    {
        pb_nops(p, j->io_at);
        pb_sys(p, SYS_SLEEP, j->io_len, PB_NONE, PB_NONE);     /* LDI, LDI, SYSCALL = 3 instructions */
    }
    while (pb_pos(p) < body_end) { pb_emit(p, NOP()); }
    pb_exit(p);
}

void sched_compute_stats(sched_stats_t *out, uint64_t total_cycles)
{
    double w = 0, t = 0, r = 0;
    int n = 0;

    for (int i = 0; i < MAX_PROCS; i++)
    {
        const pcb_t *p = &proc_table[i];
        if (p->pid == 0 || p->state != PROC_TERMINATED) { continue; }
        double turnaround = (double)(p->finish_time - p->arrival_time);
        double waiting = turnaround - (double)p->burst_time - (double)sched_io_time(p);
        double response = (double)(p->first_run_time - (int64_t)p->arrival_time);
        w += waiting; t += turnaround; r += response; n++;
    }

    out->nproc = n;
    out->avg_wait = n ? w / n : 0;
    out->avg_turnaround = n ? t / n : 0;
    out->avg_response = n ? r / n : 0;
    out->makespan = total_cycles;
    out->cpu_util = total_cycles ? (double)sched_busy_cycles() / (double)total_cycles : 0;
    out->switches = sched_switches();
}

void sched_print_gantt(void)
{
    int len;
    const int *g = sched_gantt(&len);
    int start = 0;

    printf("Gantt: ");
    for (int i = 1; i <= len; i++)
    {
        if (i == len || g[i] != g[start])
        {
            if (g[start] == 0) { printf("[idle %d-%d] ", start, i); }
            else { printf("[P%d %d-%d] ", g[start], start, i); }
            start = i;
        }
    }
    printf("\n");
}

void sched_print_report(const char *name, uint64_t total_cycles)
{
    sched_stats_t st;
    sched_compute_stats(&st, total_cycles);

    printf("=== %s ===\n", name);
    sched_print_gantt();
    printf("  pid  arrive  burst  prio  finish  turnaround  waiting  response\n");
    for (int i = 0; i < MAX_PROCS; i++)
    {
        const pcb_t *p = &proc_table[i];
        if (p->pid == 0 || p->state != PROC_TERMINATED) { continue; }
        long turnaround = (long)(p->finish_time - p->arrival_time);
        long waiting = turnaround - (long)p->burst_time - (long)sched_io_time(p);
        long response = (long)p->first_run_time - (long)p->arrival_time;
        printf("  %3d  %6llu  %5llu  %4d  %6llu  %10ld  %7ld  %8ld\n", p->pid,
               (unsigned long long)p->arrival_time, (unsigned long long)p->burst_time, p->priority,
               (unsigned long long)p->finish_time, turnaround, waiting, response);
    }
    printf("  average waiting %.2f, turnaround %.2f, response %.2f, CPU utilisation %.1f%%, context switches %llu\n",
           st.avg_wait, st.avg_turnaround, st.avg_response, st.cpu_util * 100.0, (unsigned long long)st.switches);
}

int sched_run_workload(const scheduler_t *s, const job_t *jobs, int n, sched_stats_t *out, int verbose)
{
    cpu_init(&sim_cpu);
    proc_init();

    for (int i = 0; i < n; i++)
    {
        prog_t p;
        build_job(&p, &jobs[i]);
        int pid = pb_spawn(&sim_cpu, &p, jobs[i].arrival, jobs[i].priority);
        if (pid < 0) { return -1; }
        proc_table[pid - 1].burst_time = (uint64_t)jobs[i].burst;
        proc_table[pid - 1].remaining_time = (uint64_t)jobs[i].burst;
    }

    sched_init(&sim_cpu, s);
    sched_start(&sim_cpu);
    cpu_run(&sim_cpu, 500000);

    sched_compute_stats(out, sim_cpu.cycles);
    if (verbose) { sched_print_report(s->name, sim_cpu.cycles); }
    return 0;
}

void sched_compare(const scheduler_t *const *list, int k, const job_t *jobs, int n)
{
    printf("%-26s %8s %11s %9s %7s %9s\n", "algorithm", "avg wait", "avg turnar.", "avg resp.", "CPU %", "switches");
    for (int i = 0; i < k; i++)
    {
        sched_stats_t st;
        sched_run_workload(list[i], jobs, n, &st, 0);
        printf("%-26s %8.2f %11.2f %9.2f %6.1f%% %9llu\n", list[i]->name, st.avg_wait, st.avg_turnaround,
               st.avg_response, st.cpu_util * 100.0, (unsigned long long)st.switches);
    }
}
