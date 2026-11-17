/*
 * cpu8 instruction table, shared by the assembler (cpu8asm) and the
 * simulator (cpu8sim). See docs/isa.md.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Every instruction is 2 bytes: [op:4 | hi:4] [lo:8]
 */
#ifndef CPU8ISA_H
#define CPU8ISA_H

#define CPU8_MEM_SIZE   4096
#define CPU8_STACK_PAGE 0xF00   /* stack lives in 0xF00-0xFFF */

/*
 * Input mailbox: a software convention, not part of the chip. Whoever owns
 * the RAM (RP2040 firmware, simulator) waits until FLAG is 0, writes the
 * next input byte to DATA and then sets FLAG to 1. The program waits for
 * FLAG != 0, reads DATA and writes 0 to FLAG. See programs/lib/input.asm.
 */
#define CPU8_MBOX_FLAG  0xEFE
#define CPU8_MBOX_DATA  0xEFF

enum cpu8_kind {
    K_IMM,    /* byte0 = 0x0h (h = function), lo = 8-bit immediate */
    K_ADDR,   /* byte0 = op<<4 | ea[11:8], lo = ea[7:0]           */
    K_ADDRX,  /* same, address operand written "ea,X"              */
    K_NONE    /* byte0 = 0xDh (h = function), lo = 0               */
};

struct cpu8_op {
    const char *name;
    unsigned char code;   /* K_IMM/K_NONE: full first byte; K_ADDR(X): opcode nibble */
    enum cpu8_kind kind;
};

static const struct cpu8_op cpu8_ops[] = {
    /* immediate group, opcode 0 */
    {"LDI", 0x00, K_IMM}, {"ADI", 0x01, K_IMM}, {"SBI", 0x02, K_IMM},
    {"ANI", 0x03, K_IMM}, {"ORI", 0x04, K_IMM}, {"XRI", 0x05, K_IMM},
    {"CPI", 0x06, K_IMM}, {"LDX", 0x07, K_IMM}, {"CPX", 0x08, K_IMM},
    /* 12-bit address */
    {"LDA", 0x1, K_ADDR}, {"STA", 0x2, K_ADDR}, {"ADD", 0x3, K_ADDR},
    {"SUB", 0x4, K_ADDR},
    {"LDA", 0x5, K_ADDRX}, {"STA", 0x6, K_ADDRX}, {"ADD", 0x7, K_ADDRX},
    {"CALL", 0x8, K_ADDR}, {"JMP", 0x9, K_ADDR}, {"JZ", 0xA, K_ADDR},
    {"JC", 0xB, K_ADDR}, {"JNZ", 0xC, K_ADDR}, {"CMP", 0xE, K_ADDR},
    {"STRLEN", 0xF, K_ADDR},
    /* misc group, opcode D, no operand */
    {"OUT", 0xD0, K_NONE}, {"IN", 0xD1, K_NONE}, {"SHL", 0xD2, K_NONE},
    {"SHR", 0xD3, K_NONE}, {"NOT", 0xD4, K_NONE}, {"HLT", 0xD5, K_NONE},
    {"TAX", 0xD6, K_NONE}, {"TXA", 0xD7, K_NONE}, {"INX", 0xD8, K_NONE},
    {"DEX", 0xD9, K_NONE}, {"PUSH", 0xDA, K_NONE}, {"POP", 0xDB, K_NONE},
    {"RET", 0xDC, K_NONE}, {"NOP", 0xDF, K_NONE},
};

#define CPU8_NOPS (sizeof cpu8_ops / sizeof cpu8_ops[0])

#endif
