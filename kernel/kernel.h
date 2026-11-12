#ifndef KERNEL_H
#define KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include "../hw/cpu.h"

#define USER_BASE 0x100
#define TIMER_PERIOD 50

enum syscall
{
    SYS_EXIT,
    SYS_WRITE
};

void kernel_install_handlers(cpu_t *c);
int boot_system(cpu_t *c, const uint32_t *image, size_t nwords);

#endif
