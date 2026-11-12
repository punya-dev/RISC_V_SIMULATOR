#ifndef HAZARD_H
#define HAZARD_H

#include "pipeline_state.h"

typedef struct {
    int stall;
    int load_use;
} HazardVerdict;

typedef struct {
    uint32_t a, b;
    int fwd_ex, fwd_mem;
} Operands;

int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs);
Operands forward_operands(const Pipeline *p, const IDEX *d);
HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c);

#endif
