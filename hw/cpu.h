#ifndef CPU_H
#define CPU_H
#include <stdint.h>
#include <stdbool.h>

#define NREGS 8
#define RAM_WORDS 4096
#define NVECTORS 8

typedef enum { MODE_KERNEL, MODE_USER } cpu_mode_t;

enum vectors { VEC_TIMER, VEC_SYSCALL, VEC_PRIV_FAULT, VEC_ILLEGAL, VEC_MEM_FAULT };

struct cpu;
typedef void (*isr_t)(struct cpu *c, int vec);

typedef struct cpu
{
    uint32_t reg[NREGS];
    uint32_t pc;
    bool zf;
    cpu_mode_t mode;
    bool int_enabled;
    bool halted;
    bool trace;

    uint32_t base, limit;               /* address translation (user mode) */

    uint32_t saved_pc;                  /* saved on interrupt entry */
    cpu_mode_t saved_mode;
    bool saved_int;

    uint32_t ram[RAM_WORDS];
    isr_t ivt[NVECTORS];
    unsigned pending;

    uint32_t timer_period, timer_count;
    uint64_t cycles;
    uint64_t irq_count[NVECTORS];
} cpu_t;

void cpu_init(cpu_t *ptr);
void cpu_raise(cpu_t *ptr, int vec);
bool cpu_step(cpu_t *ptr);
void cpu_run(cpu_t *ptr, uint64_t max_cycles);
#endif
