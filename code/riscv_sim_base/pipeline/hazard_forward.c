#include "hazard.h"

/*
 * Full forwarding unit:
 *   EX/MEM -> EX   producer one instruction ahead (ALU result)
 *   MEM/WB -> EX   producer two ahead (ALU result or loaded value)
 *   (three ahead needs nothing: WB writes before ID reads)
 * plus a 1-cycle load-use stall. FP registers (unified numbers 32..63) are
 * forwarded exactly like integer ones.
 */
static int writes(const HazardSlot *s, int r) {
    return r != REG_NONE && s->valid && s->ctrl.dst == r;
}

static ForwardSel select_source(const HazardInput *in, int r) {
    if (writes(&in->mem, r)) return in->mem.ctrl.mem_op == MEMOP_LOAD ? FWD_NONE : FWD_EX_MEM;
    if (writes(&in->wb, r)) return FWD_MEM_WB;
    return FWD_NONE;
}

static void decide_forward(const HazardInput *in, HazardDecision *out) {
    if (in->ex.valid) {
        out->fwd1 = select_source(in, in->ex.ctrl.src1);
        out->fwd2 = select_source(in, in->ex.ctrl.src2);
        out->fwd3 = select_source(in, in->ex.ctrl.src3);
    }
    if (in->id.valid && in->ex.valid && in->ex.ctrl.mem_op == MEMOP_LOAD) {
        int r = in->ex.ctrl.dst;
        if (r != REG_NONE && (in->id.ctrl.src1 == r || in->id.ctrl.src2 == r || in->id.ctrl.src3 == r))
            out->stall = 1;
    }
}

const HazardDetector hazard_forward = {
    "forward",
    "EX->EX and MEM->EX forwarding, 1-cycle stall on load-use",
    decide_forward,
};
