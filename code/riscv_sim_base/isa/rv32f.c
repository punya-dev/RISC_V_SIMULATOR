#include "rv32f.h"
#include "isa_registry.h"

#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/*
 * RV32F on top of the host's IEEE 754 hardware.
 *
 * The register file holds raw bit patterns (uint32_t). A value only
 * becomes a C `float` for the duration of an arithmetic operation, via
 * memcpy (never a pointer cast: that would break strict aliasing).
 *
 * Arithmetic (add/sub/mul/div/sqrt/fma, int->float) runs on the host FPU
 * with the rounding mode set through <fenv.h>, and the host's exception
 * flags are translated into fcsr.fflags. That gives correctly rounded
 * results and exact flags, including subnormals and underflow (x86 SSE,
 * like RISC-V, detects tininess after rounding).
 *
 * Everything where RISC-V's rules differ from what C/the host does is
 * done by hand on the bits:
 *   - NaN results are always the canonical NaN 0x7fc00000 (x86 would
 *     produce 0xffc00000; C makes no promise at all).
 *   - fmin/fmax: a NaN operand is ignored if the other is a number;
 *     -0 < +0; signalling NaNs raise NV.
 *   - feq is quiet (NV only for signalling NaNs); flt/fle signal NV for
 *     any NaN.
 *   - float->int conversions saturate and raise NV when out of range or
 *     NaN (C's cast is undefined behaviour there).
 *   - fsgnj*, fmv, fclass, flw/fsw never touch a float at all.
 *   - Round-to-nearest-max-magnitude (RMM) has no <fenv.h> equivalent.
 *     It only differs from round-to-nearest-even when the exact result
 *     lies exactly halfway between two floats; see d2f() and fp_arith().
 *   - inf * 0 in a fused multiply-add raises NV even when the addend is a
 *     quiet NaN (IEEE leaves that case to the implementation; RISC-V
 *     requires it).
 *
 * Rounding mode: the instruction's rm field (funct3), or fcsr.frm when
 * rm = 7 (dyn). rm values 5 and 6, or dyn with an invalid frm, make the
 * instruction illegal.
 */

#define OP_LOAD_FP  0x07
#define OP_STORE_FP 0x27
#define OP_FP       0x53
#define OP_FMADD    0x43
#define OP_FMSUB    0x47
#define OP_FNMSUB   0x4B
#define OP_FNMADD   0x4F

#define CANONICAL_NAN 0x7fc00000u
#define SIGN_BIT      0x80000000u

/* ------------------------------------------------------------------ */
/* Helpers you may use (static inline: no warning while unused)        */
/* ------------------------------------------------------------------ */

static inline float f_of(uint32_t bits) { float f; memcpy(&f, &bits, sizeof f); return f; }
static inline uint32_t bits_of(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static inline int is_nan_bits(uint32_t b)  { return (b & 0x7f800000u) == 0x7f800000u && (b & 0x007fffffu) != 0; }
static inline int is_snan_bits(uint32_t b) { return is_nan_bits(b) && (b & 0x00400000u) == 0; }

static int get_rm(CPUState *c, DecodedInstr *i) {
    uint32_t rm = i->funct3;
    if (rm == RM_DYN) rm = cpu_frm(c);
    if (rm > RM_RMM) {
        c->status = CPU_ILLEGAL_INSTRUCTION;
        c->fault_addr = c->pc;
        snprintf(c->fault_msg, sizeof(c->fault_msg), "invalid rounding mode %u at pc=0x%08x", rm, c->pc);
        return -1;
    }
    return (int)rm;
}

static void fe_begin(int rm) {
    static const int modes[] = {FE_TONEAREST, FE_TOWARDZERO, FE_DOWNWARD, FE_UPWARD, FE_TONEAREST};
    fesetround(modes[rm]);
    feclearexcept(FE_ALL_EXCEPT);
}

static void fe_end(CPUState *c) {
    int e = fetestexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
    uint32_t f = 0;
    if (e & FE_INVALID)   f |= FFLAG_NV;
    if (e & FE_DIVBYZERO) f |= FFLAG_DZ;
    if (e & FE_OVERFLOW)  f |= FFLAG_OF;
    if (e & FE_UNDERFLOW) f |= FFLAG_UF;
    if (e & FE_INEXACT)   f |= FFLAG_NX;
    cpu_raise_fflags(c, f);
}

static uint32_t canon(float r) { return isnan(r) ? CANONICAL_NAN : bits_of(r); }

/* Round an exactly computed double to float, ties to max magnitude. */
static float d2f_rmm(double d) {
    float f = (float)d;
    if ((double)f == d || isinf(f) || isnan(f)) return f;
    uint32_t b = bits_of(f), g;
    if (f == 0.0f) g = (d < 0 ? SIGN_BIT : 0) | 1u;
    else g = fabs(d) > fabs((double)f) ? b + 1 : b - 1;
    float fg = f_of(g);
    if (fabs((double)fg - d) == fabs((double)f - d) && fabsf(fg) > fabsf(f)) return fg;
    return f;
}

typedef enum { F_ADD, F_SUB, F_MUL, F_DIV, F_SQRT } FOp;

static void fp_arith(CPUState *c, DecodedInstr *i, FOp op) {
    int rm = get_rm(c, i);
    if (rm < 0) return;
    float a = f_of(cpu_read_freg(c, (int)i->rs1)), b = f_of(cpu_read_freg(c, (int)i->rs2));
    volatile float r;
    fe_begin(rm);
    if (rm == RM_RMM && op <= F_MUL) {
        double da = a, db = b;
        volatile double d = op == F_ADD ? da + db : op == F_SUB ? da - db : da * db;
        r = d2f_rmm(d);
    } else {
        switch (op) {
            case F_ADD:  r = a + b; break;
            case F_SUB:  r = a - b; break;
            case F_MUL:  r = a * b; break;
            case F_DIV:  r = a / b; break;
            default:     r = sqrtf(a); break;
        }
    }
    fe_end(c);
    cpu_write_freg(c, (int)i->rd, canon(r));
}

static void fp_fma(CPUState *c, DecodedInstr *i, int neg_prod, int neg_add) {
    int rm = get_rm(c, i);
    if (rm < 0) return;
    float a = f_of(cpu_read_freg(c, (int)i->rs1) ^ (neg_prod ? SIGN_BIT : 0));
    float b = f_of(cpu_read_freg(c, (int)i->rs2));
    float z = f_of(cpu_read_freg(c, (int)i->rs3) ^ (neg_add ? SIGN_BIT : 0));
    volatile float r;
    fe_begin(rm);
    if (rm == RM_RMM) r = d2f_rmm(fma((double)a, (double)b, (double)z));
    else r = fmaf(a, b, z);
    fe_end(c);
    if ((isinf(a) && b == 0.0f) || (a == 0.0f && isinf(b))) cpu_raise_fflags(c, FFLAG_NV);
    cpu_write_freg(c, (int)i->rd, canon(r));
}

static void fp_minmax(CPUState *c, DecodedInstr *i, int is_max) {
    uint32_t a = cpu_read_freg(c, (int)i->rs1), b = cpu_read_freg(c, (int)i->rs2), r;
    if (is_snan_bits(a) || is_snan_bits(b)) cpu_raise_fflags(c, FFLAG_NV);
    if (is_nan_bits(a) && is_nan_bits(b)) r = CANONICAL_NAN;
    else if (is_nan_bits(a)) r = b;
    else if (is_nan_bits(b)) r = a;
    else if (f_of(a) == f_of(b)) r = is_max ? (a & b) : (a | b);
    else r = ((f_of(a) < f_of(b)) != is_max) ? a : b;
    cpu_write_freg(c, (int)i->rd, r);
}

enum { CMP_EQ, CMP_LT, CMP_LE };

static void fp_cmp(CPUState *c, DecodedInstr *i, int kind) {
    uint32_t a = cpu_read_freg(c, (int)i->rs1), b = cpu_read_freg(c, (int)i->rs2), r = 0;
    if (is_nan_bits(a) || is_nan_bits(b)) {
        if (kind != CMP_EQ || is_snan_bits(a) || is_snan_bits(b)) cpu_raise_fflags(c, FFLAG_NV);
    } else {
        float fa = f_of(a), fb = f_of(b);
        r = kind == CMP_EQ ? fa == fb : kind == CMP_LT ? fa < fb : fa <= fb;
    }
    cpu_write_reg(c, (int)i->rd, r);
}

static void fp_to_int(CPUState *c, DecodedInstr *i, int is_unsigned) {
    int rm = get_rm(c, i);
    if (rm < 0) return;
    uint32_t ab = cpu_read_freg(c, (int)i->rs1), r, flags = 0;
    float a = f_of(ab);
    if (is_nan_bits(ab)) {
        r = is_unsigned ? 0xFFFFFFFFu : 0x7FFFFFFFu;
        flags = FFLAG_NV;
    } else {
        volatile float t;
        if (rm == RM_RMM) t = roundf(a);
        else { fe_begin(rm); t = rintf(a); fesetround(FE_TONEAREST); }
        if (is_unsigned) {
            if (t < 0.0f)                  { r = 0;           flags = FFLAG_NV; }
            else if (t >= 4294967296.0f)   { r = 0xFFFFFFFFu; flags = FFLAG_NV; }
            else                           { r = (uint32_t)t; flags = (t != a) ? FFLAG_NX : 0; }
        } else {
            if (t < -2147483648.0f)        { r = 0x80000000u; flags = FFLAG_NV; }
            else if (t >= 2147483648.0f)   { r = 0x7FFFFFFFu; flags = FFLAG_NV; }
            else                           { r = (uint32_t)(int32_t)t; flags = (t != a) ? FFLAG_NX : 0; }
        }
    }
    cpu_raise_fflags(c, flags);
    cpu_write_reg(c, (int)i->rd, r);
}

static void int_to_fp(CPUState *c, DecodedInstr *i, int is_unsigned) {
    int rm = get_rm(c, i);
    if (rm < 0) return;
    uint32_t x = cpu_read_reg(c, (int)i->rs1);
    volatile float r;
    fe_begin(rm);
    if (rm == RM_RMM) r = d2f_rmm(is_unsigned ? (double)x : (double)(int32_t)x);
    else r = is_unsigned ? (float)x : (float)(int32_t)x;
    fe_end(c);
    cpu_write_freg(c, (int)i->rd, bits_of(r));
}

/* ------------------------------------------------------------------ */
/* Handlers                                                           */
/* ------------------------------------------------------------------ */

/* Read an f register inside a handler: FREG(i->rs1). */
#define FREG(i)  cpu_read_freg(c, (int)(i))

static void mem_fault(CPUState *c, uint32_t addr, const char *what) {
    c->status = CPU_MEM_FAULT;
    c->fault_addr = addr;
    snprintf(c->fault_msg, sizeof(c->fault_msg), "%s out of bounds at address 0x%08x", what, addr);
}

/* ---- loads / stores: raw 32-bit moves ---- */

static void exec_flw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, (int)i->rs1) + (uint32_t)i->imm;
    uint32_t v;
    if (memory_read_word(m, addr, &v) != MEM_OK) { mem_fault(c, addr, "flw"); return; }
    cpu_write_freg(c, (int)i->rd, v);
}

