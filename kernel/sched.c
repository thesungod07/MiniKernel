#include <stdio.h>
#include <string.h>
#include "sched.h"
#include "../hw/isa.h"

#define GANTT_MAX 16384
#define NLEVELS 3
#define AGING_INTERVAL 5
#define MLFQ_BOOST 60

static const uint64_t mlfq_quantum[NLEVELS] = { 2, 4, 8 };

static const scheduler_t *sched;
static uint64_t last_account, now_cycles, slice_used;
static int slice_restart;
static pcb_t idle_proc;

static uint64_t queued_at[MAX_PROCS];     /* when the process joined its ready queue (FIFO order) */
static uint64_t age_stamp[MAX_PROCS];
static uint64_t wake_at[MAX_PROCS];
static uint64_t blocked_at[MAX_PROCS];
static uint64_t io_time[MAX_PROCS];
static int level[MAX_PROCS];              /* MLFQ level */
static int eff_prio[MAX_PROCS];           /* effective priority (aging, inheritance) */
static int sleeping[MAX_PROCS];
static const void *wait_chan[MAX_PROCS];

static uint64_t busy_cycles, switch_count, last_boost, last_monitor, monitor_interval;
static void (*monitor_fn)(cpu_t *);
static void (*resume_hook)(cpu_t *, pcb_t *);
static void (*exit_hook)(pcb_t *);
static syscall_fn syscall_table[SYSCALL_MAX];

static int gantt_log[GANTT_MAX];
static int gantt_len;

/* ---------- small helpers ---------- */

static int slot_of(const pcb_t *p) { return (int)(p - proc_table); }
static bool is_real(const pcb_t *p) { return p != NULL && p->pid != 0; }
static bool runnable(const pcb_t *p) { return p->state == PROC_READY || p->state == PROC_RUNNING; }

static pcb_t *running_real(void)
{
    if (is_real(current_proc) && current_proc->state == PROC_RUNNING) { return current_proc; }
    return NULL;
}

static bool any_sleeping(void)
{
    for (int i = 0; i < MAX_PROCS; i++) { if (sleeping[i]) { return true; } }
    return false;
}

static bool work_left(void)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        proc_state_t s = proc_table[i].state;
        if (s == PROC_NEW || s == PROC_READY || s == PROC_RUNNING) { return true; }
        if (s == PROC_WAITING && (sleeping[i] || monitor_fn != NULL)) { return true; }
    }
    return false;
}

/* ---------- bookkeeping ---------- */

static void account(cpu_t *c)
{
    uint64_t delta = c->cycles - last_account;
    last_account = c->cycles;
    now_cycles = c->cycles;

    int pid = 0;
    if (current_proc != NULL && current_proc->state == PROC_RUNNING) { pid = current_proc->pid; }

    for (uint64_t k = 0; k < delta && gantt_len < GANTT_MAX; k++) { gantt_log[gantt_len++] = pid; }

    slice_used += delta;
    if (pid != 0)
    {
        busy_cycles += delta;
        if (delta > current_proc->remaining_time) { current_proc->remaining_time = 0; }
        else { current_proc->remaining_time -= delta; }
    }
}

static void admit(const cpu_t *c)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (proc_table[i].state == PROC_NEW && proc_table[i].arrival_time <= c->cycles)
        {
            proc_table[i].state = PROC_READY;
            level[i] = 0;
            queued_at[i] = proc_table[i].arrival_time;
            eff_prio[i] = proc_table[i].priority;
            age_stamp[i] = c->cycles;
        }
    }
}

static void wake_sleepers(const cpu_t *c)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (sleeping[i] && wake_at[i] <= c->cycles)
        {
            sleeping[i] = 0;
            proc_table[i].state = PROC_READY;
            proc_table[i].reg[0] = 0;
            queued_at[i] = c->cycles;
        }
    }
}

static void age_tick(const cpu_t *c)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        pcb_t *p = &proc_table[i];
        if (p->state == PROC_READY)
        {
            if (c->cycles - age_stamp[i] >= AGING_INTERVAL && eff_prio[i] > 0)
            {
                eff_prio[i]--;
                age_stamp[i] = c->cycles;
            }
        }
        else if (p->state == PROC_RUNNING) { age_stamp[i] = c->cycles; }
    }
}

static void mlfq_boost(const cpu_t *c)
{
    if (c->cycles - last_boost < MLFQ_BOOST) { return; }
    last_boost = c->cycles;
    for (int i = 0; i < MAX_PROCS; i++) { level[i] = 0; }
}

