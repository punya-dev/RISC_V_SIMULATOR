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
    MEM_WB_Latch *w = &p->mem_wb;
    if (!w->valid) return 0;
    if (has_fault(&w->fault)) {
        cpu->status = w->fault.status;
        cpu->fault_addr = w->fault.addr;
        snprintf(cpu->fault_msg, sizeof(cpu->fault_msg), "%s", w->fault.msg);
        cpu->pc = w->pc;
        return 1;
    }
    if (w->ctrl.dst != REG_NONE) unified_write(cpu, w->ctrl.dst, w->wb_val);
    p->stats.retired++;
    if (w->exit_requested) {
        cpu->status = CPU_ECALL_EXIT;
        cpu->pc = w->pc;
        return 1;
    }
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
    const EX_MEM_Latch *x = &p->ex_mem;
    MEM_WB_Latch *w = &p->mem_wb;
    w->valid = x->valid;
    if (!x->valid) return 0;
    w->pc = x->pc;
    w->raw = x->raw;
    w->fault = x->fault;
    w->ctrl = x->ctrl;
    w->wb_val = x->alu_result;
    w->exit_requested = x->exit_requested;
    if (has_fault(&w->fault)) return 0;

    uint32_t addr = x->alu_result;
    const char *mn = x->handler ? x->handler->mnemonic : "?";
    if (x->ctrl.mem_op == MEMOP_LOAD) {
        uint32_t v;
        if (mem_load(mem, addr, x->ctrl.mem_size, &v) != MEM_OK) {
            fault_set(&w->fault, CPU_MEM_FAULT, addr, "%s out of bounds at address 0x%08x", mn, addr);
            return 1;
        }
        w->wb_val = extend_load(v, x->ctrl.mem_size, x->ctrl.mem_unsigned);
    } else if (x->ctrl.mem_op == MEMOP_STORE) {
        if (mem_store(mem, addr, x->ctrl.mem_size, x->store_val) != MEM_OK) {
            fault_set(&w->fault, CPU_MEM_FAULT, addr, "%s out of bounds at address 0x%08x", mn, addr);
            return 1;
        }
    }
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
    if (sel == FWD_EX_MEM) { stats->forwards++; return cur->ex_mem.alu_result; }
    if (sel == FWD_MEM_WB) { stats->forwards++; return cur->mem_wb.wb_val; }
    return from_regfile;
}

static void stage_ex(PipelineState *p, CPUState *cpu, Memory *mem, const PipelineState *cur,
                     const HazardDecision *hz, ExOutcome *o) {
    const ID_EX_Latch *d = &p->id_ex;
    EX_MEM_Latch *x = &p->ex_mem;
    x->valid = d->valid;
    if (!d->valid) return;
    x->pc = d->pc;
    x->raw = d->raw;
    x->fault = d->fault;
    x->handler = d->handler;
    x->ctrl = d->ctrl;
    x->alu_result = 0;
    x->store_val = 0;
    x->exit_requested = 0;
    if (has_fault(&x->fault)) { o->stop = 1; return; }

    const Control *ct = &d->ctrl;
    uint32_t op1 = forward_mux(d->src1_val, hz->fwd1, cur, &p->stats);
    uint32_t op2 = forward_mux(d->src2_val, hz->fwd2, cur, &p->stats);
    uint32_t op3 = forward_mux(d->src3_val, hz->fwd3, cur, &p->stats);

    if (ct->mem_op != MEMOP_NONE) {
        x->alu_result = op1 + (uint32_t)d->instr.imm;
        x->store_val = op2;
        return;
    }

    CPUState s;
    memset(&s, 0, sizeof(s));
    s.status = CPU_RUNNING;
    s.pc = d->pc;
    s.next_pc = d->pc + 4;
    s.fcsr = cpu->fcsr;
    if (ct->src1 != REG_NONE) unified_write(&s, ct->src1, op1);
    if (ct->src2 != REG_NONE) unified_write(&s, ct->src2, op2);
    if (ct->src3 != REG_NONE) unified_write(&s, ct->src3, op3);
    DecodedInstr instr = d->instr;
    d->handler->execute(&s, mem, &instr);
    cpu->fcsr = s.fcsr;

    if (s.status == CPU_ECALL_EXIT) {
        x->exit_requested = 1;
        o->stop = 1;
        return;
    }
    if (s.status != CPU_RUNNING) {
        fault_set(&x->fault, s.status, s.fault_addr,"%s", s.fault_msg);
        o->stop = 1;
        return;
    }
    if (ct->dst != REG_NONE) x->alu_result = unified_read(&s, ct->dst);

    if (ct->is_branch) {
        p->stats.branches++;
        uint32_t target = d->pc + (uint32_t)d->instr.imm;
        p->cfg.predictor->update(&p->bp, d->pc, target, s.next_pc == target);
        if (s.next_pc != d->pred_next) p->stats.branch_mispredicts++;
    }
    if (ct->is_jump) {
        if ((d->raw & 0x7F) == OPC_JAL) p->stats.jal++;
        else p->stats.jalr++;
    }
    if (s.next_pc != d->pred_next) {
        o->redirect = 1;
        o->target = s.next_pc;
        p->stats.redirects++;
    }
}

