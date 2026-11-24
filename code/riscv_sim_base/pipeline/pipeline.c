#include "pipeline.h"
#include "decoder.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define OPC_BRANCH 0x63
#define OPC_JAL    0x6F

/*
 * The stages are evaluated in REVERSE order (WB, MEM, EX, ID, IF) each
 * cycle, updating the latches in place. Going backwards means every stage
 * reads its input latch before the stage behind it overwrites that latch
 * with next cycle's value -- the software equivalent of all the pipeline
 * registers clocking at the same instant.
 *
 * Lab 2: the hazard unit and the forwarding muxes need the latch contents
 * as they were at the START of the cycle (the MEM/WB latch, for one, has
 * already been overwritten by the time EX runs). pipeline_step() therefore
 * takes a snapshot `cur` first; hazard decisions and forwarded values come
 * from it.
 */

/* ------------------------------------------------------------------ */
/* Faults                                                             */
/* ------------------------------------------------------------------ */

static inline void fault_clear(PipeFault *f) {
    f->status = CPU_RUNNING;
    f->addr = 0;
    f->msg[0] = '\0';
}

static inline void fault_set(PipeFault *f, CPUStatus status, uint32_t addr, const char *fmt, ...) {
    f->status = status;
    f->addr = addr;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->msg, sizeof(f->msg), fmt, ap);
    va_end(ap);
}