/* ---------- algorithms ---------- */

static uint64_t key_arrival(const pcb_t *p)   { return p->arrival_time; }
static uint64_t key_burst(const pcb_t *p)     { return p->burst_time; }
static uint64_t key_remaining(const pcb_t *p) { return p->remaining_time; }
static uint64_t key_priority(const pcb_t *p)  { return (uint64_t)eff_prio[slot_of(p)]; }

/* Smallest key wins; the running process wins ties so equal keys never preempt. */
static pcb_t *pick_min(uint64_t (*key)(const pcb_t *))
{
    pcb_t *best = running_real();

    for (int i = 0; i < MAX_PROCS; i++)
    {
        pcb_t *p = &proc_table[i];
        if (!runnable(p)) { continue; }
        if (best == NULL || key(p) < key(best)) { best = p; }
    }
    return best;
}

static pcb_t *pick_fcfs(void) { return pick_min(key_arrival); }
static pcb_t *pick_sjf(void)  { return pick_min(key_burst); }
static pcb_t *pick_srtf(void) { return pick_min(key_remaining); }
static pcb_t *pick_prio(void) { return pick_min(key_priority); }

static pcb_t *pick_rr(void)
{
    pcb_t *cur = running_real();

    if (cur != NULL && slice_used < sched->quantum) { return cur; }

    int start = is_real(current_proc) ? slot_of(current_proc) : -1;
    for (int k = 1; k <= MAX_PROCS; k++)
    {
        pcb_t *p = &proc_table[(start + k) % MAX_PROCS];
        if (runnable(p))
        {
            if (p == current_proc) { slice_restart = 1; }
            return p;
        }
    }
    return NULL;
}

/* Multilevel queue: level 0 (priority 0) is round robin and always beats
 * level 1 (any other priority), which is plain FCFS. */
static int mlq_level(const pcb_t *p) { return p->priority == 0 ? 0 : 1; }

static pcb_t *level_pick(int lvl, const pcb_t *exclude, int by_arrival)
{
    pcb_t *best = NULL;
    for (int i = 0; i < MAX_PROCS; i++)
    {
        pcb_t *p = &proc_table[i];
        if (!runnable(p) || mlq_level(p) != lvl || p == exclude) { continue; }
        uint64_t k = by_arrival ? p->arrival_time : queued_at[i];
        uint64_t kb = best ? (by_arrival ? best->arrival_time : queued_at[slot_of(best)]) : 0;
        if (best == NULL || k < kb) { best = p; }
    }
    return best;
}

static pcb_t *pick_mlq(void)
{
    pcb_t *cur = running_real();
    bool any0 = false;

    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (runnable(&proc_table[i]) && mlq_level(&proc_table[i]) == 0) { any0 = true; }
    }

    if (any0)
    {
        bool cur0 = (cur != NULL && mlq_level(cur) == 0);
        if (cur0 && slice_used < sched->quantum) { return cur; }
        pcb_t *n = level_pick(0, cur0 ? cur : NULL, 0);
        if (n == NULL) { n = cur; slice_restart = 1; }
        return n;
    }

    if (cur != NULL) { return cur; }
    return level_pick(1, NULL, 1);
}

/* Multilevel feedback queue: new jobs start at level 0, a job that uses its
 * whole quantum is demoted, and everything is boosted back up periodically. */
static pcb_t *pick_mlfq(void)
{
    pcb_t *cur = running_real();
    bool expired = false;

    if (cur != NULL)
    {
        int cs = slot_of(cur);
        if (slice_used >= mlfq_quantum[level[cs]])
        {
            if (level[cs] < NLEVELS - 1) { level[cs]++; }
            queued_at[cs] = now_cycles;
            expired = true;
        }
    }

    pcb_t *best = NULL;
    for (int i = 0; i < MAX_PROCS; i++)
    {
        pcb_t *p = &proc_table[i];
        if (!runnable(p)) { continue; }
        if (best == NULL || level[i] < level[slot_of(best)]
            || (level[i] == level[slot_of(best)] && queued_at[i] < queued_at[slot_of(best)]))
        {
            best = p;
        }
    }

    if (best != NULL && best == cur && expired) { slice_restart = 1; }
    return best;
}

