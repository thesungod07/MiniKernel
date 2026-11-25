#include <stdio.h>
#include "../kernel/ipc.h"
#include "../kernel/sync.h"
#include "../kernel/progbuild.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static cpu_t cpu;

static void setup(const scheduler_t *s)
{
    cpu_init(&cpu);
    proc_init();
    sched_init(&cpu, s);
    ipc_init();
    sync_init();
}
static void go(void) { sched_start(&cpu); cpu_run(&cpu, 300000); }
static uint32_t mem(int proc_index, int addr) { return cpu.ram[proc_table[proc_index].base + addr]; }

/* increments shared word 0 n times: read, add one, write back */
static void counter_prog(prog_t *p, int seg, int n, int sem)
{
    pb_init(p);
    pb_emit(p, LDI(5, 1)); pb_emit(p, LDI(6, n)); pb_emit(p, LDI(4, 0));
    int loop = pb_pos(p);
    if (sem >= 0) pb_sys(p, SYS_SEM_WAIT, sem, PB_NONE, PB_NONE);
    pb_sys(p, SYS_SHM_READ, seg, 0, PB_NONE);
    pb_emit(p, ADD(0, 5));
    pb_emit(p, MOV(3, 0));
    pb_sys(p, SYS_SHM_WRITE, seg, 0, PB_NONE);                 /* value is in R3 */
    if (sem >= 0) pb_sys(p, SYS_SEM_SIGNAL, sem, PB_NONE, PB_NONE);
    pb_emit(p, SUB(6, 5));
    pb_emit(p, CMP(6, 4));
    pb_emit(p, JNZ(loop));
    pb_exit(p);
}

int main(void)
{
    scheduler_t rr = sched_rr;
    rr.quantum = 11;   /* whether a race shows up depends on where the preemptions fall */

    printf("race condition: two processes each add 1 to a shared counter 40 times\n");
    {
        setup(&rr);
        int seg = shm_create(1);
        prog_t a, b;
        counter_prog(&a, seg, 40, -1);
        counter_prog(&b, seg, 40, -1);
        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        go();
        printf("        final counter = %u (expected 80 if there were no race)\n", shm_peek(seg, 0));
        CHECK(shm_peek(seg, 0) < 80, "updates were lost: the read-add-write sequence is not atomic");
    }

    printf("the same program guarded by a binary semaphore\n");
    {
        setup(&rr);
        int seg = shm_create(1);
        int sem = ksem_create(1);
        prog_t a, b;
        counter_prog(&a, seg, 40, sem);
        counter_prog(&b, seg, 40, sem);
        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        go();
        CHECK(shm_peek(seg, 0) == 80, "no updates lost: final counter is exactly 80");
        CHECK(proc_table[0].state == PROC_TERMINATED && proc_table[1].state == PROC_TERMINATED, "both finished");
    }

    printf("semaphore as a signal: P1 must wait until P2 has written the data\n");
    {
        setup(&rr);
        int seg = shm_create(1);
        int sem = ksem_create(0);
        prog_t a, b;

        pb_init(&a);
        pb_sys(&a, SYS_SEM_WAIT, sem, PB_NONE, PB_NONE);
        pb_sys(&a, SYS_SHM_READ, seg, 0, PB_NONE);
        pb_emit(&a, ST(0, 100));
        pb_exit(&a);

        pb_init(&b);
        pb_nops(&b, 30);
        pb_sys(&b, SYS_SHM_WRITE, seg, 0, 777);
        pb_sys(&b, SYS_SEM_SIGNAL, sem, PB_NONE, PB_NONE);
        pb_exit(&b);

        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        go();
        CHECK(mem(0, 100) == 777, "P1 slept until the signal and then read 777");
    }

    printf("priority inversion: L(p3) holds a mutex, M(p2) hogs the CPU, H(p1) needs the mutex\n");
    {
        int finish_h[2], finish_m[2], finish_l[2];
        for (int inherit = 0; inherit < 2; inherit++)
        {
            setup(&sched_prio_p);
            sync_set_inheritance(inherit);
            int m = kmutex_create();
            prog_t l, mid, h;

            pb_init(&l);
            pb_sys(&l, SYS_MUTEX_LOCK, m, PB_NONE, PB_NONE);
            pb_nops(&l, 20);
            pb_sys(&l, SYS_MUTEX_UNLOCK, m, PB_NONE, PB_NONE);
            pb_exit(&l);

            pb_init(&mid);
            pb_nops(&mid, 30);
            pb_exit(&mid);

            pb_init(&h);
            pb_nops(&h, 2);
            pb_sys(&h, SYS_MUTEX_LOCK, m, PB_NONE, PB_NONE);
            pb_nops(&h, 3);
            pb_sys(&h, SYS_MUTEX_UNLOCK, m, PB_NONE, PB_NONE);
            pb_exit(&h);

            pb_spawn(&cpu, &l, 0, 3);
            pb_spawn(&cpu, &mid, 5, 2);
            pb_spawn(&cpu, &h, 10, 1);
            go();
            finish_l[inherit] = (int)proc_table[0].finish_time;
            finish_m[inherit] = (int)proc_table[1].finish_time;
            finish_h[inherit] = (int)proc_table[2].finish_time;
            printf("        inheritance %s: H finishes at %d, M at %d, L at %d\n",
                   inherit ? "ON " : "OFF", finish_h[inherit], finish_m[inherit], finish_l[inherit]);
        }
        CHECK(finish_m[0] < finish_h[0], "without inheritance the medium job finishes BEFORE the high priority job (inversion)");
        CHECK(finish_h[1] < finish_m[1], "with inheritance the high priority job finishes first");
        CHECK(finish_h[1] < finish_h[0], "and H finishes earlier than without inheritance");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
