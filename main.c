#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/* 65536 locations */
uint16_t memory[UINT16_MAX];

enum {
  R_R0 = 0,
  R_R1,
  R_R2,
  R_R3,
  R_R4,
  R_R5,
  R_R6,
  R_R7,
  R_PC, /* program counter */
  R_COND,
  R_COUNT
};

uint16_t reg[R_COUNT];

/* op codes */
enum {
  OP_BR = 0, /* branch */
  OP_ADD,    /* add  */
  OP_LD,     /* load */
  OP_ST,     /* store */
  OP_JSR,    /* jump register */
  OP_AND,    /* bitwise and */
  OP_LDR,    /* load register */
  OP_STR,    /* store register */
  OP_RTI,    /* unused */
  OP_NOT,    /* bitwise not */
  OP_LDI,    /* load indirect */
  OP_STI,    /* store indirect */
  OP_JMP,    /* jump */
  OP_RES,    /* reserved (unused) */
  OP_LEA,    /* load effective address */
  OP_TRAP    /* execute trap */
};

/* condition flags */
enum {
  FL_POS = 1 << 0, /* P */
  FL_ZRO = 1 << 1, /* Z */
  FL_NEG = 1 << 2, /* N */
};

/* convert small bit number to 16-bit, keeping sign bit intact */
uint16_t sign_extend(uint16_t x, int bit_count) {
  if ((x >> (bit_count - 1)) & 1) {
    x |= (0xFFFF << bit_count);
  }
  return x;
}

/* update condition flags based on outcome of result (negative, zero, or
 * positive */
void update_flags(uint16_t r) {
  if (reg[r] == 0) {
    reg[R_COND] = FL_ZRO;
  }
  else if (reg[r] >> 15) { /* a 1 in the left-most bit indicates negative */
    reg[R_COND] = FL_NEG;
  }
  else {
    reg[R_COND] = FL_POS;
  }
}

int main(int argc, const char* argv[]) {
  {Load Arguments, 12}
  {Setup, 12}

  /* set the PC to starting position */
  /* 0x3000 is the default */
  enum { PC_START = 0x3000 };
  reg[R_PC] = PC_START;

  int running = 1;
  while (running)
  {
    /* FETCH */
    uint16_t instr = mem_read(reg[R_PC]++);
    uint16_t op = instr >> 12;

    switch (op)
    {
      case OP_ADD:
        {
          /* destination register (DR) */
          uint16_t r0 = (instr >> 9) & 0x7;
          /* first operand (SR1) */
          uint16_t r1 = (instr >> 6) & 0x7;
          /* whether we are in immediate mode */
          uint16_t imm_flag = (instr >> 5) & 0x1;

          if (imm_flag) {
            uint16_t imm5 = sign_extend(instr & 0x1F, 5);
            reg[r0] = reg[r1] + imm5;
          }
          else {
            uint16_t r2 = instr & 0x7;
            reg[r0] = reg[r1] + reg[r2];
          }

          update_flags(r0);
        }
        break;
      case OP_AND:
        {AND, 7}
        break;
      case OP_NOT:
        {NOT, 7}
        break;
      case OP_BR:
        {BR, 7}
        break;
      case OP_JMP:
        {JMP, 7}
        break;
      case OP_JSR:
        {JSR, 7}
        break;
      case OP_LD:
        {LD, 7}
        break;
      case OP_LDI:
        {
          /* destination register (DR) */
          uint16_t r0 = (instr >> 9) & 0x7;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          /* add pc_offset to the current PC, look at that memory location to
           * get the final address */
          reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
          update_flags(r0);
        }
        break;
      case OP_LDR:
        {LDR, 7}
        break;
      case OP_LEA:
        {LEA, 7}
        break;
      case OP_ST:
        {ST, 7}
        break;
      case OP_STI:
        {STI, 7}
        break;
      case OP_STR:
        {STR, 7}
        break;
      case OP_TRAP:
        {TRAP, 8}
        break;
      case OP_RES:
      case OP_RTI:
      default:
        {BAD OPCODE, 7}
        break;
    }
  }

  {Shutdown, 12}
}