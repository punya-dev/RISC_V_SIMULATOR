#include "hazard.h"

/*
 * Naive interlock, no forwarding: the instruction in ID may only read the
 * register file once every older producer of its sources has written back.
 * (WB writes before ID reads in the same cycle.)
 */
static int writes(const HazardSlot *s, int r) {
    return r != REG_NONE && s->valid && s->ctrl.dst == r;
}

static void decide_stall(const HazardInput *in, HazardDecision *out) {
    if (!in->id.valid) return;
    const int srcs[3] = {in->id.ctrl.src1, in->id.ctrl.src2, in->id.ctrl.src3};
    for (int k = 0; k < 3; k++) {
        if (writes(&in->ex, srcs[k]) || writes(&in->mem, srcs[k])) out->stall = 1;
    }
}

const HazardDetector hazard_stall = {
    "stall",
    "stall in ID until producers write back; no forwarding",
    decide_stall,
};
