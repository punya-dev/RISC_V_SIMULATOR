#include "pipeline_state.h"
#include "hazard.h"
#include "decoder.h"
#include "cli_display.h"
#include <stdio.h>
#include <string.h>

void pipeline_init(Pipeline *p, int forwarding) {
    memset(p, 0, sizeof(*p));
    p->forwarding = forwarding;
}

static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b) {
    uint32_t imm = (uint32_t)i->imm;
    switch (i->opcode) {
        case 0x37: return imm;
        case 0x17: return pc + imm;
        case 0x03:
        case 0x23: return a + imm;
        case 0x33:
            switch (i->funct3) {
                case 0x0: return i->funct7 == 0x20 ? a - b : a + b;
                case 0x1: return a << (b & 0x1F);
                case 0x2: return (int32_t)a < (int32_t)b;
                case 0x3: return a < b;
                case 0x4: return a ^ b;
                case 0x5: return i->funct7 == 0x20 ? (uint32_t)((int32_t)a >> (b & 0x1F)) : a >> (b & 0x1F);
                case 0x6: return a | b;
                case 0x7: return a & b;
            }
            break;
        case 0x13:
            switch (i->funct3) {
                case 0x0: return a + imm;
                case 0x1: return a << (imm & 0x1F);
                case 0x2: return (int32_t)a < (int32_t)imm;
                case 0x3: return a < imm;
                case 0x4: return a ^ imm;
                case 0x5: return i->funct7 == 0x20 ? (uint32_t)((int32_t)a >> (imm & 0x1F)) : a >> (imm & 0x1F);
                case 0x6: return a | imm;
                case 0x7: return a & imm;
            }
            break;
    }
    return 0;
}

static int branch_taken(uint32_t funct3, uint32_t a, uint32_t b) {
    switch (funct3) {
        case 0x0: return a == b;
        case 0x1: return a != b;
        case 0x4: return (int32_t)a < (int32_t)b;
        case 0x5: return (int32_t)a >= (int32_t)b;
        case 0x6: return a < b;
        case 0x7: return a >= b;
    }
    return 0;
}

static void raise_fault(CPUState *cpu, const MEMWB *w) {
    cpu->fault_addr = w->fault_addr;
    switch (w->fault) {
        case PF_ILLEGAL:
            cpu->status = CPU_ILLEGAL_INSTRUCTION;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                     "unknown instruction 0x%08x at pc=0x%08x (no handler registered)",
                     w->instr.raw, w->pc);
            break;
        case PF_FETCH:
            cpu->status = CPU_MEM_FAULT;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                     "instruction fetch out of bounds at pc=0x%08x", w->pc);
            break;
        default:
            cpu->status = CPU_MEM_FAULT;
            snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                     "data access out of bounds at addr=0x%08x (pc=0x%08x)", w->fault_addr, w->pc);
            break;
    }
}

CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem) {
    if (cpu->status != CPU_RUNNING) return cpu->status;

    Pipeline nx = *p;
    nx.cycles++;
    cpu->cycle_count = nx.cycles;

    /* ---------------- WB ---------------- */
    if (p->mem_wb.valid) {
        const MEMWB *w = &p->mem_wb;
        if (w->fault != PF_NONE) {
            raise_fault(cpu, w);
            *p = nx;
            return cpu->status;
        }
        if (w->ctrl.reg_write) cpu_write_reg(cpu, (int)w->instr.rd, w->wb_val);
        if (w->ctrl.is_system) {
            uint32_t saved_pc = cpu->pc;
            DecodedInstr instr = w->instr;
            cpu->pc = w->pc;
            w->handler->execute(cpu, mem, &instr);
            cpu->pc = saved_pc;
        }
        nx.retired++;
        if (cpu->status != CPU_RUNNING) {
            *p = nx;
            return cpu->status;
        }
    }

    /* ---------------- MEM ---------------- */
    nx.mem_wb.valid = 0;
    if (p->ex_mem.valid) {
        const EXMEM *e = &p->ex_mem;
        MEMWB out = {0};
        out.valid = 1;
        out.pc = e->pc;
        out.fault = e->fault;
        out.instr = e->instr;
        out.handler = e->handler;
        out.ctrl = e->ctrl;
        out.fault_addr = e->fault_addr;
        out.wb_val = e->alu_result;
        if (out.fault == PF_NONE) {
            uint32_t addr = e->alu_result;
            MemStatus ms = MEM_OK;
            if (e->ctrl.mem_read) {
                uint8_t b; uint16_t h; uint32_t w;
                switch (e->instr.funct3) {
                    case 0x0: ms = memory_read_byte(mem, addr, &b); out.wb_val = (uint32_t)(int32_t)(int8_t)b; break;
                    case 0x1: ms = memory_read_half(mem, addr, &h); out.wb_val = (uint32_t)(int32_t)(int16_t)h; break;
                    case 0x2: ms = memory_read_word(mem, addr, &w); out.wb_val = w; break;
                    case 0x4: ms = memory_read_byte(mem, addr, &b); out.wb_val = b; break;
                    case 0x5: ms = memory_read_half(mem, addr, &h); out.wb_val = h; break;
                }
            } else if (e->ctrl.mem_write) {
                switch (e->instr.funct3) {
                    case 0x0: ms = memory_write_byte(mem, addr, (uint8_t)e->store_val); break;
                    case 0x1: ms = memory_write_half(mem, addr, (uint16_t)e->store_val); break;
                    case 0x2: ms = memory_write_word(mem, addr, e->store_val); break;
                }
            }
            if (ms != MEM_OK) {
                out.fault = PF_MEM;
                out.fault_addr = addr;
            }
        }
        nx.mem_wb = out;
    }

    /* ---------------- EX ---------------- */
    int flush = 0;
    uint32_t redirect_pc = 0;
    nx.ex_mem.valid = 0;
    if (p->id_ex.valid) {
        const IDEX *d = &p->id_ex;
        EXMEM out = {0};
        out.valid = 1;
        out.pc = d->pc;
        out.fault = d->fault;
        out.instr = d->instr;
        out.handler = d->handler;
        out.ctrl = d->ctrl;
        Operands o = forward_operands(p, d);
        out.store_val = o.b;
        nx.fwd_ex += (uint64_t)o.fwd_ex;
        nx.fwd_mem += (uint64_t)o.fwd_mem;
        if (d->fault != PF_NONE) {
            out.fault_addr = d->pc;
        } else if (d->ctrl.is_branch) {
            if (branch_taken(d->instr.funct3, o.a, o.b)) {
                flush = 1;
                redirect_pc = d->pc + (uint32_t)d->instr.imm;
            }
        } else if (d->ctrl.is_jump) {
            out.alu_result = d->pc + 4;
            flush = 1;
            if (d->instr.opcode == 0x67)
                redirect_pc = (o.a + (uint32_t)d->instr.imm) & ~1u;
            else
                redirect_pc = d->pc + (uint32_t)d->instr.imm;
        } else {
            out.alu_result = alu(&d->instr, d->pc, o.a, o.b);
        }
        nx.ex_mem = out;
    }
    if (flush) nx.flushes++;

    /* ---------------- ID ---------------- */
    int stall = 0;
    nx.id_ex.valid = 0;
    if (p->if_id.valid && !flush) {
        const IFID *f = &p->if_id;
        IDEX out = {0};
        out.valid = 1;
        out.pc = f->pc;
        if (f->fault != PF_NONE) {
            out.fault = f->fault;
            nx.id_ex = out;
        } else {
            DecodedInstr d;
            const InstructionHandler *h;
            if (decode_instruction(f->raw, &d, &h) != DECODE_OK) {
                out.fault = PF_ILLEGAL;
                out.instr.raw = f->raw;
                nx.id_ex = out;
            } else {
                ControlSignals c = control_decode(&d, h);
                HazardVerdict hz = hazard_check(p, &d, &c);
                if (hz.stall) {
                    nx.id_ex.valid = 0;
                    nx.if_id = p->if_id;
                    stall = 1;
                    nx.stalls++;
                    if (hz.load_use) nx.load_use_stalls++;
                } else {
                    out.instr = d;
                    out.handler = h;
                    out.ctrl = c;
                    out.rs1_val = cpu_read_reg(cpu, (int)d.rs1);
                    out.rs2_val = cpu_read_reg(cpu, (int)d.rs2);
                    nx.id_ex = out;
                }
            }
        }
    }

    /* ---------------- IF ---------------- */
    if (!stall) {
        uint32_t fetch_pc = flush ? redirect_pc : cpu->pc;
        IFID out = {0};
        out.valid = 1;
        out.pc = fetch_pc;
        if (memory_read_word(mem, fetch_pc, &out.raw) != MEM_OK) out.fault = PF_FETCH;
        nx.if_id = out;
        cpu->pc = fetch_pc + 4;
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
    printf("pipeline: mode=%s cycles=%llu retired=%llu stalls=%llu flushes=%llu "
           "CPI=%.3f fwd_ex=%llu fwd_mem=%llu load_use=%llu\n",
           p->forwarding ? "forward" : "stall",
           (unsigned long long)p->cycles, (unsigned long long)p->retired,
           (unsigned long long)p->stalls, (unsigned long long)p->flushes,
           p->retired ? (double)p->cycles / (double)p->retired : 0.0,
           (unsigned long long)p->fwd_ex, (unsigned long long)p->fwd_mem,
           (unsigned long long)p->load_use_stalls);
}
