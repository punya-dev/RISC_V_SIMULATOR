# Lab 2 Report: Building a 5-Stage RV32I Pipeline

## 1. Verification

### Register-File Oracle Equivalence (`MATCH` checks)

Hand check on `tests/sample1_sum_loop.s`:

```text
$ ./rvsim tests/sample1_sum_loop.s --batch    > ref.txt
$ ./rvsim tests/sample1_sum_loop.s --pipeline > pipe.txt
$ diff <(grep '^x' ref.txt) <(grep '^x' pipe.txt) && echo MATCH
MATCH
```

Per-program register diff (`--batch` vs `--pipeline`) across all 13 programs in `tests/`:

```text
micro.s: MATCH
sample1_sum_loop.s: MATCH
sample2_mem_and_branches.s: MATCH
sample4_data_hazards.s: MATCH
t_branches.s: MATCH
t_chain.s: MATCH
t_fault_fetch.s: MATCH
t_fault_illegal.s: MATCH
t_fault_mem.s: MATCH
t_indep5.s: MATCH
t_jump.s: MATCH
t_ldst.s: MATCH
t_x0.s: MATCH
```

### `./run_all.sh` Verification Transcript

```text
$ ./run_all.sh
PASS micro.s  cycles=15 retired=5 stalls=6 flushes=0
PASS sample1_sum_loop.s  cycles=108 retired=53 stalls=40 flushes=11
PASS sample2_mem_and_branches.s  cycles=26 retired=13 stalls=8 flushes=1
PASS sample4_data_hazards.s  cycles=69 retired=29 stalls=35 flushes=1
PASS t_branches.s  cycles=44 retired=19 stalls=14 flushes=7
PASS t_chain.s  cycles=42 retired=13 stalls=24 flushes=1
PASS t_fault_fetch.s  cycles=15 retired=3 stalls=6 flushes=1
PASS t_fault_illegal.s  cycles=10 retired=2 stalls=3 flushes=0
PASS t_fault_mem.s  cycles=14 retired=3 stalls=6 flushes=0
PASS t_indep5.s  cycles=11 retired=7 stalls=0 flushes=0
PASS t_jump.s  cycles=15 retired=8 stalls=1 flushes=2
PASS t_ldst.s  cycles=34 retired=21 stalls=9 flushes=0
PASS t_x0.s  cycles=19 retired=9 stalls=6 flushes=0
passed=13 failed=0
```

Interactive REPL (`./rvsim tests/sample1_sum_loop.s`) and `--batch` single-cycle execution were also verified to remain intact and unchanged.

---

## 2. Measured Pipeline Results (Lab 2 Step 2)

> **Note:** `sample3_hazards.s` was not supplied with the handout (`riscv_sim_base/tests/` contained only `sample1_sum_loop.s` and `sample2_mem_and_branches.s`), so it is omitted from the measured table below.

### Programs Ending via Exit `ecall` (`ECALL_EXIT`)

For every `ECALL_EXIT` program, `retired` equals the single-cycle cycle count and the cycle identity `cycles = retired + stalls + flushes + 4` holds exactly:

| Program | Single-Cycle | Pipeline Cycles | Retired | Stalls | Flushes | CPI | Identity Check (`retired + stalls + flushes + 4`) |
|---|---|---|---|---|---|---|---|
| `sample1_sum_loop.s` | 53 | 108 | 53 | 40 | 11 | 2.038 | `53 + 40 + 11 + 4 = 108` |
| `sample2_mem_and_branches.s` | 13 | 26 | 13 | 8 | 1 | 2.000 | `13 + 8 + 1 + 4 = 26` |
| `micro.s` | 5 | 15 | 5 | 6 | 0 | 3.000 | `5 + 6 + 0 + 4 = 15` |
| `sample4_data_hazards.s` | 29 | 69 | 29 | 35 | 1 | 2.379 | `29 + 35 + 1 + 4 = 69` |
| `t_indep5.s` | 7 | 11 | 7 | 0 | 0 | 1.571 | `7 + 0 + 0 + 4 = 11` |
| `t_jump.s` | 8 | 15 | 8 | 1 | 2 | 1.875 | `8 + 1 + 2 + 4 = 15` |
| `t_ldst.s` | 21 | 34 | 21 | 9 | 0 | 1.619 | `21 + 9 + 0 + 4 = 34` |
| `t_branches.s` | 19 | 44 | 19 | 14 | 7 | 2.316 | `19 + 14 + 7 + 4 = 44` |
| `t_x0.s` | 9 | 19 | 9 | 6 | 0 | 2.111 | `9 + 6 + 0 + 4 = 19` |
| `t_chain.s` | 13 | 42 | 13 | 24 | 1 | 3.231 | `13 + 24 + 1 + 4 = 42` |

### Fault Programs

For fault programs, the exit `ecall` identity does not apply because execution halts when the faulting instruction reaches WB; register states and final fault status still match the single-cycle oracle:

