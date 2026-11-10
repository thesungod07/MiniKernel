#include <stdio.h>
#include <string.h>
#include "isa.h"
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
        ptr->cycles += 1;   /* count it, so cpu_run can't loop forever */
        return true;
    }

    uint32_t old_pc = ptr->pc;              /* address of this instruction */
    uint32_t local = ptr->ram[ptr->pc];
    ptr->pc += 1;                           /* pc now points at the NEXT instruction */

    uint32_t op = local >> 24;
    uint32_t ra = (local >> 16) & 0xFF;
    uint32_t imm = local & 0xFFFF;
    uint32_t rb = imm & 0xFF;

    if (ptr->trace == true)
    {
        printf("pc = %u, word = 0x%08x, op = %u, ra = %u, imm = %u, rb = %u\n",
               old_pc, local, op, ra, imm, rb);
    }

    ptr->cycles += 1;

    if (op >= OP_COUNT)
    {
        cpu_raise(ptr, VEC_ILLEGAL);
    }
    else
    {
        switch (op)
        {
            case OP_NOP:
                break;

            case OP_LDI:
                if (ra >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                ptr->reg[ra] = imm;
                break;

            case OP_MOV:
                if (ra >= NREGS || rb >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                ptr->reg[ra] = ptr->reg[rb];
                break;

            case OP_ADD:
                if (ra >= NREGS || rb >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                ptr->reg[ra] += ptr->reg[rb];
                break;

            case OP_SUB:
                if (ra >= NREGS || rb >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                ptr->reg[ra] -= ptr->reg[rb];
                break;

            case OP_MUL:
                if (ra >= NREGS || rb >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                ptr->reg[ra] *= ptr->reg[rb];
                break;

            case OP_CMP:
                if (ra >= NREGS || rb >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                ptr->zf = (ptr->reg[ra] == ptr->reg[rb]);
                break;

            case OP_JMP:
                ptr->pc = imm;
                break;

            case OP_JZ:
                if (ptr->zf) { ptr->pc = imm; }
                break;

            case OP_JNZ:
                if (!ptr->zf) { ptr->pc = imm; }
                break;

            case OP_LD:
                if (ra >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                if (imm >= RAM_WORDS) { cpu_raise(ptr, VEC_MEM_FAULT); break; }
                ptr->reg[ra] = ptr->ram[imm];
                break;

            case OP_ST:
                if (ra >= NREGS) { cpu_raise(ptr, VEC_ILLEGAL); break; }
                if (imm >= RAM_WORDS) { cpu_raise(ptr, VEC_MEM_FAULT); break; }
                ptr->ram[imm] = ptr->reg[ra];
                break;

            case OP_SYSCALL:    /* later: trap into the kernel */
                break;

            case OP_HLT:
                ptr->halted = true;
                break;

            case OP_CLI:        /* later: privileged */
            case OP_STI:
                break;
        }
    }

    return true;
}

void cpu_run(cpu_t *ptr, uint64_t max_cycles)
{
    while (ptr->halted == false && ptr->cycles < max_cycles)
    {
        cpu_step(ptr);
    }
}
