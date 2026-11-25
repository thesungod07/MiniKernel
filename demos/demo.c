#include <stdio.h>
#include "../kernel/schedrun.h"
#include "../kernel/smp.h"
#include "../kernel/ipc.h"
#include "../kernel/sync.h"
#include "../kernel/deadlock.h"
#include "../kernel/progbuild.h"
#include "../kernel/threads.h"

static void heading(const char *t) { printf("\n\n################ %s ################\n", t); }

static void ut_body(void *arg)
{
    int id = *(int *)arg;
    for (int i = 0; i < 3; i++) { printf("  user-level thread %d, step %d\n", id, i); ut_yield(); }
}

static void race_demo(void)
{
    static cpu_t cpu;
    for (int guarded = 0; guarded < 2; guarded++)
    {
        cpu_init(&cpu);
        proc_init();
        scheduler_t rr = sched_rr;
        rr.quantum = 11;
        sched_init(&cpu, &rr);
        ipc_init();
        sync_init();
        int seg = shm_create(1);
        int sem = ksem_create(1);

        for (int k = 0; k < 2; k++)
        {
            prog_t p;
            pb_init(&p);
            pb_emit(&p, LDI(5, 1)); pb_emit(&p, LDI(6, 40)); pb_emit(&p, LDI(4, 0));
            int loop = pb_pos(&p);
            if (guarded) pb_sys(&p, SYS_SEM_WAIT, sem, PB_NONE, PB_NONE);
            pb_sys(&p, SYS_SHM_READ, seg, 0, PB_NONE);
            pb_emit(&p, ADD(0, 5));
            pb_emit(&p, MOV(3, 0));
            pb_sys(&p, SYS_SHM_WRITE, seg, 0, PB_NONE);
            if (guarded) pb_sys(&p, SYS_SEM_SIGNAL, sem, PB_NONE, PB_NONE);
            pb_emit(&p, SUB(6, 5)); pb_emit(&p, CMP(6, 4)); pb_emit(&p, JNZ(loop));
            pb_exit(&p);
            pb_spawn(&cpu, &p, 0, 1);
        }
        sched_start(&cpu);
        cpu_run(&cpu, 300000);
        printf("  %s: final counter = %u (80 expected)\n", guarded ? "with semaphore   " : "without semaphore", shm_peek(seg, 0));
    }
}

static void deadlock_demo(void)
{
    static cpu_t cpu;
    cpu_init(&cpu);
    proc_init();
    scheduler_t rr = sched_rr;
    sched_init(&cpu, &rr);
    int tot[2] = { 1, 1 };
    rm_init(RM_NONE, 2, tot);
    rm_enable_monitor(10);

    for (int k = 0; k < 2; k++)
    {
        int first = k, second = 1 - k;
        prog_t p;
        pb_init(&p);
        pb_sys(&p, SYS_RES_REQUEST, first, 1, PB_NONE);
        pb_sys(&p, SYS_SLEEP, 5, PB_NONE, PB_NONE);
        pb_sys(&p, SYS_RES_REQUEST, second, 1, PB_NONE);
        pb_nops(&p, 3);
        pb_sys(&p, SYS_RES_RELEASE, second, 1, PB_NONE);
        pb_sys(&p, SYS_RES_RELEASE, first, 1, PB_NONE);
        pb_exit_code(&p, 0);
        pb_spawn(&cpu, &p, 0, 1);
    }
    sched_start(&cpu);
    cpu_run(&cpu, 5000);
    printf("  P1 exit code %d, P2 exit code %d (-1 = terminated by the deadlock recovery)\n",
           proc_table[0].exit_code, proc_table[1].exit_code);
}

