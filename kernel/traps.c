#include <stdio.h>
#include "kernel.h"

kstate_t ks;

static void isr_timer(cpu_t *c, int vec)
{
    (void)c;
    (void)vec;
    ks.timer_ticks++;
    if (ks.verbose != 0) { printf("[kernel] timer tick #%d\n", ks.timer_ticks); }
}

static void isr_syscall(cpu_t *c, int vec)
{
    (void)vec;
    ks.syscalls++;
    switch (c->reg[0])
    {
        case SYS_EXIT:
            printf("[kernel] sys_exit: program finished\n");
            ks.exited = 1;
            c->halted = true;
            break;
        case SYS_WRITE:
            ks.last_write = c->reg[1];
            printf("[kernel] sys_write: %u\n", ks.last_write);
            break;
        default:
            printf("[kernel] unknown syscall: %u\n", c->reg[0]);
            break;
    }
}

static void kill_program(cpu_t *c, const char *why)
{
    printf("[kernel] %s at pc=0x%04x (mode was %s): killing program\n", why,
           (unsigned)(c->saved_pc - 1), c->saved_mode == MODE_USER ? "user" : "kernel");
    ks.killed = 1;
    c->halted = true;
}

static void isr_priv(cpu_t *c, int vec)    { (void)vec; ks.priv_faults++;    kill_program(c, "privilege violation"); }
static void isr_illegal(cpu_t *c, int vec) { (void)vec; ks.illegal_faults++; kill_program(c, "illegal instruction"); }
static void isr_mem(cpu_t *c, int vec)     { (void)vec; ks.mem_faults++;     kill_program(c, "memory fault"); }

void kernel_install_handlers(cpu_t *c)
{
    c->ivt[VEC_TIMER]      = isr_timer;
    c->ivt[VEC_SYSCALL]    = isr_syscall;
    c->ivt[VEC_PRIV_FAULT] = isr_priv;
    c->ivt[VEC_ILLEGAL]    = isr_illegal;
    c->ivt[VEC_MEM_FAULT]  = isr_mem;
}
