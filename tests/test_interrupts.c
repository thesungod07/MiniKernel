#include <stdio.h>
#include "../hw/isa.h"
#include "../hw/cpu.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static cpu_t cpu;
static int calls;
static cpu_mode_t mode_inside;
static bool int_inside;
static uint32_t pc_inside;

static void spy(cpu_t *c, int vec)
{
    (void)vec;
    calls++;
    mode_inside = c->mode;
    int_inside = c->int_enabled;
    pc_inside = c->saved_pc;
}

static void redirect(cpu_t *c, int vec)
{
    (void)vec;
    c->saved_pc = 0x50;
}

static void reset(void) { cpu_init(&cpu); calls = 0; }

int main(void)
{
    printf("syscall trap from user mode\n");
    reset();
    cpu.mode = MODE_USER;
    cpu.int_enabled = true;
    cpu.ivt[VEC_SYSCALL] = spy;
    cpu.ram[0] = LDI(1, 5);
    cpu.ram[1] = SYSCALL();
    cpu.ram[2] = LDI(2, 6);
    cpu_step(&cpu); cpu_step(&cpu); cpu_step(&cpu);
    CHECK(calls == 1, "handler called once");
    CHECK(mode_inside == MODE_KERNEL, "handler ran in kernel mode");
    CHECK(!int_inside, "interrupts off inside handler");
    CHECK(pc_inside == 2, "saved pc points after the SYSCALL");
    CHECK(cpu.mode == MODE_USER, "user mode restored after handler");
    CHECK(cpu.int_enabled, "int_enabled restored after handler");
    CHECK(cpu.reg[1] == 5 && cpu.reg[2] == 6, "program resumed in the right place");
    CHECK(cpu.pending == 0, "nothing left pending");
    CHECK(cpu.irq_count[VEC_SYSCALL] == 1, "irq_count updated");

    printf("privilege fault is delivered\n");
    reset();
    cpu.mode = MODE_USER;
    cpu.ivt[VEC_PRIV_FAULT] = spy;
    cpu.ram[0] = HLT();
    cpu_step(&cpu);
    CHECK(calls == 1 && !cpu.halted, "fault handler ran, HLT did not execute");

    printf("timer is masked while interrupts are off\n");
    reset();
    cpu.int_enabled = false;
    cpu.ivt[VEC_TIMER] = spy;
    cpu.ram[0] = NOP();
    cpu.ram[1] = NOP();
    cpu_raise(&cpu, VEC_TIMER);
    cpu_step(&cpu);
    CHECK(calls == 0 && cpu.pending == (1u << VEC_TIMER), "masked timer stays pending");
    cpu.int_enabled = true;
    cpu_step(&cpu);
    CHECK(calls == 1 && cpu.pending == 0, "delivered once interrupts are enabled");

    printf("no handler installed\n");
    reset();
    cpu.ram[0] = SYSCALL();
    cpu_step(&cpu);
    CHECK(cpu.halted, "double fault halts the CPU");

    printf("handler can redirect where the program resumes\n");
    reset();
    cpu.ivt[VEC_SYSCALL] = redirect;
    cpu.ram[0] = SYSCALL();
    cpu_step(&cpu);
    CHECK(cpu.pc == 0x50, "pc changed by editing saved_pc");

    printf("fetch outside RAM\n");
    reset();
    cpu.ivt[VEC_MEM_FAULT] = spy;
    cpu.pc = RAM_WORDS;
    cpu_step(&cpu);
    CHECK(calls == 1, "memory fault delivered");

    printf("priority: lower vector first, one per step\n");
    reset();
    cpu.ivt[VEC_SYSCALL] = spy;
    cpu.ivt[VEC_ILLEGAL] = spy;
    cpu.ram[0] = NOP();
    cpu_raise(&cpu, VEC_SYSCALL);
    cpu_raise(&cpu, VEC_ILLEGAL);
    cpu_step(&cpu);
    CHECK(calls == 1 && cpu.pending == (1u << VEC_ILLEGAL), "syscall first, illegal still pending");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
