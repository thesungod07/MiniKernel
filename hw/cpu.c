#include <stdio.h>
#include <string.h>
#include "cpu.h"

void cpu_init(cpu_t *ptr)
{
    memset(ptr, 0, sizeof(*ptr));
    ptr->mode = MODE_KERNEL;
}

void cpu_raise(cpu_t *ptr, int vec)
{
    ptr->pending |= (1u << vec);
}

bool cpu_step(cpu_t *ptr)
{
    if (ptr->halted == true)
    {
        return false;
    }


    if (ptr->pc >= RAM_WORDS)
    {
        cpu_raise(ptr, VEC_MEM_FAULT);
        return true;
    }

    uint32_t local = ptr->ram[ptr->pc];
    ptr->pc += 1;

    uint32_t op = local >> 24;
    uint32_t ra = (local >> 16) & 0XFF;
    uint32_t imm = local & 0xFFFF;
    uint32_t rb = imm & 0xFF;

    if (ptr->trace == true)
    {
        printf("pc = %u, word = 0x%08x, op = %u, ra = %u, imm = %u, rb = %u\n", ptr->pc, local, op, ra, imm, rb);
    }

    ptr->cycles += 1;

    return true;
}
