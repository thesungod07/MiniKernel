#ifndef PROGBUILD_H
#define PROGBUILD_H

/* Tiny helpers for writing test programs for the simulated CPU. */

#include <stdint.h>
#include "../hw/isa.h"
#include "proc.h"
#include "syscalls.h"

#define PB_NONE (-1)

typedef struct
{
    uint32_t w[PROC_MEM_WORDS];
    int n;
} prog_t;

static inline void pb_init(prog_t *p) { p->n = 0; }

static inline int pb_emit(prog_t *p, uint32_t word)
{
    if (p->n < PROC_MEM_WORDS) { p->w[p->n++] = word; }
    return p->n - 1;
}

static inline int pb_pos(const prog_t *p) { return p->n; }
static inline void pb_patch(prog_t *p, int index, uint32_t word) { p->w[index] = word; }

static inline void pb_nops(prog_t *p, int k)
{
    for (int i = 0; i < k; i++) { pb_emit(p, NOP()); }
}

/* load the arguments that are not PB_NONE, then SYSCALL */
static inline void pb_sys(prog_t *p, int num, int a1, int a2, int a3)
{
    pb_emit(p, LDI(0, num));
    if (a1 != PB_NONE) { pb_emit(p, LDI(1, a1)); }
    if (a2 != PB_NONE) { pb_emit(p, LDI(2, a2)); }
    if (a3 != PB_NONE) { pb_emit(p, LDI(3, a3)); }
    pb_emit(p, SYSCALL());
}

/* exit: two instructions (LDI R0, SYSCALL); the exit code is whatever is in R1 */
static inline void pb_exit(prog_t *p) { pb_sys(p, SYS_EXIT, PB_NONE, PB_NONE, PB_NONE); }

/* exit with an explicit exit code: three instructions */
static inline void pb_exit_code(prog_t *p, int code) { pb_sys(p, SYS_EXIT, code, PB_NONE, PB_NONE); }

/* exit with the value currently in R0 as the exit code */
static inline void pb_exit_r0(prog_t *p)
{
    pb_emit(p, MOV(1, 0));
    pb_emit(p, LDI(0, SYS_EXIT));
    pb_emit(p, SYSCALL());
}

/* create a process from a program; arrival > 0 keeps it NEW until then */
static inline int pb_spawn(cpu_t *c, const prog_t *p, int arrival, int priority)
{
    int pid = proc_create(c, p->w, (size_t)p->n, priority, (uint64_t)p->n);
    if (pid < 0) { return pid; }
    pcb_t *pcb = &proc_table[pid - 1];
    pcb->arrival_time = (uint64_t)arrival;
    pcb->state = arrival > 0 ? PROC_NEW : PROC_READY;
    return pid;
}

#endif
