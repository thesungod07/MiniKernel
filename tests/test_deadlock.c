#include <stdio.h>
#include <string.h>
#include "../kernel/deadlock.h"
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
}

/* req(R_first); sleep; req(R_second); work; release both; exit(0) */
static void two_step(prog_t *p, int first, int second)
{
    pb_init(p);
    pb_sys(p, SYS_RES_REQUEST, first, 1, PB_NONE);
    pb_sys(p, SYS_SLEEP, 5, PB_NONE, PB_NONE);
    pb_sys(p, SYS_RES_REQUEST, second, 1, PB_NONE);
    pb_nops(p, 3);
    pb_sys(p, SYS_RES_RELEASE, second, 1, PB_NONE);
    pb_sys(p, SYS_RES_RELEASE, first, 1, PB_NONE);
    pb_exit_code(p, 0);
}

int main(void)
{
    printf("Banker's algorithm: the textbook example (5 processes, 3 resource types)\n");
    {
        dl_state_t s;
        memset(&s, 0, sizeof s);
        s.np = 5; s.nr = 3;
        int avail[3] = { 3, 3, 2 };
        int max[5][3] = { {7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3} };
        int alloc[5][3] = { {0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2} };
        for (int r = 0; r < 3; r++) s.available[r] = avail[r];
        for (int p = 0; p < 5; p++) for (int r = 0; r < 3; r++) { s.max[p][r] = max[p][r]; s.alloc[p][r] = alloc[p][r]; }

        int seq[5];
        CHECK(dl_is_safe(&s, seq), "the initial state is safe");
        CHECK(seq[0] == 1 && seq[1] == 3 && seq[2] == 4 && seq[3] == 0 && seq[4] == 2, "safe sequence <P1, P3, P4, P0, P2>");

        int r1[3] = { 1, 0, 2 };
        CHECK(dl_request(&s, 1, r1) == 0, "P1 requests (1,0,2): granted");
        int r4[3] = { 3, 3, 0 };
        CHECK(dl_request(&s, 4, r4) == -2, "P4 requests (3,3,0): not available, must wait");
        int r0[3] = { 0, 2, 0 };
        CHECK(dl_request(&s, 0, r0) == -3, "P0 requests (0,2,0): available, but the result is unsafe");
        int big[3] = { 9, 0, 0 };
        CHECK(dl_request(&s, 3, big) == -1, "a request above the declared maximum is refused");
    }

    printf("deadlock detection: the textbook example\n");
    {
        dl_state_t s;
        memset(&s, 0, sizeof s);
        s.np = 5; s.nr = 3;
        int alloc[5][3] = { {0,1,0}, {2,0,0}, {3,0,3}, {2,1,1}, {0,0,2} };
        int req[5][3]   = { {0,0,0}, {2,0,2}, {0,0,0}, {1,0,0}, {0,0,2} };
        for (int p = 0; p < 5; p++) for (int r = 0; r < 3; r++) { s.alloc[p][r] = alloc[p][r]; s.request[p][r] = req[p][r]; }

        int dead[5];
        CHECK(dl_detect(&s, dead) == 0, "no deadlock with available (0,0,0)");
        s.request[2][2] = 1;                                     /* P2 now also wants one C */
        int n = dl_detect(&s, dead);
        CHECK(n == 4 && dead[0] == 1 && dead[1] == 2 && dead[2] == 3 && dead[3] == 4, "after P2 asks for one more C: P1, P2, P3, P4 are deadlocked");

        int victims[5];
        int k = dl_recover_terminate(&s, victims);
        CHECK(k >= 1 && dl_detect(&s, dead) == 0, "recovery by termination breaks the deadlock");
    }

    printf("wait-for graph\n");
    {
        int adj[DL_MAXP][DL_MAXP] = { {0} }, cyc[DL_MAXP];
        adj[0][1] = adj[1][2] = adj[2][0] = 1;
        CHECK(dl_wfg_cycle(3, adj, cyc) == 3, "P0 -> P1 -> P2 -> P0 is a cycle of length 3");
        adj[2][0] = 0;
        CHECK(dl_wfg_cycle(3, adj, cyc) == 0, "remove one edge and there is no cycle");
    }

    printf("prevention helpers\n");
    {
        CHECK(dl_ordered_ok(1, 2) && !dl_ordered_ok(2, 1), "requests must go up the resource order");
        dl_state_t s;
        memset(&s, 0, sizeof s);
        s.np = 1; s.nr = 2; s.available[0] = 1; s.available[1] = 0;
        int both[2] = { 1, 1 };
        CHECK(dl_request_all_or_none(&s, 0, both) == -2 && s.alloc[0][0] == 0, "all-or-none: nothing is taken if one part is missing");
    }

    printf("kernel: two processes lock two resources in opposite order, no protection, monitor on\n");
    {
        scheduler_t rr = sched_rr;
        setup(&rr);
        int tot[2] = { 1, 1 };
        rm_init(RM_NONE, 2, tot);
        rm_enable_monitor(10);
        prog_t a, b;
        two_step(&a, 0, 1);
        two_step(&b, 1, 0);
        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        sched_start(&cpu);
        cpu_run(&cpu, 5000);
        CHECK(rm_deadlocks_found() >= 1, "the monitor detected the deadlock");
        CHECK(proc_table[1].exit_code == -1, "it terminated the victim (P2)");
        CHECK(proc_table[0].state == PROC_TERMINATED && proc_table[0].exit_code == 0, "the other process then ran to completion");
    }

    printf("kernel: same programs, Banker's algorithm avoids the deadlock\n");
    {
        scheduler_t rr = sched_rr;
        setup(&rr);
        int tot[2] = { 1, 1 };
        rm_init(RM_AVOID, 2, tot);
        rm_enable_monitor(10);
        prog_t a, b;
        two_step(&a, 0, 1);
        two_step(&b, 1, 0);
        int p1 = pb_spawn(&cpu, &a, 0, 1);
        int p2 = pb_spawn(&cpu, &b, 0, 1);
        rm_set_max(p1, 0, 1); rm_set_max(p1, 1, 1);
        rm_set_max(p2, 0, 1); rm_set_max(p2, 1, 1);
        sched_start(&cpu);
        cpu_run(&cpu, 5000);
        CHECK(rm_deadlocks_found() == 0, "no deadlock ever formed");
        CHECK(proc_table[0].exit_code == 0 && proc_table[1].exit_code == 0, "both processes finished normally");
    }

    printf("kernel: resource ordering rejects the out-of-order request\n");
    {
        scheduler_t rr = sched_rr;
        setup(&rr);
        int tot[2] = { 1, 1 };
        rm_init(RM_ORDERED, 2, tot);
        rm_enable_monitor(10);
        prog_t a, b;
        two_step(&a, 0, 1);
        pb_init(&b);
        pb_sys(&b, SYS_RES_REQUEST, 1, 1, PB_NONE);
        pb_sys(&b, SYS_SLEEP, 5, PB_NONE, PB_NONE);
        pb_sys(&b, SYS_RES_REQUEST, 0, 1, PB_NONE);        /* lower than the R1 it holds: refused */
        pb_exit_r0(&b);                                    /* exit code = the result of that request */
        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        sched_start(&cpu);
        cpu_run(&cpu, 5000);
        CHECK(proc_table[1].exit_code == -1, "P2's request for R0 while holding R1 returned -1");
        CHECK(rm_deadlocks_found() == 0 && proc_table[0].exit_code == 0, "no deadlock, P1 finished");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
