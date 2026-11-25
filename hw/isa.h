#ifndef ISA_H
#define ISA_H
#include <stdint.h>

/* instruction word: [ opcode:8 | register:8 | immediate:16 ] */
enum opcodes
{
    OP_NOP, OP_LDI, OP_MOV, OP_ADD, OP_SUB, OP_MUL, OP_CMP,
    OP_JMP, OP_JZ, OP_JNZ, OP_LD, OP_ST,
    OP_SYSCALL, OP_HLT, OP_CLI, OP_STI,
    OP_COUNT
};

#define ENC(op, ra, imm) \
    ((((uint32_t)(op) << 24) | \
      (((uint32_t)(ra) & 0xFF) << 16) | \
      ((uint32_t)(imm) & 0xFFFF)))

#define NOP()        ENC(OP_NOP, 0, 0)
#define LDI(r, v)    ENC(OP_LDI, r, v)
#define MOV(a, b)    ENC(OP_MOV, a, b)
#define ADD(a, b)    ENC(OP_ADD, a, b)
#define SUB(a, b)    ENC(OP_SUB, a, b)
#define MUL(a, b)    ENC(OP_MUL, a, b)
#define CMP(a, b)    ENC(OP_CMP, a, b)
#define JMP(addr)    ENC(OP_JMP, 0, addr)
#define JZ(addr)     ENC(OP_JZ, 0, addr)
#define JNZ(addr)    ENC(OP_JNZ, 0, addr)
#define LD(r, addr)  ENC(OP_LD, r, addr)
#define ST(r, addr)  ENC(OP_ST, r, addr)
#define SYSCALL()    ENC(OP_SYSCALL, 0, 0)
#define HLT()        ENC(OP_HLT, 0, 0)
#define CLI()        ENC(OP_CLI, 0, 0)
#define STI()        ENC(OP_STI, 0, 0)
#endif
