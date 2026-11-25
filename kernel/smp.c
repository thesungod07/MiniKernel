#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "smp.h"

#define CFS_LATENCY 20
#define CFS_MIN_GRAN 2

/* Linux's nice -> weight table (nice -20 .. +19); nice 0 = 1024 */
static const unsigned prio_to_weight[40] = {
    88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
     9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
     1024,   820,   655,   526,   423,   335,   272,   215,   172,   137,
      110,    87,    70,    56,    45,    36,    29,    23,    18,    15
};

static smp_task_t tasks[SMP_MAX_TASKS];
static int ntasks;

unsigned smp_weight_of_nice(int nice)
{
    if (nice < -20) { nice = -20; }
    if (nice > 19) { nice = 19; }
    return prio_to_weight[nice + 20];
}

void smp_reset(void) { memset(tasks, 0, sizeof tasks); ntasks = 0; }

int smp_add_task(int arrival, int burst, int nice, int home_cpu, unsigned affinity)
{
    if (ntasks >= SMP_MAX_TASKS) { return -1; }
    smp_task_t *t = &tasks[ntasks];
    memset(t, 0, sizeof *t);
    t->arrival = arrival;
    t->burst = burst;
    t->nice = nice;
    t->home_cpu = home_cpu;
    t->affinity = affinity;
    return ntasks++;
}

const smp_task_t *smp_task(int i) { return (i >= 0 && i < ntasks) ? &tasks[i] : NULL; }

/* ---------- helpers ---------- */

static bool allowed(const smp_task_t *t, int k) { return t->affinity == 0 || ((t->affinity >> k) & 1u); }

static bool in_queue(const smp_config_t *cfg, const smp_task_t *t, int q)
{
    return cfg->percpu ? (t->cpu == q) : true;
}

static int load_of(const smp_config_t *cfg, int q)
{
    int n = 0;
    for (int i = 0; i < ntasks; i++)
        if ((tasks[i].state == 1 || tasks[i].state == 2) && in_queue(cfg, &tasks[i], q)) { n++; }
    return n;
}

static uint64_t min_vruntime(const smp_config_t *cfg, int q)
{
    bool any = false;
    uint64_t m = 0;
    for (int i = 0; i < ntasks; i++)
    {
        const smp_task_t *t = &tasks[i];
        if ((t->state == 1 || t->state == 2) && in_queue(cfg, t, q))
        {
            if (!any || t->vruntime < m) { m = t->vruntime; any = true; }
        }
    }
    return any ? m : 0;
}

static int least_loaded_for(const smp_config_t *cfg, const smp_task_t *t)
{
    int best = -1, best_load = 0;
    for (int k = 0; k < cfg->ncpus; k++)
    {
        if (!allowed(t, k)) { continue; }
        int l = load_of(cfg, k);
        if (best < 0 || l < best_load) { best = k; best_load = l; }
    }
    return best < 0 ? 0 : best;
}

/* move a task between run queues, keeping its vruntime fair in the new queue */
static void move_task(const smp_config_t *cfg, smp_task_t *t, int dst, uint64_t now)
{
    if (cfg->cfs)
    {
        uint64_t src_min = min_vruntime(cfg, t->cpu);
        uint64_t rel = t->vruntime >= src_min ? t->vruntime - src_min : 0;
        t->vruntime = min_vruntime(cfg, dst) + rel;
    }
    t->cpu = dst;
    t->enq = now;
}

/* ---------- the simulation ---------- */