static void exec_fsw(CPUState *c, Memory *m, DecodedInstr *i) {
    uint32_t addr = cpu_read_reg(c, (int)i->rs1) + (uint32_t)i->imm;
    if (memory_write_word(m, addr, FREG(i->rs2)) != MEM_OK) mem_fault(c, addr, "fsw");
}

/* ---- arithmetic ---- */

static void exec_fadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_arith(c, i, F_ADD); }
static void exec_fsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_arith(c, i, F_SUB); }
static void exec_fmul(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_arith(c, i, F_MUL); }
static void exec_fdiv(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_arith(c, i, F_DIV); }
static void exec_fsqrt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_arith(c, i, F_SQRT); }

/* Fused multiply-add family: one rounding of (+-rs1 * rs2) +- rs3.
 * Negating = flipping the sign bit, which never signals. */
static void exec_fmadd(CPUState *c, Memory *m, DecodedInstr *i)  { (void)m; fp_fma(c, i, 0, 0); }
static void exec_fmsub(CPUState *c, Memory *m, DecodedInstr *i)  { (void)m; fp_fma(c, i, 0, 1); }
static void exec_fnmsub(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_fma(c, i, 1, 0); }
static void exec_fnmadd(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_fma(c, i, 1, 1); }

/* ---- min / max ---- */

