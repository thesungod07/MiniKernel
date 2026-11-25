#include <stdio.h>
#include <string.h>
#include "deadlock.h"

/* ================= algorithms ================= */

static int need(const dl_state_t *s, int p, int r)
{
    int n = s->max[p][r] - s->alloc[p][r];
    return n < 0 ? 0 : n;
}

bool dl_is_safe(const dl_state_t *s, int seq[])
{
    int work[DL_MAXR];
    bool finish[DL_MAXP] = { false };
    int count = 0;

    for (int r = 0; r < s->nr; r++) { work[r] = s->available[r]; }

    bool progress = true;
    while (count < s->np && progress)
    {
        progress = false;
        for (int i = 0; i < s->np; i++)
        {
            if (finish[i]) { continue; }

            bool ok = true;
            for (int r = 0; r < s->nr; r++) { if (need(s, i, r) > work[r]) { ok = false; break; } }
            if (!ok) { continue; }

            for (int r = 0; r < s->nr; r++) { work[r] += s->alloc[i][r]; }
            finish[i] = true;
            if (seq != NULL) { seq[count] = i; }
            count++;
            progress = true;
        }
    }
    return count == s->np;
}

int dl_request(dl_state_t *s, int p, const int req[])
{
    for (int r = 0; r < s->nr; r++) { if (req[r] > need(s, p, r)) { return -1; } }
    for (int r = 0; r < s->nr; r++) { if (req[r] > s->available[r]) { return -2; } }

    dl_state_t trial = *s;                       /* pretend to grant it */
    for (int r = 0; r < s->nr; r++)
    {
        trial.available[r] -= req[r];
        trial.alloc[p][r] += req[r];
    }
    if (!dl_is_safe(&trial, NULL)) { return -3; }

    *s = trial;
    return 0;
}

int dl_detect(const dl_state_t *s, int deadlocked[])
{
    int work[DL_MAXR];
    bool finish[DL_MAXP];

    for (int r = 0; r < s->nr; r++) { work[r] = s->available[r]; }
    for (int i = 0; i < s->np; i++)
    {
        bool holds = false;
        for (int r = 0; r < s->nr; r++) { if (s->alloc[i][r] > 0) { holds = true; } }
        finish[i] = !holds;                      /* a process holding nothing cannot be in a deadlock */
    }

    bool progress = true;
    while (progress)
    {
        progress = false;
        for (int i = 0; i < s->np; i++)
        {
            if (finish[i]) { continue; }
            bool ok = true;
            for (int r = 0; r < s->nr; r++) { if (s->request[i][r] > work[r]) { ok = false; break; } }
            if (!ok) { continue; }
            for (int r = 0; r < s->nr; r++) { work[r] += s->alloc[i][r]; }
            finish[i] = true;
            progress = true;
        }
    }

    int n = 0;
    for (int i = 0; i < s->np; i++) { if (!finish[i]) { deadlocked[n++] = i; } }
    return n;
}

void dl_build_wfg(const dl_state_t *s, int adj[DL_MAXP][DL_MAXP])
{
    memset(adj, 0, sizeof(int) * DL_MAXP * DL_MAXP);
    for (int i = 0; i < s->np; i++)
        for (int r = 0; r < s->nr; r++)
            if (s->request[i][r] > 0)
                for (int j = 0; j < s->np; j++)
                    if (j != i && s->alloc[j][r] > 0) { adj[i][j] = 1; }
}

static int cycle_dfs(int u, int n, const int adj[DL_MAXP][DL_MAXP], int color[], int stack[], int *sp, int cycle[])
{
    color[u] = 1;
    stack[(*sp)++] = u;

    for (int v = 0; v < n; v++)
    {
        if (!adj[u][v]) { continue; }
        if (color[v] == 0)
        {
            int len = cycle_dfs(v, n, adj, color, stack, sp, cycle);
            if (len) { return len; }
        }
        else if (color[v] == 1)                  /* back edge: a cycle, read it off the stack */
        {
            int k = *sp - 1;
            while (stack[k] != v) { k--; }
            int len = 0;
            for (; k < *sp; k++) { cycle[len++] = stack[k]; }
            return len;
        }
    }

    color[u] = 2;
    (*sp)--;
    return 0;
}

int dl_wfg_cycle(int n, const int adj[DL_MAXP][DL_MAXP], int cycle[])
{
    int color[DL_MAXP] = { 0 };
    int stack[DL_MAXP + 1];
    int sp = 0;

    for (int u = 0; u < n; u++)
    {
        if (color[u] == 0)
        {
            int len = cycle_dfs(u, n, adj, color, stack, &sp, cycle);
            if (len) { return len; }
        }
    }
    return 0;
}

int dl_recover_terminate(dl_state_t *s, int victims[])
{
    int count = 0;
    int dead[DL_MAXP];

    while (dl_detect(s, dead) > 0)
    {
        int n = dl_detect(s, dead);
        int victim = dead[0], best_cost = -1;
        for (int k = 0; k < n; k++)
        {
            int cost = 0;
            for (int r = 0; r < s->nr; r++) { cost += s->alloc[dead[k]][r]; }
            if (best_cost < 0 || cost <= best_cost) { best_cost = cost; victim = dead[k]; }
        }
        for (int r = 0; r < s->nr; r++)
        {
            s->available[r] += s->alloc[victim][r];
            s->alloc[victim][r] = 0;
            s->request[victim][r] = 0;
        }
        victims[count++] = victim;
    }
    return count;
}

bool dl_ordered_ok(int highest_held, int requested) { return requested >= highest_held; }