const scheduler_t sched_fcfs       = { "FCFS",                pick_fcfs, 0, 0, 0, 0 };
const scheduler_t sched_sjf        = { "SJF",                 pick_sjf,  0, 0, 0, 0 };
const scheduler_t sched_srtf       = { "SRTF",                pick_srtf, 1, 0, 0, 0 };
const scheduler_t sched_prio       = { "Priority",            pick_prio, 0, 0, 0, 0 };
const scheduler_t sched_prio_p     = { "Priority (preempt)",  pick_prio, 1, 0, 0, 0 };
const scheduler_t sched_prio_aging = { "Priority + aging",    pick_prio, 1, 0, 0, 1 };
const scheduler_t sched_rr         = { "RR (q=4)",            pick_rr,   1, 4, 0, 0 };
const scheduler_t sched_mlq        = { "MLQ (RR q=4 / FCFS)", pick_mlq,  1, 4, 0, 0 };
const scheduler_t sched_mlfq       = { "MLFQ (q=2,4,8)",      pick_mlfq, 1, 0, 0, 0 };
const scheduler_t sched_batch      = { "Batch (CPU held in I/O)", pick_fcfs, 0, 0, 1, 0 };
const scheduler_t sched_multiprog  = { "Multiprogramming",    pick_fcfs, 0, 0, 0, 0 };
const scheduler_t sched_timeshare  = { "Time-sharing (RR q=4)", pick_rr, 1, 4, 0, 0 };

/* ---------- switching ---------- */

static void run_next(cpu_t *c)
{
    pcb_t *next;

    if (sched->io_hold && any_sleeping()) { next = &idle_proc; }
    else { next = sched->pick(); }

    if (next == NULL)
    {
        if (!work_left()) { c->halted = true; return; }
        next = &idle_proc;
    }

    if (next != current_proc)
    {
        if (running_real() != NULL) { queued_at[slot_of(current_proc)] = now_cycles; }
        context_switch(c, next);
        switch_count++;
        slice_used = 0;
        slice_restart = 0;
        if (resume_hook != NULL && is_real(next)) { resume_hook(c, next); }
    }
    else if (slice_restart)
    {
        slice_used = 0;
        slice_restart = 0;
    }
}

static void finish_proc(cpu_t *c, pcb_t *p, int code)
{
    int s = slot_of(p);
    p->state = PROC_TERMINATED;
    p->finish_time = c->cycles;
    p->exit_code = code;
    sleeping[s] = 0;
    wait_chan[s] = NULL;
    if (exit_hook != NULL) { exit_hook(p); }
}

void sched_terminate(cpu_t *c, pcb_t *p, int code)
{
    if (p->state == PROC_TERMINATED) { return; }
    bool was_current = (p == current_proc);
    finish_proc(c, p, code);
    if (was_current) { run_next(c); }
}

void sched_block_on(cpu_t *c, const void *chan)
{
    int s = slot_of(current_proc);
    wait_chan[s] = chan;
    blocked_at[s] = now_cycles;
    context_save(c, current_proc);
    current_proc->state = PROC_WAITING;
    run_next(c);
}

int sched_wake_one(const void *chan)
{
    int best = -1;
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (proc_table[i].state == PROC_WAITING && !sleeping[i] && wait_chan[i] == chan)
        {
            if (best < 0 || blocked_at[i] < blocked_at[best]) { best = i; }
        }
    }
    if (best < 0) { return 0; }
    wait_chan[best] = NULL;
    proc_table[best].state = PROC_READY;
    queued_at[best] = now_cycles;
    return proc_table[best].pid;
}

int sched_wake_all(const void *chan)
{
    int n = 0;
    while (sched_wake_one(chan) != 0) { n++; }
    return n;
}

const void *sched_wait_chan(const pcb_t *p) { return wait_chan[slot_of(p)]; }

int sched_interrupt_sleep(pcb_t *p)
{
    int s = slot_of(p);
    if (!sleeping[s]) { return 0; }
    sleeping[s] = 0;
    p->state = PROC_READY;
    p->reg[0] = 0;
    queued_at[s] = now_cycles;
    return 1;
}

int sched_get_eff_prio(const pcb_t *p) { return eff_prio[slot_of(p)]; }
void sched_set_eff_prio(pcb_t *p, int v) { eff_prio[slot_of(p)] = v; }

pcb_t *sched_find_pid(int pid)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (proc_table[i].state != PROC_UNUSED && proc_table[i].pid == pid) { return &proc_table[i]; }
    }
    return NULL;
}

