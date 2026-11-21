#include <stdio.h>
#include <string.h>
#include "sched.h"
#include "../hw/isa.h"

static const scheduler_t *sched;
static uint64_t last_account;
static pcb_t idle_proc;

static void account(cpu_t *c)
{
    uint64_t delta = c->cycles - last_account;
    last_account = c->cycles;

    if (current_proc != NULL && current_proc->state == PROC_RUNNING)
    {
        if (delta > current_proc->remaining_time) { current_proc->remaining_time = 0;}
        else {current_proc->remaining_time -= delta;}
    }
}

static void admit(const cpu_t *c)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        if (proc_table[i].state == PROC_NEW && proc_table[i].arrival_time <= c->cycles)
        {
            proc_table[i].state = PROC_READY;
        }
    }
}

static bool work_left(void)
{
    for (int i = 0; i < MAX_PROCS; i++)
    {
        proc_state_t s = proc_table[i].state;
        if (s == PROC_NEW || s == PROC_READY || s == PROC_RUNNING) { return true; }
    }
    return false;
}

static void run_next(cpu_t *c)
{
    pcb_t *next = sched->pick();

    if (next == NULL)
    {
        if (!work_left())
        {
            c->halted = true;
            return;
        }
        next = &idle_proc;
    }

    if (next != current_proc)
    {
        context_switch(c, next);
    }
}

static void isr_sched_timer(cpu_t *c, int vec)
{
    (void)vec;
    account(c);
    admit(c);

    if (current_proc == &idle_proc || sched->preemptive)
    {
        run_next(c);
    }
}

static void isr_sched_syscall(cpu_t *c, int vec)
{
    (void)vec;

    switch (c->reg[0])
    {
        case SYS_EXIT:
            account(c);
            current_proc->state = PROC_TERMINATED;
            current_proc->finish_time = c->cycles;
            current_proc->exit_code = (int)c->reg[1];
            run_next(c);
            break;

        case SYS_WRITE:
            printf("[P%d] output: %u\n", current_proc->pid, c->reg[1]);
            break;

        default:
            printf("[kernel] unknown syscall %u\n", c->reg[0]);
            break;
    }
}

static pcb_t *pick_fcfs(void)
{
    pcb_t *best = NULL;

    for (int i = 0; i < MAX_PROCS; i++)
    {
        pcb_t *p = &proc_table[i];
        if (p->state != PROC_READY && p->state != PROC_RUNNING) { continue; }
        if (best == NULL || p->arrival_time < best->arrival_time) { best = p; }
    }
    return best;
}

const scheduler_t sched_fcfs = { "FCFS", pick_fcfs, 0, 0 };

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
    current_proc = NULL;
    admit(c);
    run_next(c);

    c->pc = c->saved_pc;
    c->mode = MODE_USER;
    c->int_enabled = true;
}
