#include "pipeline_state.h"
#include "decoder.h"
#include "cli_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int trace_enabled = 0;

void pipeline_init(Pipeline *p) {
    memset(p, 0, sizeof(*p));
    const char *env = getenv("RVSIM_TRACE");
    trace_enabled = (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0);
}

static int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs) {
    return valid && lc->reg_write && rd != 0 && rd == rs;
}

static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b) {
    switch (i->opcode) {
        case 0x37: /* lui */
            return (uint32_t)i->imm;
        case 0x17: /* auipc */
            return pc + (uint32_t)i->imm;
        case 0x03: /* loads */
        case 0x23: /* stores */
            return a + (uint32_t)i->imm;
        case 0x33: /* R-type */
        case 0x13: /* I-type arithmetic */ {
            uint32_t y = (i->opcode == 0x33) ? b : (uint32_t)i->imm;
            uint32_t shamt = y & 0x1Fu;
            switch (i->funct3) {
                case 0x0:
                    if (i->opcode == 0x33 && i->funct7 == 0x20) return a - y;
                    return a + y;
                case 0x1:
                    return a << shamt;
                case 0x2:
                    return ((int32_t)a < (int32_t)y) ? 1u : 0u;
                case 0x3:
                    return (a < y) ? 1u : 0u;
                case 0x4:
                    return a ^ y;
                case 0x5:
                    if (i->funct7 == 0x20) return (uint32_t)((int32_t)a >> shamt);
                    return a >> shamt;
                case 0x6:
                    return a | y;
                case 0x7:
                    return a & y;
                default:
                    return 0;
            }
        }
        default:
            return 0;
    }
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;

    Pipeline nx = *p;
    int stall = 0;
    int flush = 0;
    uint32_t redirect_pc = 0;

    /* 1. WB (reads p->mem_wb) */
    if (p->mem_wb.valid) {
        if (p->mem_wb.fault != PF_NONE) {
            if (p->mem_wb.fault == PF_ILLEGAL) {
                cpu->status = CPU_ILLEGAL_INSTRUCTION;
                cpu->fault_addr = p->mem_wb.pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "illegal instruction at pc=0x%08x", p->mem_wb.pc);
            } else if (p->mem_wb.fault == PF_FETCH) {
                cpu->status = CPU_MEM_FAULT;
                cpu->fault_addr = p->mem_wb.pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "instruction fetch out of bounds at pc=0x%08x", p->mem_wb.pc);
            } else {
                cpu->status = CPU_MEM_FAULT;
                cpu->fault_addr = p->mem_wb.fault_addr;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "memory access out of bounds at address 0x%08x", p->mem_wb.fault_addr);
            }
            nx.cycles++;
            cpu->cycle_count = nx.cycles;
            *p = nx;
            return cpu->status;
        }
        if (p->mem_wb.ctrl.reg_write) {
            cpu_write_reg(cpu, (int)p->mem_wb.instr.rd, p->mem_wb.wb_val);
        }
        if (p->mem_wb.ctrl.is_system) {
            uint32_t saved_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            DecodedInstr tmp = p->mem_wb.instr;
            p->mem_wb.handler->execute(cpu, mem, &tmp);
            cpu->pc = saved_pc;
        }
        nx.retired++;
    }

    /* 2. MEM (reads p->ex_mem, writes nx.mem_wb) */
    memset(&nx.mem_wb, 0, sizeof(nx.mem_wb));
    if (p->ex_mem.valid) {
        nx.mem_wb.valid      = 1;
        nx.mem_wb.pc         = p->ex_mem.pc;
        nx.mem_wb.fault      = p->ex_mem.fault;
        nx.mem_wb.instr      = p->ex_mem.instr;
        nx.mem_wb.handler    = p->ex_mem.handler;
        nx.mem_wb.ctrl       = p->ex_mem.ctrl;
        nx.mem_wb.fault_addr = p->ex_mem.fault_addr;
        nx.mem_wb.wb_val     = p->ex_mem.alu_result;

        if (p->ex_mem.fault == PF_NONE) {
            uint32_t addr = p->ex_mem.alu_result;
            if (p->ex_mem.ctrl.mem_read) {
                MemStatus ms = MEM_OK;
                switch (p->ex_mem.instr.funct3) {
                    case 0x0: {
                        uint8_t b = 0;
                        ms = memory_read_byte(mem, addr, &b);
                        if (ms == MEM_OK) nx.mem_wb.wb_val = (uint32_t)(int32_t)(int8_t)b;
                        break;
                    }
                    case 0x1: {
                        uint16_t h = 0;
                        ms = memory_read_half(mem, addr, &h);
                        if (ms == MEM_OK) nx.mem_wb.wb_val = (uint32_t)(int32_t)(int16_t)h;
                        break;
                    }
                    case 0x2: {
                        uint32_t w = 0;
                        ms = memory_read_word(mem, addr, &w);
                        if (ms == MEM_OK) nx.mem_wb.wb_val = w;
                        break;
                    }
                    case 0x4: {
                        uint8_t b = 0;
                        ms = memory_read_byte(mem, addr, &b);
                        if (ms == MEM_OK) nx.mem_wb.wb_val = (uint32_t)b;
                        break;
                    }
                    case 0x5: {
                        uint16_t h = 0;
                        ms = memory_read_half(mem, addr, &h);
                        if (ms == MEM_OK) nx.mem_wb.wb_val = (uint32_t)h;
                        break;
                    }
                    default:
                        break;
                }
                if (ms != MEM_OK) {
                    nx.mem_wb.fault = PF_MEM;
                    nx.mem_wb.fault_addr = addr;
                }
            } else if (p->ex_mem.ctrl.mem_write) {
                MemStatus ms = MEM_OK;
                switch (p->ex_mem.instr.funct3) {
                    case 0x0:
                        ms = memory_write_byte(mem, addr, (uint8_t)p->ex_mem.store_val);
                        break;
                    case 0x1:
                        ms = memory_write_half(mem, addr, (uint16_t)p->ex_mem.store_val);
                        break;
                    case 0x2:
                        ms = memory_write_word(mem, addr, p->ex_mem.store_val);
                        break;
                    default:
                        break;
                }
                if (ms != MEM_OK) {
                    nx.mem_wb.fault = PF_MEM;
                    nx.mem_wb.fault_addr = addr;
                }
            }
        }
    }

    /* 3. EX (reads p->id_ex, writes nx.ex_mem) */
    memset(&nx.ex_mem, 0, sizeof(nx.ex_mem));
    if (p->id_ex.valid) {
        nx.ex_mem.valid      = 1;
        nx.ex_mem.pc         = p->id_ex.pc;
        nx.ex_mem.fault      = p->id_ex.fault;
        nx.ex_mem.instr      = p->id_ex.instr;
        nx.ex_mem.handler    = p->id_ex.handler;
        nx.ex_mem.ctrl       = p->id_ex.ctrl;
        nx.ex_mem.store_val  = p->id_ex.rs2_val;
        nx.ex_mem.fault_addr = p->id_ex.pc;

        if (p->id_ex.fault == PF_NONE) {
            if (p->id_ex.ctrl.is_branch) {
                uint32_t a = p->id_ex.rs1_val;
                uint32_t b = p->id_ex.rs2_val;
                int taken = 0;
                switch (p->id_ex.instr.funct3) {
                    case 0x0: taken = (a == b); break;
                    case 0x1: taken = (a != b); break;
                    case 0x4: taken = ((int32_t)a < (int32_t)b); break;
                    case 0x5: taken = ((int32_t)a >= (int32_t)b); break;
                    case 0x6: taken = (a < b); break;
                    case 0x7: taken = (a >= b); break;
                    default: break;
                }
                if (taken) {
                    flush = 1;
                    redirect_pc = p->id_ex.pc + (uint32_t)p->id_ex.instr.imm;
                }
            } else if (p->id_ex.ctrl.is_jump) {
                nx.ex_mem.alu_result = p->id_ex.pc + 4;
                flush = 1;
                if (p->id_ex.instr.opcode == 0x6F) {
                    redirect_pc = p->id_ex.pc + (uint32_t)p->id_ex.instr.imm;
                } else {
                    redirect_pc = (p->id_ex.rs1_val + (uint32_t)p->id_ex.instr.imm) & ~1u;
                }
            } else {
                nx.ex_mem.alu_result = alu(&p->id_ex.instr, p->id_ex.pc,
                                           p->id_ex.rs1_val, p->id_ex.rs2_val);
            }
        }
    }

    /* 4. ID (reads p->if_id, writes nx.id_ex) */
    memset(&nx.id_ex, 0, sizeof(nx.id_ex));
    if (p->if_id.valid) {
        if (p->if_id.fault != PF_NONE) {
            nx.id_ex.valid = 1;
            nx.id_ex.pc    = p->if_id.pc;
            nx.id_ex.fault = p->if_id.fault;
        } else {
            DecodedInstr d;
            const InstructionHandler *h = NULL;
            if (decode_instruction(p->if_id.raw, &d, &h) == DECODE_UNKNOWN_INSTRUCTION) {
                nx.id_ex.valid = 1;
                nx.id_ex.pc    = p->if_id.pc;
                nx.id_ex.fault = PF_ILLEGAL;
            } else {
                ControlSignals c = control_decode(&d, h);
                uint32_t s1 = c.uses_rs1 ? d.rs1 : 0;
                uint32_t s2 = c.uses_rs2 ? d.rs2 : 0;
                int hazard =
                    produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  s1) ||
                    produces(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  s2) ||
                    produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s1) ||
                    produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, s2) ||
                    produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s1) ||
                    produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, s2);

                if (hazard && !flush) {
                    nx.if_id = p->if_id;
                    stall = 1;
                    nx.stalls++;
                } else if (!flush) {
                    nx.id_ex.valid   = 1;
                    nx.id_ex.pc      = p->if_id.pc;
                    nx.id_ex.fault   = PF_NONE;
                    nx.id_ex.instr   = d;
                    nx.id_ex.handler = h;
                    nx.id_ex.ctrl    = c;
                    nx.id_ex.rs1_val = cpu_read_reg(cpu, (int)d.rs1);
                    nx.id_ex.rs2_val = cpu_read_reg(cpu, (int)d.rs2);
                }
            }
        }
    }
    if (flush) {
        memset(&nx.id_ex, 0, sizeof(nx.id_ex));
    }

    /* 5. IF (writes nx.if_id) */
    if (!stall || flush) {
        memset(&nx.if_id, 0, sizeof(nx.if_id));
        uint32_t fpc = flush ? redirect_pc : cpu->pc;
        uint32_t raw = 0;
        nx.if_id.valid = 1;
        nx.if_id.pc = fpc;
        if (memory_read_word(mem, fpc, &raw) != MEM_OK) {
            nx.if_id.fault = PF_FETCH;
        } else {
            nx.if_id.raw = raw;
        }
        cpu->pc = fpc + 4;
    }
    if (flush) {
        nx.flushes++;
    }

    nx.cycles++;
    cpu->cycle_count = nx.cycles;

    if (trace_enabled) {
        fprintf(stderr, "c%-4llu IF=%04x ID=%04x EX=%04x MEM=%04x WB=%04x%s%s\n",
                (unsigned long long)nx.cycles,
                nx.if_id.valid  ? nx.if_id.pc  : 0xffffu,
                p->if_id.valid  ? p->if_id.pc  : 0xffffu,
                p->id_ex.valid  ? p->id_ex.pc  : 0xffffu,
                p->ex_mem.valid ? p->ex_mem.pc : 0xffffu,
                p->mem_wb.valid ? p->mem_wb.pc : 0xffffu,
                stall ? " STALL" : "",
                flush ? " FLUSH" : "");
    }

    *p = nx;
    return cpu->status;
}

void pipeline_run_batch(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING && p->cycles < max_cycles) {
        pipeline_cycle(p, cpu, mem);
    }
    printf("\n");
    print_registers(cpu);
    print_status(cpu);
    if (cpu->status == CPU_RUNNING) {
        printf("  -> stopped: reached max cycle limit (%llu)\n", (unsigned long long)max_cycles);
    }
    double cpi = (p->retired > 0) ? ((double)p->cycles / (double)p->retired) : 0.0;
    printf("pipeline: cycles=%llu retired=%llu stalls=%llu flushes=%llu CPI=%.3f\n",
           (unsigned long long)p->cycles,
           (unsigned long long)p->retired,
           (unsigned long long)p->stalls,
           (unsigned long long)p->flushes,
           cpi);
}
