#ifndef ISA_H
#define ISA_H

#include <stdint.h>

enum opcodes
{
    OP_NOP,
    OP_LDI,
    OP_MOV,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_CMP,
    OP_JMP,
    OP_JZ,
    OP_JNZ,
    OP_LD,
    OP_ST,
    OP_SYSCALL,
    OP_HLT,
    OP_CLI,
    OP_STI,
    OP_COUNT
};

#endif

