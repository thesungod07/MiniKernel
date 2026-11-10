#include <stdio.h>
#include "../hw/isa.h"
#include "../hw/cpu.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static cpu_t cpu;

static void run_one(uint32_t word, cpu_mode_t mode)
{
    cpu_init(&cpu);
    cpu.mode = mode;
    cpu.ram[0] = word;
    cpu_step(&cpu);
}

int main(void)
{
    printf("user mode: privileged instructions must fault\n");
    run_one(HLT(), MODE_USER);
    CHECK(cpu.pending == (1u << VEC_PRIV_FAULT), "HLT raises PRIV_FAULT");
    CHECK(!cpu.halted, "HLT did not halt the CPU");

    run_one(CLI(), MODE_USER);
    CHECK(cpu.pending == (1u << VEC_PRIV_FAULT), "CLI raises PRIV_FAULT");

    cpu_init(&cpu);
    cpu.mode = MODE_USER;
    cpu.int_enabled = false;
    cpu.ram[0] = STI();
    cpu_step(&cpu);
    CHECK(cpu.pending == (1u << VEC_PRIV_FAULT), "STI raises PRIV_FAULT");
    CHECK(!cpu.int_enabled, "STI did not change int_enabled");

    printf("kernel mode: privileged instructions work\n");
    run_one(HLT(), MODE_KERNEL);
    CHECK(cpu.halted && cpu.pending == 0, "HLT halts, no fault");

    cpu_init(&cpu);
    cpu.int_enabled = true;
    cpu.ram[0] = CLI();
    cpu_step(&cpu);
    CHECK(!cpu.int_enabled && cpu.pending == 0, "CLI disables interrupts");

    run_one(STI(), MODE_KERNEL);
    CHECK(cpu.int_enabled && cpu.pending == 0, "STI enables interrupts");

    printf("syscall trap\n");
    run_one(SYSCALL(), MODE_USER);
    CHECK(cpu.pending == (1u << VEC_SYSCALL), "SYSCALL in user mode raises SYSCALL");
    run_one(SYSCALL(), MODE_KERNEL);
    CHECK(cpu.pending == (1u << VEC_SYSCALL), "SYSCALL in kernel mode raises SYSCALL");

    printf("unprivileged instructions are fine in user mode\n");
    run_one(LDI(1, 9), MODE_USER);
    CHECK(cpu.reg[1] == 9 && cpu.pending == 0, "LDI runs in user mode");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
