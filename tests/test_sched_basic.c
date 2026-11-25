#include <stdio.h>
#include "../hw/isa.h"
#include "../kernel/sched.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static cpu_t cpu;
static void setup(void) { cpu_init(&cpu); proc_init(); }
static void add_proc(int arrival, int burst, int priority)
{
    uint32_t img[PROC_MEM_WORDS];
    for (int i = 0; i < burst - 2; i++) img[i] = NOP();
    img[burst - 2] = LDI(0, SYS_EXIT);
    img[burst - 1] = SYSCALL();
    int pid = proc_create(&cpu, img, burst, priority, burst);
    pcb_t *p = &proc_table[pid - 1];
    p->arrival_time = arrival;
    p->state = arrival > 0 ? PROC_NEW : PROC_READY;
}
static void run(const scheduler_t *s) { sched_init(&cpu, s); sched_start(&cpu); cpu_run(&cpu, 100000); }
static int fin(int i) { return (int)proc_table[i].finish_time; }
static int total_wait(int n)
{
    int t = 0;
    for (int i = 0; i < n; i++)
        t += (int)(proc_table[i].finish_time - proc_table[i].arrival_time - proc_table[i].burst_time);
    return t;
}
static void load_classic(void) { setup(); add_proc(0, 8, 1); add_proc(1, 4, 1); add_proc(2, 9, 1); add_proc(3, 5, 1); }

int main(void)
{
    printf("FCFS\n");
    load_classic(); run(&sched_fcfs);
    CHECK(fin(0) == 8 && fin(1) == 12 && fin(2) == 21 && fin(3) == 26, "finish 8, 12, 21, 26");

    printf("SJF\n");
    load_classic(); run(&sched_sjf);
    CHECK(fin(0) == 8 && fin(1) == 12 && fin(3) == 17 && fin(2) == 26, "finish: P1 8, P2 12, P4 17, P3 26");
    CHECK(total_wait(4) == 31, "total waiting 31");

    printf("SRTF\n");
    load_classic(); run(&sched_srtf);
    CHECK(fin(1) == 5 && fin(3) == 10 && fin(0) == 17 && fin(2) == 26, "finish: P2 5, P4 10, P1 17, P3 26");
    CHECK(total_wait(4) == 26, "total waiting 26");

    printf("Priority\n");
    setup();
    add_proc(0, 10, 3); add_proc(0, 3, 1); add_proc(0, 2, 4); add_proc(0, 2, 5); add_proc(0, 5, 2);
    run(&sched_prio);
    CHECK(fin(1) == 3 && fin(4) == 8 && fin(0) == 18 && fin(2) == 20 && fin(3) == 22, "order P2, P5, P1, P3, P4");
    setup(); add_proc(0, 6, 3); add_proc(2, 4, 1); add_proc(3, 2, 2);
    run(&sched_prio);
    CHECK(fin(0) == 6 && fin(1) == 10 && fin(2) == 12, "non-preemptive: 6, 10, 12");
    setup(); add_proc(0, 6, 3); add_proc(2, 4, 1); add_proc(3, 2, 2);
    run(&sched_prio_p);
    CHECK(fin(1) == 6 && fin(2) == 8 && fin(0) == 12, "preemptive: P2 6, P3 8, P1 12");

    printf("Round Robin\n");
    setup(); add_proc(0, 24, 1); add_proc(0, 3, 1); add_proc(0, 3, 1);
    run(&sched_rr);
    CHECK(fin(1) == 7 && fin(2) == 10 && fin(0) == 30, "q=4: P2 7, P3 10, P1 30");
    CHECK(total_wait(3) == 17, "total waiting 17");
    scheduler_t rr2 = sched_rr; rr2.quantum = 2;
    setup(); add_proc(0, 24, 1); add_proc(0, 3, 1); add_proc(0, 3, 1);
    run(&rr2);
    CHECK(fin(1) == 9 && fin(2) == 10 && fin(0) == 30, "q=2: P2 9, P3 10, P1 30");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