static void exec_fmin(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_minmax(c, i, 0); }
static void exec_fmax(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_minmax(c, i, 1); }

/* ---- sign injection (pure bit operations, no flags, no NaN rules) ---- */

static void exec_fsgnj(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, (FREG(i->rs1) & ~SIGN_BIT) | (FREG(i->rs2) & SIGN_BIT));
}
static void exec_fsgnjn(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, (FREG(i->rs1) & ~SIGN_BIT) | (~FREG(i->rs2) & SIGN_BIT));
}
static void exec_fsgnjx(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, FREG(i->rs1) ^ (FREG(i->rs2) & SIGN_BIT));
}

/* ---- comparisons (result in an x register) ---- */

static void exec_feq(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_cmp(c, i, CMP_EQ); }
static void exec_flt(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_cmp(c, i, CMP_LT); }
static void exec_fle(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_cmp(c, i, CMP_LE); }

/* ---- conversions ---- */

static void exec_fcvt_w_s(CPUState *c, Memory *m, DecodedInstr *i)  { (void)m; fp_to_int(c, i, 0); }
static void exec_fcvt_wu_s(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; fp_to_int(c, i, 1); }

static void exec_fcvt_s_w(CPUState *c, Memory *m, DecodedInstr *i)  { (void)m; int_to_fp(c, i, 0); }
static void exec_fcvt_s_wu(CPUState *c, Memory *m, DecodedInstr *i) { (void)m; int_to_fp(c, i, 1); }

