CC      := gcc
# -frounding-math: the F extension changes the FPU rounding mode at run
# time (fesetround); this stops the compiler assuming round-to-nearest.
CFLAGS  := -std=c11 -Wall -Wextra -g -O0 -MMD -MP -frounding-math \
           -Icore -Iisa -Iassembler -Iui -Ipipeline
LDFLAGS := -lm

BIN := rvsim

SRCS := \
	core/cpu_state.c \
	core/memory.c \
	core/isa_registry.c \
	core/decoder.c \
	core/executor.c \
	core/sim_engine.c \
	pipeline/control.c \
	pipeline/pipeline.c \
	pipeline/hazard.c \
	pipeline/hazard_none.c \
	pipeline/hazard_stall.c \
	pipeline/hazard_forward.c \
	pipeline/branch_predictor.c \
	pipeline/bp_static.c \
	pipeline/bp_dynamic.c \
	isa/rv32i_base.c \
	isa/rv32m.c \
	isa/rv32f.c \
	isa/zicsr.c \
	assembler/assembler.c \
	ui/cli_display.c \
	main.c

OBJS := $(SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean run memcheck

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Example: make run PROG=../sample.s
run: $(BIN)
	./$(BIN) $(PROG) --batch

# Requires valgrind installed. Example: make memcheck PROG=../sample.s
memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full ./$(BIN) $(PROG) --batch

clean:
	rm -f $(OBJS) $(DEPS) $(BIN)

-include $(DEPS)