static inline int has_fault(const PipeFault *f) {
    return f->status != CPU_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */

void pipeline_reset(PipelineState *p, uint32_t start_pc, const PipelineConfig *cfg) {
    memset(p, 0, sizeof(*p)); /* every latch starts as a bubble (valid = 0) */
    p->fetch_pc = start_pc;
    p->cfg = *cfg;
    if (!p->cfg.hazard) p->cfg.hazard = &hazard_forward;
    if (!p->cfg.predictor) p->cfg.predictor = &bp_2bit;
    p->cfg.predictor->reset(&p->bp);
}

/* ------------------------------------------------------------------ */
/* WB                                                                 */
/* ------------------------------------------------------------------ */

/* Returns 1 if the machine stopped (fault or exit) this cycle. */
static int stage_wb(PipelineState *p, CPUState *cpu) {
    (void)p; (void)cpu;
    /* TODO(lab1): if MEM/WB holds a valid instruction: raise its fault (copy
     * into cpu->status / fault_addr / fault_msg, set cpu->pc, return 1), or
     * write wb_val to ctrl.dst (unified_write), count stats.retired, and on
     * exit_requested set CPU_ECALL_EXIT + cpu->pc and return 1. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* MEM                                                                */
/* ------------------------------------------------------------------ */

static inline uint32_t extend_load(uint32_t v, int size, int is_unsigned) {
    if (size == 1) return is_unsigned ? (v & 0xFF)   : (uint32_t)(int32_t)(int8_t)v;
    if (size == 2) return is_unsigned ? (v & 0xFFFF) : (uint32_t)(int32_t)(int16_t)v;
    return v;
}

static inline MemStatus mem_load(const Memory *mem, uint32_t addr, int size, uint32_t *out) {
    MemStatus s;
    if (size == 1) { uint8_t b;  s = memory_read_byte(mem, addr, &b); *out = b; }
    else if (size == 2) { uint16_t h; s = memory_read_half(mem, addr, &h); *out = h; }
    else s = memory_read_word(mem, addr, out);
    return s;
}

static inline MemStatus mem_store(Memory *mem, uint32_t addr, int size, uint32_t val) {
    if (size == 1) return memory_write_byte(mem, addr, (uint8_t)val);
    if (size == 2) return memory_write_half(mem, addr, (uint16_t)val);
    return memory_write_word(mem, addr, val);
}

/* Returns 1 if this instruction faulted here (younger instructions must
 * then be squashed and fetch stopped). */
static int stage_mem(PipelineState *p, Memory *mem) {
    (void)p; (void)mem;
    /* TODO(lab1): copy EX/MEM into MEM/WB (wb_val = alu_result); perform the
     * load (mem_load + extend_load) or store (mem_store) of ctrl.mem_size
     * bytes at alu_result; on an out-of-bounds access fault_set() the
     * MEM/WB fault and return 1. */
    p->mem_wb.valid = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* EX                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int      redirect;   /* PC must change to `target`: squash IF and ID */
    uint32_t target;
    int      stop;       /* exit or fault on the correct path: squash IF and ID, stop fetching */
} ExOutcome;

/* The forwarding mux in front of each ALU input. */
static inline uint32_t forward_mux(uint32_t from_regfile, ForwardSel sel, const PipelineState *cur,
                            PipelineStats *stats) {
    (void)sel; (void)cur; (void)stats;
    /* TODO(lab2): FWD_EX_MEM -> cur->ex_mem.alu_result, FWD_MEM_WB ->
     * cur->mem_wb.wb_val (count stats->forwards), else the register file. */
    return from_regfile;
}

static void stage_ex(PipelineState *p, CPUState *cpu, Memory *mem, const PipelineState *cur,
                     const HazardDecision *hz, ExOutcome *o) {
    (void)p; (void)cpu; (void)mem; (void)cur; (void)hz; (void)o;
    /* TODO(lab1..lab3, lab5):
     *   copy ID/EX into EX/MEM; an instruction carrying a fault sets o->stop;
     *   operands = forward_mux(srcN_val, hz->fwdN, ...);
     *   load/store: alu_result = rs1 + imm, store_val = rs2 (no handler call);
     *   otherwise run d->handler->execute() on a scratch CPUState holding
     *   only the operands (+ pc, next_pc = pc + 4, and cpu->fcsr), read the
     *   result back from ctrl.dst, copy the scratch fcsr back to cpu->fcsr;
     *   ecall exit -> exit_requested + o->stop; handler fault -> fault_set +
     *   o->stop;
     *   branches: count stats.branches / branch_mispredicts and call
     *   p->cfg.predictor->update(); jumps: count stats.jal / stats.jalr;
     *   next_pc != d->pred_next -> o->redirect, o->target, stats.redirects. */
    p->ex_mem.valid = 0;
}

/* ------------------------------------------------------------------ */
/* ID                                                                 */
/* ------------------------------------------------------------------ */

/* `squash`: discard the instruction in IF/ID. `wrong_path`: the reason is a
 * PC redirect (counted in stats.squashed), not an exit/fault draining.
 * `stall`: the hazard unit says wait -- IF/ID is left untouched (IF holds
 * it too) and a bubble goes into EX. */
static void stage_id(PipelineState *p, const CPUState *cpu, int squash, int wrong_path, int stall) {
    (void)p; (void)cpu; (void)squash; (void)wrong_path; (void)stall;
    /* TODO(lab1): squash (count stats.squashed when wrong_path) or stall ->
     * bubble into ID/EX; otherwise decode_instruction() (illegal ->
     * fault_set), control_from_instr(), and read src1/src2/src3 from the
     * register file (unified_read). */
    p->id_ex.valid = 0;
}

/* ------------------------------------------------------------------ */
/* IF                                                                 */
/* ------------------------------------------------------------------ */

/* IF-stage pre-decoder: just enough of the fetched word to find branch and
 * jal targets (in hardware: a few gates, or a BTB lookup). */
static inline int32_t imm_b(uint32_t raw) {
    int32_t v = (int32_t)((((raw >> 31) & 0x1) << 12) | (((raw >> 7) & 0x1) << 11) |
                          (((raw >> 25) & 0x3F) << 5) | (((raw >> 8) & 0xF) << 1));
    if (v & 0x1000) v |= (int32_t)0xFFFFE000;
    return v;
}

static inline int32_t imm_j(uint32_t raw) {
    int32_t v = (int32_t)((((raw >> 31) & 0x1) << 20) | (((raw >> 12) & 0xFF) << 12) |
                          (((raw >> 20) & 0x1) << 11) | (((raw >> 21) & 0x3FF) << 1));
    if (v & 0x100000) v |= (int32_t)0xFFE00000;
    return v;
}

/* Where to fetch after the word `raw` at `pc`. */
static inline uint32_t predict_next(const PipelineState *p, uint32_t pc, uint32_t raw) {
    (void)p;
    /* TODO(lab3): branch (OPC_BRANCH) -> ask p->cfg.predictor->predict() with
     * target pc + imm_b(raw); jal (OPC_JAL) -> pc + imm_j(raw); else pc + 4. */
    (void)raw;
    return pc + 4;
}

static void stage_if(PipelineState *p, const Memory *mem, const ExOutcome *o, int stall) {
    (void)p; (void)mem; (void)o; (void)stall;
    /* TODO(lab1): o->stop -> bubble + fetch_stopped; o->redirect -> squash the
     * word fetched this cycle (stats.squashed) and fetch_pc = o->target;
     * stall -> hold IF/ID; otherwise fetch memory[fetch_pc] into IF/ID
     * (fetch fault -> fault_set), pred_next = predict_next(), advance. */
    p->if_id.valid = 0;
}

/* ------------------------------------------------------------------ */
/* One clock cycle                                                    */
/* ------------------------------------------------------------------ */

/* Control signals of the instruction sitting in IF/ID, for the hazard unit
 * (hardware would get these from the ID stage's decoder). An instruction
 * that will fault has no operands: it never executes. */
static HazardSlot peek_id_slot(const IF_ID_Latch *f) {
    HazardSlot s;
    memset(&s, 0, sizeof(s));
    if (!f->valid || has_fault(&f->fault)) return s;
    DecodedInstr d;
    const InstructionHandler *h;
    if (decode_instruction(f->raw, &d, &h) != DECODE_OK) return s;
    s.valid = 1;
    control_from_instr(h, &d, &s.ctrl);
    return s;
}

static HazardSlot slot_of(int valid, const PipeFault *fault, const Control *ctrl) {
    HazardSlot s;
    memset(&s, 0, sizeof(s));
    if (valid && !has_fault(fault)) { s.valid = 1; s.ctrl = *ctrl; }
    return s;
}

CPUStatus pipeline_step(PipelineState *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;
    cpu->cycle_count++;

    /* Start-of-cycle snapshot + hazard unit decision for this cycle. */
    const PipelineState cur = *p;
    HazardInput hin;
    hin.id  = peek_id_slot(&cur.if_id);
    hin.ex  = slot_of(cur.id_ex.valid,  &cur.id_ex.fault,  &cur.id_ex.ctrl);
    hin.mem = slot_of(cur.ex_mem.valid, &cur.ex_mem.fault, &cur.ex_mem.ctrl);
    hin.wb  = slot_of(cur.mem_wb.valid, &cur.mem_wb.fault, &cur.mem_wb.ctrl);
    HazardDecision hz = {0, FWD_NONE, FWD_NONE, FWD_NONE};
    p->cfg.hazard->decide(&hin, &hz);

    if (stage_wb(p, cpu)) return cpu->status;

    ExOutcome o = {0, 0, 0};
    if (stage_mem(p, mem)) {
        /* A load/store just faulted: the instruction behind it in EX is
         * younger and must not execute. */
        p->ex_mem.valid = 0;
        o.stop = 1;
    } else {
        stage_ex(p, cpu, mem, &cur, &hz, &o);
    }

    /* A redirect or stop squashes ID anyway, so a stall only matters
     * when neither happened. */
    int stall = hz.stall && !o.redirect && !o.stop && hin.id.valid;
    if (stall) p->stats.stalls++;

    stage_id(p, cpu, o.redirect || o.stop, o.redirect, stall);
    stage_if(p, mem, &o, stall);

    cpu->pc = p->fetch_pc;
    return cpu->status;
}

/* ------------------------------------------------------------------ */
/* Views / stats / engine wrapper                                     */
/* ------------------------------------------------------------------ */

int pipeline_stage_views(const PipelineState *p, PipelineStageView *out, int max) {
    if (max < 5) return 0;
    out[0] = (PipelineStageView){"IF",  p->fetch_pc,  p->fetch_stopped};
    out[1] = (PipelineStageView){"ID",  p->if_id.pc,  !p->if_id.valid};
    out[2] = (PipelineStageView){"EX",  p->id_ex.pc,  !p->id_ex.valid};
    out[3] = (PipelineStageView){"MEM", p->ex_mem.pc, !p->ex_mem.valid};
    out[4] = (PipelineStageView){"WB",  p->mem_wb.pc, !p->mem_wb.valid};
    return 5;
}

void pipeline_format_stats(const PipelineState *p, const CPUState *cpu, char *buf, size_t n) {
    const PipelineStats *s = &p->stats;
    double cpi = s->retired ? (double)cpu->cycle_count / (double)s->retired : 0.0;
    double acc = s->branches ? 100.0 * (double)(s->branches - s->branch_mispredicts) / (double)s->branches
                             : 100.0;
    snprintf(buf, n,
             "pipeline        = 5-stage, hazard unit '%s', predictor '%s'\n"
             "retired         = %llu\n"
             "CPI             = %.3f\n"
             "stalls          = %llu\n"
             "forwards        = %llu\n"
             "branches        = %llu\n"
             "mispredicts     = %llu\n"
             "accuracy        = %.1f%%\n"
             "jal             = %llu\n"
             "jalr            = %llu\n"
             "redirects       = %llu\n"
             "squashed        = %llu\n",
             p->cfg.hazard->name, p->cfg.predictor->name,
             (unsigned long long)s->retired, cpi,
             (unsigned long long)s->stalls,
             (unsigned long long)s->forwards,
             (unsigned long long)s->branches,
             (unsigned long long)s->branch_mispredicts,
             acc,
             (unsigned long long)s->jal,
             (unsigned long long)s->jalr,
             (unsigned long long)s->redirects,
             (unsigned long long)s->squashed);
}

static CPUStatus engine_step(SimEngine *e) {
    return pipeline_step((PipelineState *)e->ctx, e->cpu, e->mem);
}

static int engine_views(const SimEngine *e, PipelineStageView *out, int max) {
    return pipeline_stage_views((const PipelineState *)e->ctx, out, max);
}

static void engine_stats(const SimEngine *e, char *buf, size_t n) {
    pipeline_format_stats((const PipelineState *)e->ctx, e->cpu, buf, n);
}

void pipeline_make_engine(SimEngine *e, PipelineState *p, CPUState *cpu, Memory *mem) {
    e->name = "5-stage pipeline";
    e->cpu = cpu;
    e->mem = mem;
    e->ctx = p;
    e->step = engine_step;
    e->stage_views = engine_views;
    e->format_stats = engine_stats;
}
