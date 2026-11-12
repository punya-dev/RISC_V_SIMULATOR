#include "hazard.h"

static int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs) {
    return valid && lc->reg_write && rd != 0 && rd == rs;
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d,
                           const ControlSignals *c) {
    HazardVerdict v = {0, 0};

    if (!p->forwarding) {
        uint32_t s1 = c->uses_rs1 ? d->rs1 : 0;
        uint32_t s2 = c->uses_rs2 ? d->rs2 : 0;
        if (produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.instr.rd, s1) ||
            produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.instr.rd, s2) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s1) ||
            produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s2) ||
            produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s1) ||
            produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s2))
            v.stall = 1;
        return v;
    }

    return v;
}
