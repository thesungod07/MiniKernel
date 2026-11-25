#include <stdio.h>
#include "../kernel/smp.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static void eight_tasks_on_cpu0(void)
{
    smp_reset();
    for (int i = 0; i < 8; i++) smp_add_task(0, 20, 0, 0, 0);
}

int main(void)
{
    smp_result_t r;

    printf("one global queue shared by 4 CPUs\n");
    {
        smp_config_t cfg = { 4, 0, 0, 4, 0, 0, 0, 0 };
        eight_tasks_on_cpu0();
        smp_run(&cfg, &r);
        CHECK(r.makespan == 40, "8 tasks x 20 ticks on 4 CPUs finish in 40 ticks");
        CHECK(r.busy[0] == 40 && r.busy[1] == 40 && r.busy[2] == 40 && r.busy[3] == 40, "all four CPUs were busy the whole time");
    }

    printf("per-CPU queues, every task starts on CPU 0\n");
    {
        smp_config_t none = { 4, 1, 0, 4, 0, 0, 0, 0 };
        eight_tasks_on_cpu0();
        smp_run(&none, &r);
        uint64_t m_none = r.makespan;
        CHECK(m_none == 160 && r.busy[1] == 0 && r.busy[2] == 0 && r.busy[3] == 0, "no balancing: CPU 0 does everything (160 ticks), the others idle");

        smp_config_t pull = { 4, 1, 0, 4, 0, 1, 0, 0 };
        eight_tasks_on_cpu0();
        smp_run(&pull, &r);
        printf("        pull balancing: makespan %llu, migrations %d\n", (unsigned long long)r.makespan, r.migrations);
        CHECK(r.makespan < m_none / 2 && r.migrations > 0 && r.busy[1] > 0, "pull migration: idle CPUs steal work");

        smp_config_t push = { 4, 1, 0, 4, 5, 0, 0, 0 };
        eight_tasks_on_cpu0();
        smp_run(&push, &r);
        printf("        push balancing: makespan %llu, migrations %d\n", (unsigned long long)r.makespan, r.migrations);
        CHECK(r.makespan < m_none && r.migrations > 0, "push migration also spreads the load");
    }

    printf("hard affinity: task 0 may only run on CPU 2\n");
    {
        smp_config_t cfg = { 4, 0, 0, 4, 0, 0, 0, 0 };
        smp_reset();
        smp_add_task(0, 30, 0, -1, 1u << 2);
        for (int i = 0; i < 5; i++) smp_add_task(0, 30, 0, -1, 0);
        smp_run(&cfg, &r);
        const smp_task_t *t = smp_task(0);
        CHECK(t->ticks_on[0] == 0 && t->ticks_on[1] == 0 && t->ticks_on[3] == 0 && t->ticks_on[2] == 30, "it only ever ran on CPU 2");
        CHECK(t->migrations == 0, "and never migrated");
    }

    printf("soft affinity: fewer migrations in a global queue\n");
    {
        smp_config_t off = { 4, 0, 0, 3, 0, 0, 0, 0 };
        smp_config_t on  = { 4, 0, 0, 3, 0, 0, 1, 0 };
        smp_reset();
        for (int i = 0; i < 6; i++) smp_add_task(0, 30, 0, -1, 0);
        smp_run(&off, &r);
        int m_off = r.migrations;
        smp_run(&on, &r);
        int m_on = r.migrations;
        printf("        migrations without soft affinity %d, with %d\n", m_off, m_on);
        CHECK(m_on < m_off, "preferring the last CPU reduces migrations");
        CHECK(r.makespan == 45, "and the work still finishes in 45 ticks (6 x 30 / 4)");
    }

    printf("CFS: CPU share follows the weight\n");
    {
        smp_config_t cfg = { 1, 0, 1, 0, 0, 0, 0, 400 };
        smp_reset();
        smp_add_task(0, 1000, 0, -1, 0);
        smp_add_task(0, 1000, 0, -1, 0);
        smp_run(&cfg, &r);
        double eq = (double)smp_task(0)->ticks_on[0] / smp_task(1)->ticks_on[0];
        printf("        equal nice: %d vs %d ticks\n", smp_task(0)->ticks_on[0], smp_task(1)->ticks_on[0]);
        CHECK(eq > 0.9 && eq < 1.1, "two nice-0 tasks share the CPU equally");

        smp_reset();
        smp_add_task(0, 1000, 0, -1, 0);
        smp_add_task(0, 1000, -5, -1, 0);
        smp_run(&cfg, &r);
        double ratio = (double)smp_task(1)->ticks_on[0] / smp_task(0)->ticks_on[0];
        printf("        nice 0 vs nice -5: %d vs %d ticks (ratio %.2f, weights give %.2f)\n",
               smp_task(0)->ticks_on[0], smp_task(1)->ticks_on[0], ratio, 3121.0 / 1024.0);
        CHECK(ratio > 2.7 && ratio < 3.4, "nice -5 gets about 3.05 times the CPU of nice 0");
    }

    printf("CFS with per-CPU queues and balancing\n");
    {
        smp_config_t cfg = { 4, 1, 1, 0, 5, 1, 0, 0 };
        eight_tasks_on_cpu0();
        smp_run(&cfg, &r);
        CHECK(r.makespan <= 48, "balanced CFS finishes close to the ideal 40 ticks");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