/* ---- moves between register files (bits unchanged) ---- */

static void exec_fmv_x_w(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_reg(c, (int)i->rd, FREG(i->rs1));
}
static void exec_fmv_w_x(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    cpu_write_freg(c, (int)i->rd, cpu_read_reg(c, (int)i->rs1));
}

/* ---- classify: exactly one bit set ---- */

static void exec_fclass(CPUState *c, Memory *m, DecodedInstr *i) { (void)m;
    uint32_t b = FREG(i->rs1), e = (b >> 23) & 0xFF, man = b & 0x7FFFFF;
    int neg = (b & SIGN_BIT) != 0, bit;
    if (e == 0xFF)   bit = man ? ((man & 0x400000) ? 9 : 8) : (neg ? 0 : 7);
    else if (e == 0) bit = man ? (neg ? 2 : 5) : (neg ? 3 : 4);
    else             bit = neg ? 1 : 6;
    cpu_write_reg(c, (int)i->rd, 1u << bit);
}

/* ------------------------------------------------------------------ */
/* Registration                                                       */
/* ------------------------------------------------------------------ */

#define FFF (IF_RD_FP | IF_RS1_FP | IF_RS2_FP)   /* fd, fs1, fs2 */

static void reg(const char *mn, uint32_t opcode, int32_t f3, int32_t f7, InstrFormat fmt,
                ExecuteFn fn, uint32_t flags, uint32_t rs2_sel) {
    register_instruction_ex((InstructionHandler){mn, opcode, f3, f7, fmt, fn}, flags, rs2_sel);
}