/* ------------------------------------------------------------------ */
/* ID                                                                 */
/* ------------------------------------------------------------------ */

/* `squash`: discard the instruction in IF/ID. `wrong_path`: the reason is a
 * PC redirect (counted in stats.squashed), not an exit/fault draining.
 * `stall`: the hazard unit says wait -- IF/ID is left untouched (IF holds
 * it too) and a bubble goes into EX. */
static void stage_id(PipelineState *p, const CPUState *cpu, int squash, int wrong_path, int stall) {
    const IF_ID_Latch *f = &p->if_id;
    ID_EX_Latch *d = &p->id_ex;
    if (squash) {
        if (wrong_path && f->valid) p->stats.squashed++;
        d->valid = 0;
        return;
    }
    if (stall || !f->valid) { d->valid = 0; return; }

    d->valid = 1;
    d->pc = f->pc;
    d->raw = f->raw;
    d->fault = f->fault;
    d->pred_next = f->pred_next;
    d->handler = NULL;
    memset(&d->instr, 0, sizeof(d->instr));
    memset(&d->ctrl, 0, sizeof(d->ctrl));
    d->ctrl.src1 = d->ctrl.src2 = d->ctrl.src3 = d->ctrl.dst = REG_NONE;
    d->src1_val = d->src2_val = d->src3_val = 0;
    if (has_fault(&d->fault)) return;

    if (decode_instruction(f->raw, &d->instr, &d->handler) != DECODE_OK) {
        fault_set(&d->fault, CPU_ILLEGAL_INSTRUCTION, f->pc,
                  "unknown instruction 0x%08x at pc=0x%08x (no handler registered)", f->raw, f->pc);
        return;
    }
    control_from_instr(d->handler, &d->instr, &d->ctrl);
    if (d->ctrl.src1 != REG_NONE) d->src1_val = unified_read(cpu, d->ctrl.src1);
    if (d->ctrl.src2 != REG_NONE) d->src2_val = unified_read(cpu, d->ctrl.src2);
    if (d->ctrl.src3 != REG_NONE) d->src3_val = unified_read(cpu, d->ctrl.src3);
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
    uint32_t opc = raw & 0x7F;
    if (opc == OPC_BRANCH) {
        uint32_t target = pc + (uint32_t)imm_b(raw);
        return p->cfg.predictor->predict(&p->bp, pc, target) ? target : pc + 4;
    }
    if (opc == OPC_JAL) return pc + (uint32_t)imm_j(raw);
    return pc + 4;
}

static void stage_if(PipelineState *p, const Memory *mem, const ExOutcome *o, int stall) {
    IF_ID_Latch *f = &p->if_id;
    if (o->stop) {
        f->valid = 0;
        p->fetch_stopped = 1;
        return;
    }
    if (o->redirect) {
        if (!p->fetch_stopped) p->stats.squashed++;
        f->valid = 0;
        p->fetch_pc = o->target;
        return;
    }
    if (stall) return;
    if (p->fetch_stopped) { f->valid = 0; return; }

    f->valid = 1;
    f->pc = p->fetch_pc;
    fault_clear(&f->fault);
    if (memory_read_word(mem, p->fetch_pc, &f->raw) != MEM_OK) {
        f->raw = 0;
        fault_set(&f->fault, CPU_MEM_FAULT, p->fetch_pc,
                  "instruction fetch out of bounds at pc=0x%08x", p->fetch_pc);
        f->pred_next = p->fetch_pc + 4;
    } else {
        f->pred_next = predict_next(p, p->fetch_pc, f->raw);
    }
    p->fetch_pc = f->pred_next;
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
