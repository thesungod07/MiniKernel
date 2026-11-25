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

static bool translate(const cpu_t *c, uint32_t logical, uint32_t *physical)
{
    if (c->mode == MODE_KERNEL)
    {
        if (logical < RAM_WORDS) { *physical = logical; return true; }
        return false;
    }
    if (logical < c->limit && (c->base + logical) < RAM_WORDS)
    {
        *physical = c->base + logical;
        return true;
    }
    return false;
}

static bool is_privileged(uint32_t op)
{
    return (op == OP_HLT || op == OP_CLI || op == OP_STI);
}

/* Deliver at most one pending interrupt. Traps (vectors 1..) come first and
 * the timer (vector 0) last, so a trap is never overtaken by a tick. */
static void dispatch(cpu_t *ptr)
{
    for (int k = 1; k <= NVECTORS; k++)
    {
        int v = k % NVECTORS;

        if (v == VEC_TIMER && ptr->int_enabled == false) { continue; }
        if (!(ptr->pending & (1u << v))) { continue; }

        ptr->pending &= ~(1u << v);
        ptr->irq_count[v]++;

        if (ptr->ivt[v] == NULL)
        {
            fprintf(stderr, "[hw] no handler for vector %d: double fault, CPU stopped\n", v);
            ptr->halted = true;
            return;
        }

        ptr->saved_pc = ptr->pc;
        ptr->saved_mode = ptr->mode;
        ptr->saved_int = ptr->int_enabled;

        ptr->mode = MODE_KERNEL;
        ptr->int_enabled = false;

        ptr->ivt[v](ptr, v);

        ptr->pc = ptr->saved_pc;
        ptr->mode = ptr->saved_mode;
        ptr->int_enabled = ptr->saved_int;
        return;
    }
}

bool cpu_step(cpu_t *ptr)
{
    uint32_t phys;

    if (ptr->halted) { return false; }

    if (!translate(ptr, ptr->pc, &phys))
    {
        cpu_raise(ptr, VEC_MEM_FAULT);
        dispatch(ptr);
        ptr->cycles += 1;
        return true;
    }

    uint32_t old_pc = ptr->pc;
    uint32_t word = ptr->ram[phys];
    ptr->pc += 1;

    uint32_t op = word >> 24;
    uint32_t ra = (word >> 16) & 0xFF;
    uint32_t imm = word & 0xFFFF;
    uint32_t rb = imm & 0xFF;

    if (ptr->trace)
    {
        printf("pc = %u, word = 0x%08x, op = %u, ra = %u, imm = %u, rb = %u\n", old_pc, word, op, ra, imm, rb);
    }

    ptr->cycles += 1;

    bool uses_ra = (op == OP_LDI || op == OP_LD || op == OP_ST || (op >= OP_MOV && op <= OP_CMP));
    bool uses_rb = (op >= OP_MOV && op <= OP_CMP);

    if (op >= OP_COUNT)
    {
        cpu_raise(ptr, VEC_ILLEGAL);
    }
    else if (is_privileged(op) && ptr->mode == MODE_USER)
    {
        cpu_raise(ptr, VEC_PRIV_FAULT);
    }
    else if ((uses_ra && ra >= NREGS) || (uses_rb && rb >= NREGS))
    {
        cpu_raise(ptr, VEC_ILLEGAL);
    }
    else
    {
        switch (op)
        {
            case OP_NOP: break;
            case OP_LDI: ptr->reg[ra] = imm; break;
            case OP_MOV: ptr->reg[ra] = ptr->reg[rb]; break;
            case OP_ADD: ptr->reg[ra] += ptr->reg[rb]; break;
            case OP_SUB: ptr->reg[ra] -= ptr->reg[rb]; break;
            case OP_MUL: ptr->reg[ra] *= ptr->reg[rb]; break;
            case OP_CMP: ptr->zf = (ptr->reg[ra] == ptr->reg[rb]); break;
            case OP_JMP: ptr->pc = imm; break;
            case OP_JZ:  if (ptr->zf)  { ptr->pc = imm; } break;
            case OP_JNZ: if (!ptr->zf) { ptr->pc = imm; } break;
            case OP_LD:
                if (!translate(ptr, imm, &phys)) { cpu_raise(ptr, VEC_MEM_FAULT); break; }
                ptr->reg[ra] = ptr->ram[phys];
                break;
            case OP_ST:
                if (!translate(ptr, imm, &phys)) { cpu_raise(ptr, VEC_MEM_FAULT); break; }
                ptr->ram[phys] = ptr->reg[ra];
                break;
            case OP_SYSCALL: cpu_raise(ptr, VEC_SYSCALL); break;
            case OP_HLT: ptr->halted = true; break;
            case OP_CLI: ptr->int_enabled = false; break;
            case OP_STI: ptr->int_enabled = true; break;
        }
    }

    if (ptr->timer_period != 0)
    {
        ptr->timer_count++;
        if (ptr->timer_count >= ptr->timer_period)
        {
            ptr->timer_count = 0;
            cpu_raise(ptr, VEC_TIMER);
        }
    }

    dispatch(ptr);
    return true;
}

void cpu_run(cpu_t *ptr, uint64_t max_cycles)
{
    while (ptr->halted == false && ptr->cycles < max_cycles)
    {
        cpu_step(ptr);
    }
}