void smp_run(const smp_config_t *cfg, smp_result_t *out)
{
    int running[SMP_MAX_CPUS];
    int slice_left[SMP_MAX_CPUS];
    int done = 0;
    uint64_t t = 0;
    uint64_t limit = cfg->max_ticks > 0 ? (uint64_t)cfg->max_ticks : 100000;

    memset(out, 0, sizeof *out);
    out->ntasks = ntasks;
    for (int k = 0; k < SMP_MAX_CPUS; k++) { running[k] = -1; slice_left[k] = 0; }
    for (int i = 0; i < ntasks; i++)
    {
        smp_task_t *x = &tasks[i];
        x->remaining = x->burst;
        x->state = 0;
        x->cpu = -1;
        x->last_cpu = -1;
        x->weight = smp_weight_of_nice(x->nice);
        x->vruntime = 0;
        x->enq = 0;
        x->first_run = -1;
        x->finish = -1;
        x->migrations = 0;
        memset(x->ticks_on, 0, sizeof x->ticks_on);
    }

    while (done < ntasks && t < limit)
    {
        /* 1. arrivals */
        for (int i = 0; i < ntasks; i++)
        {
            smp_task_t *x = &tasks[i];
            if (x->state != 0 || (uint64_t)x->arrival > t) { continue; }

            int q = -1;
            if (cfg->percpu)
            {
                q = (x->home_cpu >= 0 && x->home_cpu < cfg->ncpus && allowed(x, x->home_cpu))
                        ? x->home_cpu : least_loaded_for(cfg, x);
            }
            x->cpu = q;
            x->vruntime = cfg->cfs ? min_vruntime(cfg, q) : 0;
            x->state = 1;
            x->enq = t;
        }

        /* 2. push migration: move one waiting task from the busiest to the idlest queue */
        if (cfg->percpu && cfg->push_interval > 0 && t > 0 && t % (uint64_t)cfg->push_interval == 0)
        {
            int busiest = 0, idlest = 0;
            for (int k = 1; k < cfg->ncpus; k++)
            {
                if (load_of(cfg, k) > load_of(cfg, busiest)) { busiest = k; }
                if (load_of(cfg, k) < load_of(cfg, idlest)) { idlest = k; }
            }
            if (load_of(cfg, busiest) - load_of(cfg, idlest) >= 2)
            {
                int mv = -1;
                for (int i = 0; i < ntasks; i++)
                {
                    if (tasks[i].state == 1 && tasks[i].cpu == busiest && allowed(&tasks[i], idlest)
                        && (mv < 0 || tasks[i].enq > tasks[mv].enq)) { mv = i; }
                }
                if (mv >= 0) { move_task(cfg, &tasks[mv], idlest, t); }
            }
        }

        /* 3. every CPU: finish an expired slice, then pick something to run */
        for (int k = 0; k < cfg->ncpus; k++)
        {
            if (running[k] >= 0 && slice_left[k] == 0)
            {
                tasks[running[k]].state = 1;
                tasks[running[k]].enq = t;
                running[k] = -1;
            }
            if (running[k] >= 0) { continue; }

            int pick = -1;
            bool prefer = (!cfg->percpu && cfg->soft_affinity);

            for (int pass = 0; pass < 2 && pick < 0; pass++)
            {
                if (pass == 0 && !prefer) { continue; }      /* pass 0 only exists for soft affinity */
                for (int i = 0; i < ntasks; i++)
                {
                    const smp_task_t *x = &tasks[i];
                    if (x->state != 1 || !allowed(x, k) || !in_queue(cfg, x, k)) { continue; }
                    if (pass == 0 && !(x->last_cpu == k || x->last_cpu == -1)) { continue; }
                    if (pick < 0) { pick = i; continue; }
                    const smp_task_t *b = &tasks[pick];
                    if (cfg->cfs ? (x->vruntime < b->vruntime) : (x->enq < b->enq)) { pick = i; }
                }
            }

            if (pick < 0 && cfg->percpu && cfg->pull)        /* idle pull: steal from the busiest other queue */
            {
                int src = -1, src_ready = 0;
                for (int q = 0; q < cfg->ncpus; q++)
                {
                    if (q == k) { continue; }
                    int ready = 0;
                    for (int i = 0; i < ntasks; i++)
                        if (tasks[i].state == 1 && tasks[i].cpu == q && allowed(&tasks[i], k)) { ready++; }
                    if (ready > src_ready) { src_ready = ready; src = q; }
                }
                if (src >= 0)
                {
                    for (int i = 0; i < ntasks; i++)
                    {
                        if (tasks[i].state == 1 && tasks[i].cpu == src && allowed(&tasks[i], k)
                            && (pick < 0 || tasks[i].enq < tasks[pick].enq)) { pick = i; }
                    }
                    if (pick >= 0) { move_task(cfg, &tasks[pick], k, t); }
                }
            }

            if (pick < 0) { continue; }

            smp_task_t *x = &tasks[pick];
            if (x->last_cpu != -1 && x->last_cpu != k) { x->migrations++; out->migrations++; }
            x->last_cpu = k;
            x->state = 2;
            if (x->first_run < 0) { x->first_run = (int)t; }
            running[k] = pick;

            if (cfg->cfs)
            {
                uint64_t total = 0;
                for (int i = 0; i < ntasks; i++)
                    if ((tasks[i].state == 1 || tasks[i].state == 2) && in_queue(cfg, &tasks[i], x->cpu)) { total += tasks[i].weight; }
                int s = (int)((uint64_t)CFS_LATENCY * x->weight / (total ? total : 1));
                slice_left[k] = s < CFS_MIN_GRAN ? CFS_MIN_GRAN : s;
            }
            else { slice_left[k] = cfg->quantum > 0 ? cfg->quantum : 1; }
        }

        /* 4. run one tick on every busy CPU */
        for (int k = 0; k < cfg->ncpus; k++)
        {
            if (running[k] < 0) { continue; }
            smp_task_t *x = &tasks[running[k]];
            x->remaining--;
            x->ticks_on[k]++;
            out->busy[k]++;
            x->vruntime += (1024ull * 1000ull) / x->weight;
            slice_left[k]--;
            if (x->remaining == 0)
            {
                x->state = 3;
                x->finish = (int)t + 1;
                done++;
                running[k] = -1;
                slice_left[k] = 0;
            }
        }
        t++;
    }

    out->makespan = t;
    double sum = 0;
    int n = 0;
    for (int i = 0; i < ntasks; i++)
    {
        if (tasks[i].state == 3) { sum += tasks[i].finish - tasks[i].arrival; n++; }
    }
    out->avg_turnaround = n ? sum / n : 0;
}

void smp_print(const smp_config_t *cfg, const smp_result_t *r, const char *title)
{
    printf("=== %s ===\n", title);
    printf("  %d CPUs, %s queue(s), %s%s%s%s\n", cfg->ncpus, cfg->percpu ? "per-CPU" : "one global",
           cfg->cfs ? "CFS" : "round robin", cfg->push_interval ? ", push balancing" : "",
           cfg->pull ? ", pull balancing" : "", cfg->soft_affinity ? ", soft affinity" : "");
    printf("  makespan %llu, migrations %d, average turnaround %.1f\n",
           (unsigned long long)r->makespan, r->migrations, r->avg_turnaround);
    printf("  CPU busy time:");
    for (int k = 0; k < cfg->ncpus; k++) { printf(" cpu%d=%llu", k, (unsigned long long)r->busy[k]); }
    printf("\n  task  nice  burst  finish  ran-on(cpu0..)        migrations\n");
    for (int i = 0; i < ntasks; i++)
    {
        const smp_task_t *t = &tasks[i];
        printf("  %4d  %4d  %5d  %6d  ", i, t->nice, t->burst, t->finish);
        for (int k = 0; k < cfg->ncpus; k++) { printf("%4d", t->ticks_on[k]); }
        printf("          %d\n", t->migrations);
    }
}
