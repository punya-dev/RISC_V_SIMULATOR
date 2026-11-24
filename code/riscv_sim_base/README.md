# Lab 5 skeleton

The complete Lab 5 directory and file structure of the RV32IMF pipeline
simulator. Everything you are **not** meant to write is complete. The
logic you **are** meant to write is empty and marked `TODO(labN)`:

```bash
grep -rn "TODO(lab" .
```

```
core/        complete: CPU state incl. FP register file f0..f31 + fcsr, memory,
             ISA registry, decoder, single-cycle executor, SimEngine
assembler/   complete: incl. .float, FP/CSR pseudo-ops, optional rounding mode
ui/          complete: identical to the evaluator's ref_ui/ (do not edit, it is replaced)
isa/
  rv32i_base.c   complete
  rv32m.c        registration table given; 8 handler bodies empty      (Lab 4)
  rv32f.c        registration table given; all handler bodies empty    (Lab 5)
  zicsr.c        registration table given; 6 handler bodies empty      (Lab 5)
pipeline/
  pipeline.h, control.h, hazard.h, branch_predictor.h   complete (the interfaces)
  pipeline.c     latches, pipeline_step() order, stats/views given;
                 stage_if/id/ex/mem/wb, forward_mux, predict_next empty (Labs 1-3, 5)
  control.c      control_from_instr() empty                             (Labs 1, 5)
  hazard_none.c  complete;  hazard_stall.c / hazard_forward.c empty     (Lab 2)
  bp_static.c    not-taken complete, btfn empty; bp_dynamic.c empty     (Lab 3)
main.c, Makefile complete
```

Build and run:

```bash
make
./rvsim ../sample.s --batch --single-cycle     # RV32I already works single-cycle
./rvsim ../sample.s --batch                    # pipeline: prints the Lab 5 dump, nothing retires yet
python3 ../evaluate.py . ../sample.s ../sample.txt -v
```

Keep the statistics text in `pipeline_format_stats()` exactly as it is.
The evaluator parses it.