void register_rv32f(void) {
    reg("flw",       OP_LOAD_FP,  0x2, -1,   FMT_I_LOAD, exec_flw,  IF_RD_FP,  0);
    reg("fsw",       OP_STORE_FP, 0x2, -1,   FMT_S,      exec_fsw,  IF_RS2_FP, 0);

    reg("fadd.s",    OP_FP, -1, 0x00, FMT_R,  exec_fadd,  FFF | IF_HAS_RM, 0);
    reg("fsub.s",    OP_FP, -1, 0x04, FMT_R,  exec_fsub,  FFF | IF_HAS_RM, 0);
    reg("fmul.s",    OP_FP, -1, 0x08, FMT_R,  exec_fmul,  FFF | IF_HAS_RM, 0);
    reg("fdiv.s",    OP_FP, -1, 0x0C, FMT_R,  exec_fdiv,  FFF | IF_HAS_RM, 0);
    reg("fsqrt.s",   OP_FP, -1, 0x2C, FMT_R2, exec_fsqrt, IF_RD_FP | IF_RS1_FP | IF_HAS_RM | IF_RS2_FIXED, 0);

    reg("fsgnj.s",   OP_FP, 0x0, 0x10, FMT_R, exec_fsgnj,  FFF, 0);
    reg("fsgnjn.s",  OP_FP, 0x1, 0x10, FMT_R, exec_fsgnjn, FFF, 0);
    reg("fsgnjx.s",  OP_FP, 0x2, 0x10, FMT_R, exec_fsgnjx, FFF, 0);
    reg("fmin.s",    OP_FP, 0x0, 0x14, FMT_R, exec_fmin,   FFF, 0);
    reg("fmax.s",    OP_FP, 0x1, 0x14, FMT_R, exec_fmax,   FFF, 0);

    reg("feq.s",     OP_FP, 0x2, 0x50, FMT_R, exec_feq, IF_RS1_FP | IF_RS2_FP, 0);
    reg("flt.s",     OP_FP, 0x1, 0x50, FMT_R, exec_flt, IF_RS1_FP | IF_RS2_FP, 0);
    reg("fle.s",     OP_FP, 0x0, 0x50, FMT_R, exec_fle, IF_RS1_FP | IF_RS2_FP, 0);

    reg("fcvt.w.s",  OP_FP, -1, 0x60, FMT_R2, exec_fcvt_w_s,  IF_RS1_FP | IF_HAS_RM | IF_RS2_FIXED, 0);
    reg("fcvt.wu.s", OP_FP, -1, 0x60, FMT_R2, exec_fcvt_wu_s, IF_RS1_FP | IF_HAS_RM | IF_RS2_FIXED, 1);
    reg("fcvt.s.w",  OP_FP, -1, 0x68, FMT_R2, exec_fcvt_s_w,  IF_RD_FP  | IF_HAS_RM | IF_RS2_FIXED, 0);
    reg("fcvt.s.wu", OP_FP, -1, 0x68, FMT_R2, exec_fcvt_s_wu, IF_RD_FP  | IF_HAS_RM | IF_RS2_FIXED, 1);

    reg("fmv.x.w",   OP_FP, 0x0, 0x70, FMT_R2, exec_fmv_x_w, IF_RS1_FP | IF_RS2_FIXED, 0);
    reg("fclass.s",  OP_FP, 0x1, 0x70, FMT_R2, exec_fclass,  IF_RS1_FP | IF_RS2_FIXED, 0);
    reg("fmv.w.x",   OP_FP, 0x0, 0x78, FMT_R2, exec_fmv_w_x, IF_RD_FP  | IF_RS2_FIXED, 0);
    /* Older spellings of the two moves (accepted by RARS too). Registered
     * after the official names, so decoding finds those first. */
    reg("fmv.x.s",   OP_FP, 0x0, 0x70, FMT_R2, exec_fmv_x_w, IF_RS1_FP | IF_RS2_FIXED, 0);
    reg("fmv.s.x",   OP_FP, 0x0, 0x78, FMT_R2, exec_fmv_w_x, IF_RD_FP  | IF_RS2_FIXED, 0);

    /* R4: funct7 holds rs3 and the 2-bit format field (00 = single), so
     * it is "don't care" for the registry. */
    reg("fmadd.s",   OP_FMADD,  -1, -1, FMT_R4, exec_fmadd,  FFF | IF_RS3_FP | IF_HAS_RM, 0);
    reg("fmsub.s",   OP_FMSUB,  -1, -1, FMT_R4, exec_fmsub,  FFF | IF_RS3_FP | IF_HAS_RM, 0);
    reg("fnmsub.s",  OP_FNMSUB, -1, -1, FMT_R4, exec_fnmsub, FFF | IF_RS3_FP | IF_HAS_RM, 0);
    reg("fnmadd.s",  OP_FNMADD, -1, -1, FMT_R4, exec_fnmadd, FFF | IF_RS3_FP | IF_HAS_RM, 0);
}