void sched_set_monitor(void (*fn)(cpu_t *), uint64_t interval) { monitor_fn = fn; monitor_interval = interval; }
void sched_set_resume_hook(void (*fn)(cpu_t *, pcb_t *)) { resume_hook = fn; }
void sched_set_exit_hook(void (*fn)(pcb_t *)) { exit_hook = fn; }

void syscall_register(unsigned num, syscall_fn fn)
{
    if (num < SYSCALL_MAX) { syscall_table[num] = fn; }
}

/* ---------- interrupt handlers ---------- */

static void isr_sched_timer(cpu_t *c, int vec)
{
    (void)vec;
    account(c);
    admit(c);
    wake_sleepers(c);

    if (sched->aging) { age_tick(c); }
    if (sched == &sched_mlfq) { mlfq_boost(c); }

    if (monitor_fn != NULL && c->cycles - last_monitor >= monitor_interval)
    {
        last_monitor = c->cycles;
        monitor_fn(c);
    }

    if (current_proc == NULL || current_proc == &idle_proc || sched->preemptive || current_proc->state != PROC_RUNNING)
    {
        run_next(c);
    }
}

static void isr_sched_syscall(cpu_t *c, int vec)
{
    (void)vec;
    account(c);
    admit(c);               /* a job that arrives at this very cycle must be visible to the pick */
    wake_sleepers(c);

    uint32_t n = c->reg[0];

    switch (n)
    {
        case SYS_EXIT:
            sched_terminate(c, current_proc, (int)c->reg[1]);
            break;

        case SYS_WRITE:
            printf("[P%d] output: %u\n", current_proc->pid, c->reg[1]);
            c->reg[0] = 0;
            break;

        case SYS_GETPID:
            c->reg[0] = (uint32_t)current_proc->pid;
            break;

        case SYS_SLEEP:
        {
            uint32_t len = c->reg[1];
            c->reg[0] = 0;
            if (len == 0) { break; }
            int s = slot_of(current_proc);
            sleeping[s] = 1;
            wake_at[s] = c->cycles + len;
            io_time[s] += len;
            context_save(c, current_proc);
            current_proc->state = PROC_WAITING;
            run_next(c);
            break;
        }

        default:
            if (n < SYSCALL_MAX && syscall_table[n] != NULL) { syscall_table[n](c); }
            else
            {
                printf("[kernel] unknown syscall %u\n", n);
                c->reg[0] = (uint32_t)-1;
            }
            break;
    }

    if (!c->halted && resume_hook != NULL && running_real() != NULL) { resume_hook(c, current_proc); }
}

/* ---------- setup ---------- */

void sched_init(cpu_t *c, const scheduler_t *s)
{
    sched = s;

    kernel_install_handlers(c);
    c->ivt[VEC_TIMER] = isr_sched_timer;
    c->ivt[VEC_SYSCALL] = isr_sched_syscall;
    c->timer_period = 1;

    memset(&idle_proc, 0, sizeof idle_proc);
    idle_proc.pid = 0;
    idle_proc.state = PROC_READY;
    idle_proc.base = 0;
    idle_proc.limit = 1;
    c->ram[0] = JMP(0);
}

void sched_start(cpu_t *c)
{
    last_account = c->cycles;
    now_cycles = c->cycles;
    slice_used = 0;
    slice_restart = 0;
    busy_cycles = 0;
    switch_count = 0;
    gantt_len = 0;
    last_boost = c->cycles;
    last_monitor = c->cycles;
    current_proc = NULL;

    for (int i = 0; i < MAX_PROCS; i++)
    {
        queued_at[i] = proc_table[i].arrival_time;
        level[i] = 0;
        eff_prio[i] = proc_table[i].priority;
        age_stamp[i] = c->cycles;
        sleeping[i] = 0;
        wait_chan[i] = NULL;
        io_time[i] = 0;
    }

    admit(c);
    run_next(c);

    c->pc = c->saved_pc;
    c->mode = MODE_USER;
    c->int_enabled = true;
}

/* ---------- statistics ---------- */

uint64_t sched_busy_cycles(void) { return busy_cycles; }
uint64_t sched_switches(void) { return switch_count; }
uint64_t sched_io_time(const pcb_t *p) { return io_time[slot_of(p)]; }
const int *sched_gantt(int *len) { *len = gantt_len; return gantt_log; }
