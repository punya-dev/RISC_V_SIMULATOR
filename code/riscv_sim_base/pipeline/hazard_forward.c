#include "hazard.h"

/*
 * Full forwarding unit:
 *   EX/MEM -> EX   producer one instruction ahead (ALU result)
 *   MEM/WB -> EX   producer two ahead (ALU result or loaded value)
 *   (three ahead needs nothing: WB writes before ID reads)
 * plus a 1-cycle load-use stall. FP registers (unified numbers 32..63) are
 * forwarded exactly like integer ones.
 */
static void decide_forward(const HazardInput *in, HazardDecision *out) {
    (void)in; (void)out;
    /* TODO(lab2): out->fwd1 / fwd2 / fwd3 for the EX instruction's sources
     * (newest producer wins; a load in MEM has no value yet), and
     * out->stall for a load in EX whose result the ID instruction needs. */
}

const HazardDetector hazard_forward = {
    "forward",
    "EX->EX and MEM->EX forwarding, 1-cycle stall on load-use",
    decide_forward,
};
