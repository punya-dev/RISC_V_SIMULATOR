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

/*
 * TODO(lab5): suggested helpers (write your own, names are up to you):
 *   - resolve the rounding mode: rm field (i->funct3), or cpu_frm(c) when
 *     rm == RM_DYN; rm 5/6 (or dyn with an invalid frm) is an illegal
 *     instruction.
 *   - map rm to a <fenv.h> mode and run the host operation between
 *     fesetround() / feclearexcept() / fetestexcept(); translate FE_* into
 *     FFLAG_* and accrue with cpu_raise_fflags().
 *   - RMM has no <fenv.h> mode: compute exactly in double and fix up ties.
 *   - any NaN result -> CANONICAL_NAN.
 *   - float -> int: saturate + NV on overflow / NaN, NX when inexact.
 */

/* ------------------------------------------------------------------ */
/* Handlers                                                           */
/* ------------------------------------------------------------------ */

/* Read an f register inside a handler: FREG(i->rs1). */
#define FREG(i)  cpu_read_freg(c, (int)(i))

/* ---- loads / stores: raw 32-bit moves ---- */

static void exec_flw(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

static void exec_fsw(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- arithmetic ---- */

static void exec_fadd(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fsub(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fmul(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fdiv(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fsqrt(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* Fused multiply-add family: one rounding of (+-rs1 * rs2) +- rs3.
 * Negating = flipping the sign bit, which never signals. */
static void exec_fmadd(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fmsub(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fnmsub(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fnmadd(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- min / max ---- */

static void exec_fmin(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fmax(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- sign injection (pure bit operations, no flags, no NaN rules) ---- */

static void exec_fsgnj(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fsgnjn(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fsgnjx(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- comparisons (result in an x register) ---- */

static void exec_feq(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_flt(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fle(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- conversions ---- */

static void exec_fcvt_w_s(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fcvt_wu_s(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

static void exec_fcvt_s_w(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fcvt_s_wu(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- moves between register files (bits unchanged) ---- */

static void exec_fmv_x_w(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}
static void exec_fmv_w_x(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
}

/* ---- classify: exactly one bit set ---- */

static void exec_fclass(CPUState *c, Memory *m, DecodedInstr *i) {
    (void)c; (void)m; (void)i;
    /* TODO(lab5) */
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
