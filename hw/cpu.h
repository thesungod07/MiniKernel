#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#define NREGS 8
#define RAM_WORDS 4096 
#define NVECTORS 8

typedef enum
{
    MODE_KERNEL,
    MODE_USER
} cpu_mode_t;

enum vectors
{
    VEC_TIMER,
    VEC_SYSCALL,
    VEC_PRIV_FAULT,
    VEC_ILLEGAL,
    VEC_MEM_FAULT
};

typedef struct
{
    uint32_t reg[NREGS];
    uint32_t pc;
    bool zf;
    cpu_mode_t mode;
    bool int_enabled;
    bool halted;
    uint32_t ram[RAM_WORDS];
}

#endif
