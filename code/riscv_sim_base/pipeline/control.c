#include "control.h"
#include <stdio.h>
#include <string.h>

/* x0 is hardwired to zero, so reading it never depends on an older
 * instruction and writing it never produces a value anyone can consume.
 * Mapping it to REG_NONE here means hazard logic never has to special-case it. */
static inline int reg_or_none(uint32_t r) {
    return (r == 0) ? REG_NONE : (int)r;
}

/* Operand in the x or f register file depending on the handler's flag. */
static inline int operand(uint32_t r, uint32_t flags, uint32_t fp_flag) {
    if (flags & fp_flag) return REG_FP_BASE + (int)(r & 0x1F);
    return reg_or_none(r);
}

uint32_t unified_read(const CPUState *cpu, int r) {
    return REG_IS_FP(r) ? cpu_read_freg(cpu, REG_INDEX(r)) : cpu_read_reg(cpu, r);
}

void unified_write(CPUState *cpu, int r, uint32_t v) {
    if (REG_IS_FP(r)) cpu_write_freg(cpu, REG_INDEX(r), v);
    else              cpu_write_reg(cpu, r, v);
}

const char *unified_name(int r) {
    static char buf[8];
    snprintf(buf, sizeof(buf), "%c%d", REG_IS_FP(r) ? 'f' : 'x', REG_INDEX(r));
    return buf;
}

/* RISC-V load/store funct3: bits [1:0] = log2(size), bit 2 = unsigned (loads). */
static inline void set_mem_width(Control *c, uint32_t funct3) {
    c->mem_size = 1 << (funct3 & 0x3);
    c->mem_unsigned = (funct3 & 0x4) ? 1 : 0;
}

void control_from_instr(const InstructionHandler *h, const DecodedInstr *d, Control *out) {
    memset(out, 0, sizeof(*out));
    out->src1 = out->src2 = out->src3 = out->dst = REG_NONE;
    (void)h; (void)d;
    /* TODO(lab1/lab5): fill *out from the instruction format (h->format)
     * and the FP operand flags (isa_flags(h): IF_RD_FP, IF_RS1_FP, ...):
     *   src1 / src2 / src3 / dst   unified register numbers (REG_NONE if unused;
     *                              x0 is always REG_NONE; f regs = REG_FP_BASE + n)
     *   mem_op, mem_size, mem_unsigned   for loads / stores (incl. flw / fsw)
     *   is_branch, is_jump, is_ecall
     * The helpers above (reg_or_none, operand, set_mem_width) do the fiddly
     * parts. Do not forget FMT_R2, FMT_R4, FMT_CSR and FMT_CSRI. */
}
