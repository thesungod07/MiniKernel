#include "cpu.h"
#include <string.h>

void cpu_init(cpu_t *ptr)
{
    memset(ptr, 0, sizeof(*ptr));
    ptr->mode = MODE_KERNEL;
}

void cpu_raise(cpu_t *ptr, int vec)
{
    ptr->pending |= (1u << vec);
}