int dl_request_all_or_none(dl_state_t *s, int p, const int req[])
{
    for (int r = 0; r < s->nr; r++) { if (req[r] > s->available[r]) { return -2; } }
    for (int r = 0; r < s->nr; r++)
    {
        s->available[r] -= req[r];
        s->alloc[p][r] += req[r];
    }
    return 0;
}

/* ================= kernel resource manager ================= */

static dl_state_t rm;
static int rm_mode;
static int found;
static char rm_chan;                       /* waiters sleep on this address */

static int slot_of(const pcb_t *p) { return (int)(p - proc_table); }

static void block_and_retry(cpu_t *c)
{
    c->saved_pc -= 1;
    sched_block_on(c, &rm_chan);
}

static void sys_res_request(cpu_t *c)
{
    int rid = (int)c->reg[1], count = (int)c->reg[2];
    int s = slot_of(current_proc);

    if (s >= DL_MAXP || rid < 0 || rid >= rm.nr || count <= 0) { c->reg[0] = (uint32_t)-1; return; }

    for (int r = 0; r < rm.nr; r++) { rm.request[s][r] = 0; }     /* forget an earlier wait */

    if (rm_mode == RM_ORDERED)
    {
        int highest = -1;
        for (int r = 0; r < rm.nr; r++) { if (rm.alloc[s][r] > 0) { highest = r; } }
        if (!dl_ordered_ok(highest, rid)) { c->reg[0] = (uint32_t)-1; return; }
    }

    int verdict;
    if (rm_mode == RM_AVOID)
    {
        int req[DL_MAXR] = { 0 };
        req[rid] = count;
        verdict = dl_request(&rm, s, req);
    }
    else if (count <= rm.available[rid])
    {
        rm.available[rid] -= count;
        rm.alloc[s][rid] += count;
        verdict = 0;
    }
    else { verdict = -2; }

    if (verdict == 0) { c->reg[0] = 0; return; }
    if (verdict == -1) { c->reg[0] = (uint32_t)-1; return; }      /* more than the declared claim */

    rm.request[s][rid] = count;                                   /* must wait: register what we want */
    block_and_retry(c);
}

static void sys_res_release(cpu_t *c)
{
    int rid = (int)c->reg[1], count = (int)c->reg[2];
    int s = slot_of(current_proc);

    if (s >= DL_MAXP || rid < 0 || rid >= rm.nr || count <= 0 || count > rm.alloc[s][rid])
    {
        c->reg[0] = (uint32_t)-1;
        return;
    }
    rm.alloc[s][rid] -= count;
    rm.available[rid] += count;
    c->reg[0] = 0;
    sched_wake_all(&rm_chan);
}

/* when a process ends (normally or killed) everything it holds goes back */
static void rm_on_exit(pcb_t *p)
{
    int s = slot_of(p);
    if (s >= DL_MAXP) { return; }
    for (int r = 0; r < rm.nr; r++)
    {
        rm.available[r] += rm.alloc[s][r];
        rm.alloc[s][r] = 0;
        rm.request[s][r] = 0;
    }
    sched_wake_all(&rm_chan);
}

void rm_init(int mode, int nres, const int totals[])
{
    memset(&rm, 0, sizeof rm);
    rm_mode = mode;
    found = 0;
    rm.np = DL_MAXP;
    rm.nr = nres;
    for (int r = 0; r < nres; r++) { rm.available[r] = totals[r]; }

    syscall_register(SYS_RES_REQUEST, sys_res_request);
    syscall_register(SYS_RES_RELEASE, sys_res_release);
    sched_set_exit_hook(rm_on_exit);
    sched_set_monitor(NULL, 0);
}

void rm_set_max(int pid, int rid, int n)
{
    pcb_t *p = sched_find_pid(pid);
    if (p != NULL && slot_of(p) < DL_MAXP && rid >= 0 && rid < rm.nr) { rm.max[slot_of(p)][rid] = n; }
}

int rm_detect(int pids[])
{
    int dead[DL_MAXP];
    int n = dl_detect(&rm, dead);
    for (int k = 0; k < n; k++) { pids[k] = proc_table[dead[k]].pid; }
    return n;
}

int rm_deadlocks_found(void) { return found; }

/* the periodic monitor: detect, explain, recover */
static void rm_monitor(cpu_t *c)
{
    int dead[DL_MAXP];

    for (;;)
    {
        int n = dl_detect(&rm, dead);
        if (n == 0) { break; }

        found++;
        printf("[deadlock] cycle %llu: deadlocked processes:", (unsigned long long)c->cycles);
        for (int k = 0; k < n; k++) { printf(" P%d", proc_table[dead[k]].pid); }
        printf("\n");

        int adj[DL_MAXP][DL_MAXP], cyc[DL_MAXP];
        dl_build_wfg(&rm, adj);
        int len = dl_wfg_cycle(rm.np, adj, cyc);
        if (len > 0)
        {
            printf("[deadlock] wait-for cycle:");
            for (int k = 0; k < len; k++) { printf(" P%d ->", proc_table[cyc[k]].pid); }
            printf(" P%d\n", proc_table[cyc[0]].pid);
        }

        int victim = dead[0], best = -1;                          /* cheapest victim: fewest units held */
        for (int k = 0; k < n; k++)
        {
            int cost = 0;
            for (int r = 0; r < rm.nr; r++) { cost += rm.alloc[dead[k]][r]; }
            if (best < 0 || cost <= best) { best = cost; victim = dead[k]; }
        }
        printf("[deadlock] recovery: terminating P%d\n", proc_table[victim].pid);
        sched_terminate(c, &proc_table[victim], -1);              /* the exit hook frees its resources */
    }
}

void rm_enable_monitor(uint64_t interval) { sched_set_monitor(rm_monitor, interval); }
