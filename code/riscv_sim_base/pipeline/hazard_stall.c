#include "hazard.h"

/*
 * Naive interlock, no forwarding: the instruction in ID may only read the
 * register file once every older producer of its sources has written back.
 * (WB writes before ID reads in the same cycle.)
 */
static void decide_stall(const HazardInput *in, HazardDecision *out) {
    (void)in; (void)out;
    /* TODO(lab2): set out->stall when the ID instruction (in->id) reads a
     * register (src1/src2/src3) that the EX or MEM instruction writes. */
}

const HazardDetector hazard_stall = {
    "stall",
    "stall in ID until producers write back; no forwarding",
    decide_stall,
};