| Program | Oracle Status | Pipeline Status | Pipeline Cycles | Retired | Stalls | Flushes | CPI |
|---|---|---|---|---|---|---|---|
| `t_fault_mem.s` | `MEM_FAULT` | `MEM_FAULT` | 14 | 3 | 6 | 0 | 4.667 |
| `t_fault_fetch.s` | `MEM_FAULT` | `MEM_FAULT` | 15 | 3 | 6 | 1 | 5.000 |
| `t_fault_illegal.s` | `ILLEGAL_INSTRUCTION` | `ILLEGAL_INSTRUCTION` | 10 | 2 | 3 | 0 | 5.000 |

---

## 3. Appendix Experiment: Single-Cycle vs. Pipelined

### Experiment A: `t_indep5.s` (Measured vs. Idealised Formula)

`t_indep5.s` executes 5 independent `addi` instructions followed by the 2-instruction exit sequence (`addi a7, zero, 10` and `ecall`), for `N = 7` total instructions with zero data dependencies and zero branches:

- **Measured (`rvsim`):**
  - Single-cycle (`--batch`): `7` instructions, `7` cycles, `CPI = 1.000`
  - Pipelined (`--pipeline`): `7` retired instructions, `11` cycles (`0` stalls, `0` flushes), `CPI = 1.571`
  - `% change in cycle count`: `(11 - 7) / 7 * 100 = +57.14%`
  - **Explanation:** With no hazards or redirects, the 5-stage pipeline takes `N + 4 = 7 + 4 = 11` cycles: 4 fill cycles for the first instruction to traverse IF/ID/EX/MEM, then 1 instruction retiring per cycle for 7 cycles.
- **Lab Sheet Idealised 5-Instruction Model (computed by formula, without the 2-instruction exit sequence):**
  - Non-pipelined sequential multi-cycle baseline (5 cycles/instruction): `5 * 5 = 25` cycles, `CPI = 5.00`
  - Ideal 5-stage pipeline (`5 + 4` fill cycles): `9` cycles, `CPI = 1.80`
  - Cycle reduction: `(25 - 9) / 25 * 100 = 64%`

### Experiment B: `sample1_sum_loop.s` and `t_ldst.s` (Measured)

| Program | Single-Cycle Cycles | Single-Cycle CPI | Pipeline Cycles | Pipeline CPI | % Change in Cycle Count `(pipe - single) / single * 100` |
|---|---|---|---|---|---|
| `t_indep5.s` | 7 | 1.000 | 11 | 1.571 | `+57.14%` |
| `sample1_sum_loop.s` | 53 | 1.000 | 108 | 2.038 | `+103.77%` |
| `t_ldst.s` | 21 | 1.000 | 34 | 1.619 | `+61.90%` |

---

## 4. Explanations

1. **Why the pipeline uses more cycles than the single-cycle simulator:**
   The base simulator's single-cycle engine (`execute_cycle`) executes all five phases of an instruction in a single `cycle_count++` tick (`CPI = 1.000` by construction). By contrast, the 5-stage pipeline executes one stage per tick: it pays 4 initial fill cycles before the first instruction retires, plus any stall cycles (waiting for RAW hazards) and flush cycles (wrong-path instructions killed by branches/jumps). In real hardware, pipelining reduces the critical path per cycle by roughly 5x, enabling a much higher clock frequency and shorter wall-clock runtime despite the higher cycle count; a cycle-count simulator does not model clock period.

2. **Why a data dependence costs 3, 2, or 1 stalls at distance 1, 2, or 3 without forwarding:**
   Because WB runs at the start of `pipeline_cycle` before ID reads `cpu_read_reg` (modeling write-in-first-half, read-in-second-half of the clock cycle), a consumer in ID can read the updated register in the cycle immediately *after* the producer is in `p->mem_wb` and retires in WB:
   - **Distance 1** (producer just entered EX in `p->id_ex`): consumer stalls for 3 cycles while the producer advances through `p->id_ex` (EX), `p->ex_mem` (MEM), and `p->mem_wb` (WB).
   - **Distance 2** (producer is in `p->ex_mem`): consumer stalls for 2 cycles (`p->ex_mem`, `p->mem_wb`).
   - **Distance 3** (producer is in `p->mem_wb`): consumer stalls for 1 cycle while WB writes the register, then reads the new value on the next cycle.

3. **Why a taken branch or jump costs 1 flush cycle:**
   Branches and jumps (`jal`, `jalr`) are resolved in EX. On the cycle EX decides to redirect, only one wrong-path instruction has been fetched and is sitting in `p->if_id` (entering ID). EX asserts `flush`, which clears `nx.id_ex` to a bubble and redirects IF to fetch from `redirect_pc`. Exactly 1 instruction slot is lost (`flushes++`).

