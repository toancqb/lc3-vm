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

/* trap codes */
enum
{
  TRAP_GETC = 0x20,  /* get character from keyboard */
  TRAP_OUT = 0x21,   /* output a character */
  TRAP_PUTS = 0x22,  /* output a word string */
  TRAP_IN = 0x23,    /* input a string */
  TRAP_PUTSP = 0x24, /* output a byte string */
  TRAP_HALT = 0x25   /* halt the program */
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

    switch (op) {
      case OP_ADD:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;
          /* first operand (SR1) */
          uint16_t sr1 = (instr >> 6) & 0x7;
          /* whether we are in immediate mode */
          uint16_t imm_flag = (instr >> 5) & 0x1;

          if (imm_flag) {
            uint16_t imm5 = sign_extend(instr & 0x1f, 5);
            reg[dr] = reg[sr1] + imm5;
          }
          else {
            uint16_t sr2 = instr & 0x7;
            reg[dr] = reg[sr1] + reg[sr2];
          }

          update_flags(dr);
        }
        break;
      case OP_AND:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;
          /* first operand (SR1) */
          uint16_t sr1 = (instr >> 6) & 0x7;
          /* whether we are in immediate mode */
          uint16_t imm_flag = (instr >> 5) & 0x1;

          if (imm_flag) {
            uint16_t imm5 = sign_extend(instr & 0x1f, 5);
            reg[dr] = reg[sr1] & imm5;
          }
          else {
            uint16_t sr2 = instr & 0x7;
            reg[dr] = reg[sr1] & reg[sr2];
          }
          update_flags(dr);
        }
        break;
      case OP_NOT:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;

          /* source register (SR) */
          uint16_t sr = (instr >> 6) & 0x7;

          reg[dr] = ~reg[sr];
          update_flags(dr);
        }
        break;
      case OP_BR:
        {
          uint16_t n_flag = (instr >> 11) & 0x1;
          uint16_t z_flag = (instr >> 10) & 0x1;
          uint16_t p_flag = (instr >> 9) & 0x1;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          /* advance program counter if one of the following is true:
           * - n_flag is set and negative condition flag is set
           * - z_flag is set and zero condition flag is set
           * - p_flag is set and positive condition flag is set */
          if ((n_flag && (reg[R_COND] & FL_NEG)) ||
              (z_flag && (reg[R_COND] & FL_ZRO)) ||
              (p_flag && (reg[R_COND] & FL_POS))) {

            reg[R_PC] += pc_offset;
          }
        }
        break;
      case OP_JMP:
        {
          uint16_t base_r = (instr >> 6) & 0x7;
          reg[R_PC] = reg[base_r];
        }
        break;
      case OP_JSR:
        {
          /* save PC in R7 to jump back to later */
          reg[R_R7] = reg[R_PC];

          /* whether we are in immediate mode */
          uint16_t imm_flag = (instr >> 11) & 0x1;

          if (imm_flag) {
            /* PCoffset 11 */
            uint16_t pc_offset = sign_extend(instr & 0x7ff, 11);

            /* add offset to program counter */
            reg[R_PC] += pc_offset;
          }
          else {
            /* assign contents of base register directly to program counter */
            uint16_t base_r = (instr >> 6) & 0x7;
            reg[R_PC] = reg[base_r];
          }
        }
        break;
      case OP_LD:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          /* add pc_offset to the current PC and load that memory location */
          reg[dr] = mem_read(reg[R_PC] + pc_offset);
          update_flags(dr);
        }
        break;
      case OP_LDI:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          /* add pc_offset to the current PC, look at that memory location to
           * get the final address */
          reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset));
          update_flags(dr);
        }
        break;
      case OP_LDR:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;

          /* base register (BaseR) */
          uint16_t base_r = (instr >> 6) & 0x7;

          /* offset 6 */
          uint16_t offset = sign_extend(instr & 0x3f, 6);

          reg[dr] = mem_read(reg[base_r] + offset);
          update_flags(dr);
        }
        break;
      case OP_LEA:
        {
          /* destination register (DR) */
          uint16_t dr = (instr >> 9) & 0x7;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          reg[dr] = reg[R_PC] + pc_offset;
          update_flags(dr);
        }
        break;
      case OP_ST:
        {
          /* source register (SR) */
          uint16_t sr = (instr >> 9) & 0x7;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          mem_write(reg[R_PC] + pc_offset, reg[sr]);
        }
        break;
      case OP_STI:
        {
          /* source register (SR) */
          uint16_t sr = (instr >> 9) & 0x7;

          /* PCoffset 9 */
          uint16_t pc_offset = sign_extend(instr & 0x1ff, 9);

          mem_write(mem_read(reg[R_PC] + pc_offset), reg[sr]);
        }
        break;
      case OP_STR:
        {
          /* source register (DR) */
          uint16_t sr = (instr >> 9) & 0x7;

          /* base register (BaseR) */
          uint16_t base_r = (instr >> 6) & 0x7;

          /* offset 6 */
          uint16_t offset = sign_extend(instr & 0x3f, 6);

          mem_write(reg[base_r] + offset, reg[sr]);
        }
        break;
      case OP_TRAP:
        switch (instr & 0xFF) {
          case TRAP_GETC:
            {TRAP GETC, 9}
            break;
          case TRAP_OUT:
            {TRAP OUT, 9}
            break;
          case TRAP_PUTS:
            {
              /* one char per word */
              uint16_t *c = memory + reg[R_R0];
              while (*c) {
                putc((char)*c, stdout);
                ++c;
              }
              fflush(stdout);
            }
            break;
          case TRAP_IN:
            {TRAP IN, 9}
            break;
          case TRAP_PUTSP:
            {TRAP PUTSP, 9}
            break;
          case TRAP_HALT:
            {TRAP HALT, 9}
            break;
        }
        break;
      case OP_RES:
      case OP_RTI:
      default:
        abort();
    }
  }

  {Shutdown, 12}
}