int main(void)
{
    sched_stats_t st;

    heading("1. CPU scheduling: Gantt charts and metrics (textbook workload)");
    job_t classic[] = { {0, 1, 8, 0, 0}, {1, 1, 4, 0, 0}, {2, 1, 9, 0, 0}, {3, 1, 5, 0, 0} };
    sched_run_workload(&sched_fcfs, classic, 4, &st, 1);
    sched_run_workload(&sched_sjf, classic, 4, &st, 1);
    sched_run_workload(&sched_srtf, classic, 4, &st, 1);
    sched_run_workload(&sched_rr, classic, 4, &st, 1);

    heading("2. Comparing every algorithm on one workload");
    job_t mixed[] = { {0, 2, 12, 0, 0}, {1, 1, 4, 0, 0}, {2, 3, 9, 0, 0}, {3, 1, 5, 0, 0}, {6, 2, 7, 0, 0} };
    const scheduler_t *all[] = { &sched_fcfs, &sched_sjf, &sched_srtf, &sched_prio, &sched_prio_p, &sched_rr, &sched_mlfq };
    sched_compare(all, 7, mixed, 5);

    heading("3. Convoy effect (one long job, then short ones)");
    job_t convoy[] = { {0, 1, 40, 0, 0}, {1, 1, 4, 0, 0}, {2, 1, 4, 0, 0}, {3, 1, 4, 0, 0}, {4, 1, 4, 0, 0} };
    const scheduler_t *cv[] = { &sched_fcfs, &sched_srtf, &sched_rr };
    sched_compare(cv, 3, convoy, 5);

    heading("4. Starvation and aging");
    job_t stream[13];
    stream[0] = (job_t){0, 6, 10, 0, 0};
    for (int i = 0; i < 12; i++) stream[1 + i] = (job_t){i * 5, 1, 5, 0, 0};
    sched_run_workload(&sched_prio_p, stream, 13, &st, 0);
    printf("  without aging the low priority job finishes at %d\n", (int)proc_table[0].finish_time);
    sched_run_workload(&sched_prio_aging, stream, 13, &st, 0);
    printf("  with aging it finishes at %d\n", (int)proc_table[0].finish_time);

    heading("5. MLQ and MLFQ");
    job_t mlq[] = { {0, 1, 20, 0, 0}, {5, 0, 6, 0, 0}, {6, 0, 6, 0, 0} };
    sched_run_workload(&sched_mlq, mlq, 3, &st, 1);
    job_t mlfq[] = { {0, 1, 20, 0, 0}, {0, 1, 3, 0, 0}, {0, 1, 3, 0, 0} };
    sched_run_workload(&sched_mlfq, mlfq, 3, &st, 1);

    heading("6. Batch vs multiprogramming vs time-sharing (jobs with I/O)");
    job_t io[] = { {0, 1, 30, 5, 20}, {0, 1, 30, 5, 20}, {0, 1, 30, 5, 20} };
    const scheduler_t *modes[] = { &sched_batch, &sched_multiprog, &sched_timeshare };
    sched_compare(modes, 3, io, 3);

    heading("7. Multi-CPU scheduling");
    smp_result_t r;
    smp_reset();
    for (int i = 0; i < 8; i++) smp_add_task(0, 20, 0, 0, 0);
    smp_config_t none = { 4, 1, 0, 4, 0, 0, 0, 0 };
    smp_config_t pull = { 4, 1, 0, 4, 0, 1, 0, 0 };
    smp_config_t push = { 4, 1, 0, 4, 5, 0, 0, 0 };
    smp_run(&none, &r); smp_print(&none, &r, "per-CPU queues, no balancing");
    smp_run(&pull, &r); smp_print(&pull, &r, "per-CPU queues, pull migration");
    smp_run(&push, &r); smp_print(&push, &r, "per-CPU queues, push migration");

    heading("8. CFS: share follows weight (nice 0 vs nice -5, one CPU, 400 ticks)");
    smp_reset();
    smp_add_task(0, 1000, 0, -1, 0);
    smp_add_task(0, 1000, -5, -1, 0);
    smp_config_t cfs = { 1, 0, 1, 0, 0, 0, 0, 400 };
    smp_run(&cfs, &r);
    smp_print(&cfs, &r, "CFS");

    heading("9. Threads");
    int ids[3] = { 0, 1, 2 };
    ut_reset();
    for (int i = 0; i < 3; i++) ut_create(ut_body, &ids[i]);
    ut_run();
    ut_reset();
    printf("  kernel threads, shared counter: with mutex %ld, without %ld (80000 expected)\n",
           kt_counter_demo(4, 20000, 1), kt_counter_demo(4, 20000, 0));

    heading("10. Race condition and semaphore (simulated processes)");
    race_demo();

    heading("11. Deadlock: detection and recovery");
    deadlock_demo();

    printf("\n");
    return 0;
}