4. **Why faults ride down the pipe to WB (precise exceptions):**
   IF speculatively fetches `pc + 4` every cycle, including past an exit `ecall` into `.data` (often `0x00000000`, which decodes as illegal) or past the end of memory. If IF or ID halted the CPU immediately, a younger wrong-path or post-`ecall` fault would kill the simulation before older valid instructions in EX/MEM/WB (such as the exit `ecall` itself) retired. Recording `PF_FETCH` / `PF_ILLEGAL` / `PF_MEM` in the latch and checking `fault != PF_NONE` only in WB guarantees precise exceptions in program order.

---

## 5. Design Choices

- **Stall / flush policy:** Exact Lab 2 specification policy:
  - Naive 3-latch RAW hazard detection in ID checking `p->id_ex`, `p->ex_mem`, and `p->mem_wb` against `s1 = ctrl.uses_rs1 ? d.rs1 : 0` and `s2 = ctrl.uses_rs2 ? d.rs2 : 0`, triggering when `valid && ctrl.reg_write && rd != 0 && rd == rs`.
  - No forwarding paths.
  - Branches (`beq`, `bne`, `blt`, `bge`, `bltu`, `bgeu`) and jumps (`jal`, `jalr`) resolved in EX; flush kills ID (`nx.id_ex`) and redirects IF to `redirect_pc`.
  - **Flush has priority over stall:** if EX redirects in the same cycle ID detects a hazard on the wrong-path instruction in `p->if_id`, no stall is taken or counted.
- **Reference alignment:** All measured cycle, retired, stall, flush, and CPI numbers match the reference figures in Section 6 of `LAB2_AGENT_SPEC.md` with zero deviations.
- **Diagnostics & build:** Added optional `RVSIM_TRACE` stderr cycle tracing (read in `pipeline_init`) and a `make check` target in `Makefile` running `./run_all.sh`.

---

## 6. Cycle-by-Cycle Hand Check of `tests/micro.s`

Program (`tests/micro.s`):
- `0x0000`: `I1` = `addi t0, zero, 5`
- `0x0004`: `I2` = `addi t1, t0, 10` (distance-1 RAW on `t0` with `I1`)
- `0x0008`: `I3` = `add  t2, t1, t0` (distance-1 RAW on `t1` with `I2`)
- `0x000c`: `I4` = `addi a7, zero, 10`
- `0x0010`: `I5` = `ecall`

Measured output of `RVSIM_TRACE=1 ./rvsim tests/micro.s --pipeline`:

```text
c1    IF=0000 ID=ffff EX=ffff MEM=ffff WB=ffff
c2    IF=0004 ID=0000 EX=ffff MEM=ffff WB=ffff
c3    IF=0004 ID=0004 EX=0000 MEM=ffff WB=ffff STALL
c4    IF=0004 ID=0004 EX=ffff MEM=0000 WB=ffff STALL
c5    IF=0004 ID=0004 EX=ffff MEM=ffff WB=0000 STALL
c6    IF=0008 ID=0004 EX=ffff MEM=ffff WB=ffff
c7    IF=0008 ID=0008 EX=0004 MEM=ffff WB=ffff STALL
c8    IF=0008 ID=0008 EX=ffff MEM=0004 WB=ffff STALL
c9    IF=0008 ID=0008 EX=ffff MEM=ffff WB=0004 STALL
c10   IF=000c ID=0008 EX=ffff MEM=ffff WB=ffff
c11   IF=0010 ID=000c EX=0008 MEM=ffff WB=ffff
c12   IF=0014 ID=0010 EX=000c MEM=0008 WB=ffff
c13   IF=0018 ID=0014 EX=0010 MEM=000c WB=0008
c14   IF=001c ID=0018 EX=0014 MEM=0010 WB=000c
c15   IF=0020 ID=001c EX=0018 MEM=0014 WB=0010
```

Walkthrough:
- **c1–c2:** `I1 (0000)` enters IF then ID; `I2 (0004)` enters IF in c2.
- **c3–c5:** `I1 (0000)` moves through EX -> MEM -> WB, retiring in c5 and writing `t0 = 5`. During all 3 cycles, `I2 (0004)` sees `I1` as a producer of `t0` in `id_ex`/`ex_mem`/`mem_wb`, stalls in ID (`IF=0004` stays frozen), and injects bubbles (`ffff`) into EX.
- **c6:** All producer latches are clear; `I2 (0004)` reads `t0 = 5` and advances to EX next cycle while `I3 (0008)` is fetched.
- **c7–c9:** `I3 (0008)` stalls 3 cycles waiting for `I2 (0004)` to write `t1 = 15` in c9.
- **c10–c15:** `I3`, `I4`, and `I5` (`0010`, `ecall`) flow down the diagonal without further hazards. `I5` reaches WB in c15 and exits (`cycles=15, retired=5, stalls=6, flushes=0, CPI=3.000`).

