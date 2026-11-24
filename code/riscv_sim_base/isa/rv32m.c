#include "rv32m.h"
#include "isa_registry.h"
#include <stdint.h>

/*
 * RV32M: all eight instructions are R-type, opcode OP (0x33), funct7 0x01,
 * told apart by funct3. Because they are FMT_R, the pipeline treats them
 * like any other ALU instruction (reads rs1/rs2, writes rd, result in EX)
 * with no changes outside this file.
 *
 * Writing these in C without undefined behaviour is the interesting part:
 *   - Signed overflow is UB in C, so the low-word product is computed on
 *     uint32_t (unsigned arithmetic wraps, and the low 32 bits of a product
 *     are the same for signed and unsigned operands).
 *   - High words come from an exact 64-bit product. int32 x int32 and
 *     int32 x uint32 both fit in int64_t; the shift is done on uint64_t
 *     because right-shifting a negative signed value is implementation-
 *     defined.
 *   - Division never traps in RISC-V. Divide-by-zero and the one overflow
 *     case (INT32_MIN / -1, which is UB in C) have spec-defined results
 *     and must be special-cased BEFORE the C division:
 *
 *        case            div       divu        rem    remu
 *        x / 0           -1        2^32 - 1    x      x
 *        INT_MIN / -1    INT_MIN   (n/a)       0      (n/a)
 *
 *   - C99 integer division truncates toward zero and % takes the sign of
 *     the dividend -- exactly RISC-V's div/rem semantics.
 */

#define OP_R        0x33
#define FUNCT7_MULDIV 0x01

static void exec_mul(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    cpu_write_reg(c, i->rd, cpu_read_reg(c, i->rs1) * cpu_read_reg(c, i->rs2));
}

static void exec_mulh(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    int64_t p = (int64_t)(int32_t)cpu_read_reg(c, i->rs1) * (int64_t)(int32_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)((uint64_t)p >> 32));
}

static void exec_mulhsu(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    int64_t p = (int64_t)(int32_t)cpu_read_reg(c, i->rs1) * (int64_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)((uint64_t)p >> 32));
}

static void exec_mulhu(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    uint64_t p = (uint64_t)cpu_read_reg(c, i->rs1) * (uint64_t)cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, (uint32_t)(p >> 32));
}

static void exec_div(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1), b = (int32_t)cpu_read_reg(c, i->rs2);
    uint32_t v;
    if (b == 0) v = 0xFFFFFFFFu;
    else if (a == INT32_MIN && b == -1) v = (uint32_t)INT32_MIN;
    else v = (uint32_t)(a / b);
    cpu_write_reg(c, i->rd, v);
}

static void exec_divu(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    uint32_t a = cpu_read_reg(c, i->rs1), b = cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, b ? a / b : 0xFFFFFFFFu);
}

static void exec_rem(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    int32_t a = (int32_t)cpu_read_reg(c, i->rs1), b = (int32_t)cpu_read_reg(c, i->rs2);
    uint32_t v;
    if (b == 0) v = (uint32_t)a;
    else if (a == INT32_MIN && b == -1) v = 0;
    else v = (uint32_t)(a % b);
    cpu_write_reg(c, i->rd, v);
}

static void exec_remu(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)m;
    uint32_t a = cpu_read_reg(c, i->rs1), b = cpu_read_reg(c, i->rs2);
    cpu_write_reg(c, i->rd, b ? a % b : a);
}

void register_rv32m(void) {
    register_instruction((InstructionHandler){"mul",    OP_R, 0x0, FUNCT7_MULDIV, FMT_R, exec_mul});
    register_instruction((InstructionHandler){"mulh",   OP_R, 0x1, FUNCT7_MULDIV, FMT_R, exec_mulh});
    register_instruction((InstructionHandler){"mulhsu", OP_R, 0x2, FUNCT7_MULDIV, FMT_R, exec_mulhsu});
    register_instruction((InstructionHandler){"mulhu",  OP_R, 0x3, FUNCT7_MULDIV, FMT_R, exec_mulhu});
    register_instruction((InstructionHandler){"div",    OP_R, 0x4, FUNCT7_MULDIV, FMT_R, exec_div});
    register_instruction((InstructionHandler){"divu",   OP_R, 0x5, FUNCT7_MULDIV, FMT_R, exec_divu});
    register_instruction((InstructionHandler){"rem",    OP_R, 0x6, FUNCT7_MULDIV, FMT_R, exec_rem});
    register_instruction((InstructionHandler){"remu",   OP_R, 0x7, FUNCT7_MULDIV, FMT_R, exec_remu});
}
