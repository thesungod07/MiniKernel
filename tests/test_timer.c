#include <stdio.h>
#include "../hw/isa.h"
#include "../hw/cpu.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static cpu_t cpu;
static int ticks;
static cpu_mode_t mode_in_tick;

static void tick(cpu_t *c, int vec)
{
    (void)vec;
    ticks++;
    mode_in_tick = c->mode;
}

static void reset(void)
{
    cpu_init(&cpu);
    ticks = 0;
    cpu.ivt[VEC_TIMER] = tick;
}

int main(void)
{
    printf("timer fires every N instructions\n");
    reset();
    cpu.mode = MODE_USER;
    cpu.int_enabled = true;
    cpu.timer_period = 5;
    cpu_run(&cpu, 20);
    CHECK(ticks == 4, "4 ticks in 20 instructions at period 5");
    CHECK(cpu.irq_count[VEC_TIMER] == 4, "irq_count agrees");
    CHECK(mode_in_tick == MODE_KERNEL, "tick handler ran in kernel mode");
    CHECK(cpu.mode == MODE_USER, "user mode restored after ticks");

    printf("period 0 means timer off\n");
    reset();
    cpu.int_enabled = true;
    cpu.timer_period = 0;
    cpu_run(&cpu, 50);
    CHECK(ticks == 0, "no ticks when period is 0");

    printf("timer is held back while interrupts are disabled\n");
    reset();
    cpu.int_enabled = false;
    cpu.timer_period = 5;
    cpu_run(&cpu, 12);
    CHECK(ticks == 0, "no ticks delivered while disabled");
    CHECK(cpu.pending == (1u << VEC_TIMER), "timer interrupt is waiting");
    cpu.int_enabled = true;
    cpu_step(&cpu);
    CHECK(ticks == 1 && cpu.pending == 0, "delivered when interrupts come back on");

    printf("program result is not disturbed by ticks\n");
    reset();
    cpu.mode = MODE_USER;
    cpu.int_enabled = true;
    cpu.timer_period = 3;
    cpu.ram[0] = LDI(1, 0);
    cpu.ram[1] = LDI(2, 100);
    cpu.ram[2] = LDI(3, 1);
    cpu.ram[3] = LDI(4, 0);
    cpu.ram[4] = ADD(1, 2);
    cpu.ram[5] = SUB(2, 3);
    cpu.ram[6] = CMP(2, 4);
    cpu.ram[7] = JNZ(4);
    cpu.mode = MODE_KERNEL;
    cpu.ram[8] = HLT();
    cpu_run(&cpu, 100000);
    CHECK(cpu.reg[1] == 5050, "sum is 5050 despite many interrupts");
    CHECK(ticks > 100, "many ticks happened during the run");

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
