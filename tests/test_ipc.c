#include <stdio.h>
#include "../kernel/ipc.h"
#include "../kernel/progbuild.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static cpu_t cpu;
static scheduler_t rr4;

static void setup(void)
{
    cpu_init(&cpu);
    proc_init();
    rr4 = sched_rr;
    rr4.quantum = 4;
    sched_init(&cpu, &rr4);
    ipc_init();
}

static void go(void) { sched_start(&cpu); cpu_run(&cpu, 200000); }
static uint32_t mem(int proc_index, int addr) { return cpu.ram[proc_table[proc_index].base + addr]; }

int main(void)
{
    printf("pipe: producer sends 1..10 through an 8-slot pipe, consumer sums until EOF\n");
    {
        setup();
        int pipe = pipe_create();
        prog_t cons, prod;

        pb_init(&cons);
        pb_emit(&cons, LDI(7, 0));
        pb_emit(&cons, LDI(5, 0)); pb_emit(&cons, LDI(6, 1)); pb_emit(&cons, SUB(5, 6));   /* R5 = EOF marker */
        int loop = pb_pos(&cons);
        pb_sys(&cons, SYS_PIPE_READ, pipe, PB_NONE, PB_NONE);
        pb_emit(&cons, CMP(0, 5));
        int jz = pb_emit(&cons, JZ(0));
        pb_emit(&cons, ADD(7, 0));
        pb_emit(&cons, JMP(loop));
        pb_patch(&cons, jz, JZ(pb_pos(&cons)));
        pb_emit(&cons, MOV(1, 7)); pb_emit(&cons, LDI(0, SYS_EXIT)); pb_emit(&cons, SYSCALL());

        pb_init(&prod);
        pb_emit(&prod, LDI(2, 1)); pb_emit(&prod, LDI(3, 11)); pb_emit(&prod, LDI(4, 1));
        int ploop = pb_pos(&prod);
        pb_emit(&prod, CMP(2, 3));
        int pjz = pb_emit(&prod, JZ(0));
        pb_sys(&prod, SYS_PIPE_WRITE, pipe, PB_NONE, PB_NONE);          /* R2 is the value */
        pb_emit(&prod, ADD(2, 4));
        pb_emit(&prod, JMP(ploop));
        pb_patch(&prod, pjz, JZ(pb_pos(&prod)));
        pb_sys(&prod, SYS_PIPE_CLOSE, pipe, PB_NONE, PB_NONE);
        pb_exit(&prod);

        pb_spawn(&cpu, &cons, 0, 1);
        pb_spawn(&cpu, &prod, 0, 1);
        go();
        CHECK(proc_table[0].state == PROC_TERMINATED && proc_table[1].state == PROC_TERMINATED, "both processes finished");
        CHECK(proc_table[0].exit_code == 55, "consumer received all ten values (sum 55)");
        CHECK(sched_switches() > 4, "the pipe made them block and switch");
    }

    printf("message queue: selective receive by type\n");
    {
        setup();
        int q = msgq_create();
        prog_t rcv, snd;

        pb_init(&rcv);
        pb_sys(&rcv, SYS_MSG_RECV, q, 2, PB_NONE);  pb_emit(&rcv, ST(0, 100));     /* wants type 2 first */
        pb_sys(&rcv, SYS_MSG_RECV, q, 0, PB_NONE);  pb_emit(&rcv, ST(0, 101));
        pb_sys(&rcv, SYS_MSG_RECV, q, 0, PB_NONE);  pb_emit(&rcv, ST(0, 102));
        pb_exit(&rcv);

        pb_init(&snd);
        pb_nops(&snd, 3);
        pb_sys(&snd, SYS_MSG_SEND, q, 1, 10);
        pb_sys(&snd, SYS_MSG_SEND, q, 2, 20);
        pb_sys(&snd, SYS_MSG_SEND, q, 1, 30);
        pb_exit(&snd);

        pb_spawn(&cpu, &rcv, 0, 1);
        pb_spawn(&cpu, &snd, 0, 1);
        go();
        CHECK(mem(0, 100) == 20, "first receive picked the type-2 message (20)");
        CHECK(mem(0, 101) == 10 && mem(0, 102) == 30, "the rest came out in order (10, 30)");
    }

    printf("shared memory: writer then reader\n");
    {
        setup();
        int seg = shm_create(8);
        prog_t wr, rd;

        pb_init(&wr);
        pb_sys(&wr, SYS_SHM_WRITE, seg, 3, 1234);
        pb_exit(&wr);

        pb_init(&rd);
        pb_nops(&rd, 20);
        pb_sys(&rd, SYS_SHM_READ, seg, 3, PB_NONE);
        pb_emit(&rd, ST(0, 100));
        pb_exit(&rd);

        pb_spawn(&cpu, &wr, 0, 1);
        pb_spawn(&cpu, &rd, 0, 1);
        go();
        CHECK(shm_peek(seg, 3) == 1234, "segment holds the written value");
        CHECK(mem(1, 100) == 1234, "reader saw it");
    }

    printf("signals: handler runs, then the process resumes\n");
    {
        setup();
        prog_t a, b;

        pb_init(&a);
        pb_emit(&a, LDI(0, SYS_SIGACTION)); pb_emit(&a, LDI(1, SIGUSR1));
        int h = pb_emit(&a, LDI(2, 0));                                   /* handler address, patched below */
        pb_emit(&a, SYSCALL());
        int loop = pb_pos(&a);
        pb_emit(&a, LD(2, 100)); pb_emit(&a, LDI(3, 0)); pb_emit(&a, CMP(2, 3));
        pb_emit(&a, JZ(loop));                                            /* wait for the flag */
        pb_emit(&a, MOV(1, 2)); pb_emit(&a, LDI(0, SYS_EXIT)); pb_emit(&a, SYSCALL());
        pb_patch(&a, h, LDI(2, pb_pos(&a)));
        pb_emit(&a, LDI(2, 42)); pb_emit(&a, ST(2, 100));                 /* the handler sets the flag */
        pb_emit(&a, LDI(0, SYS_SIGRETURN)); pb_emit(&a, SYSCALL());

        pb_init(&b);
        pb_sys(&b, SYS_SLEEP, 10, PB_NONE, PB_NONE);
        pb_sys(&b, SYS_KILL, 1, SIGUSR1, PB_NONE);
        pb_exit(&b);

        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        go();
        CHECK(proc_table[0].state == PROC_TERMINATED && proc_table[0].exit_code == 42, "handler ran and the main loop saw its flag");
        CHECK(proc_table[1].state == PROC_TERMINATED, "sender finished");
    }

    printf("signals: default action terminates\n");
    {
        setup();
        prog_t a, b;
        pb_init(&a);  pb_emit(&a, JMP(0));
        pb_init(&b);
        pb_sys(&b, SYS_SLEEP, 5, PB_NONE, PB_NONE);
        pb_sys(&b, SYS_KILL, 1, SIGTERM, PB_NONE);
        pb_exit(&b);
        pb_spawn(&cpu, &a, 0, 1);
        pb_spawn(&cpu, &b, 0, 1);
        go();
        CHECK(proc_table[0].state == PROC_TERMINATED && proc_table[0].exit_code == -1, "unhandled SIGTERM killed the spinning process");
        CHECK(cpu.halted, "machine stopped once nothing was left");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
