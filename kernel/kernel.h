#ifndef KERNEL_H
#define KERNEL_H
#include <stddef.h>
#include <stdint.h>
#include "../hw/cpu.h"

#define USER_BASE 0x100
#define TIMER_PERIOD 50

enum { SYS_EXIT = 0, SYS_WRITE = 1 };

typedef struct
{
    int timer_ticks, syscalls, priv_faults, illegal_faults, mem_faults;
    int exited, killed;
    int verbose;
    uint32_t last_write;
} kstate_t;

extern kstate_t ks;

void kernel_install_handlers(cpu_t *c);
int boot_system(cpu_t *c, const uint32_t *image, size_t n);
#endif
