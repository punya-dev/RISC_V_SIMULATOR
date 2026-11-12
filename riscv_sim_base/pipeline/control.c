#include "control.h"

ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h) {
    (void)i;
    ControlSignals c = {0};
    if (!h) return c;

    switch (h->format) {
        case FMT_R:
            c.reg_write = 1;
            c.wb_src    = WB_ALU;
            c.uses_rs1  = 1;
            c.uses_rs2  = 1;
            break;
        case FMT_I:
        case FMT_I_SHAMT:
            c.reg_write = 1;
            c.wb_src    = WB_ALU;
            c.uses_rs1  = 1;
            break;
        case FMT_U:
            c.reg_write = 1;
            c.wb_src    = WB_ALU;
            break;
        case FMT_I_LOAD:
            c.reg_write = 1;
            c.wb_src    = WB_MEM;
            c.mem_read  = 1;
            c.uses_rs1  = 1;
            break;
        case FMT_S:
            c.mem_write = 1;
            c.uses_rs1  = 1;
            c.uses_rs2  = 1;
            break;
        case FMT_B:
            c.is_branch = 1;
            c.uses_rs1  = 1;
            c.uses_rs2  = 1;
            break;
        case FMT_J:
            c.reg_write = 1;
            c.wb_src    = WB_PC4;
            c.is_jump   = 1;
            break;
        case FMT_I_JALR:
            c.reg_write = 1;
            c.wb_src    = WB_PC4;
            c.is_jump   = 1;
            c.uses_rs1  = 1;
            break;
        case FMT_I_SYS:
            c.is_system = 1;
            break;
    }

    return c;
}
