# Machine Design

## Registers
- 8 registers, R0 to R7
- Each is 32 unsigned bits
- Syscall convention: R0 = syscall number, R1 = argument
## Memory
- RAM: 4096 words, each 32 bits ; an address is an index into the array
- 0x000-0x0FF: Kernel-reserved
- 0x100: User programs load here
## Instruction format (32 bits)
- Bits 31-24: opcode
- Bits 23-16: register (0-7)
- Bits 15-0: immediate (value, address, or second register number)
- Two-register instructions: second register is the low bits of the immediate
## Instruction Set
#	Name | What it does
0	NOP | does nothing
1	LDI r, value | put the number value into register r
2	MOV a, b | copy register b into register a
3	ADD a, b | a = a + b
4	SUB a, b | a = a - b
5	MUL a, b | a = a * b
6	CMP a, b | set the zero flag if a equals b
7	JMP addr | set PC to addr
8	JZ addr | set PC to addr only if the zero flag is set
9	JNZ addr | set PC to addr only if the zero flag is not set
10	LD r, addr | copy the word at memory address addr into r
11	ST r, addr | copy register r into memory address addr
12	SYSCALL | trap into the kernel
13	HLT | halt the CPU (privileged)
14	CLI | disable interrupts (privileged)
15	STI | enable interrupts (privileged)
