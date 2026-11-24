#include "branch_predictor.h"
#include <string.h>

/* ---------------- 1-bit: last outcome ---------------- */

/* table[i] = 1 if the last branch that used entry i was taken.
 * Starts all-not-taken. A loop branch mispredicts twice per loop
 * execution: on the exit, and again on the first iteration next time. */

static void reset_1bit(BPState *s) {
    memset(s->table, 0, sizeof(s->table));
}

static int predict_1bit(const BPState *s, uint32_t pc, uint32_t target) {
    (void)s; (void)pc; (void)target;
    return 0; /* TODO(lab3) */
}

static void update_1bit(BPState *s, uint32_t pc, uint32_t target, int taken) {
    (void)s; (void)pc; (void)target; (void)taken;
    /* TODO(lab3) */
}

const BranchPredictor bp_1bit = {
    "1bit",
    "dynamic: 1-bit last-outcome table (64 entries)",
    reset_1bit,
    predict_1bit,
    update_1bit,
};

/* ---------------- 2-bit saturating counter ---------------- */

/* table[i] in 0..3:  0 strongly not taken, 1 weakly not taken,
 *                    2 weakly taken,       3 strongly taken.
 * Predict taken when >= 2. Starts at 1 (weakly not taken). One unusual
 * outcome (a loop exit) only moves a strong counter to weak, so the next
 * execution of the loop is still predicted correctly. */

#define CTR_WEAK_NOT_TAKEN 1
#define CTR_MAX 3

static void reset_2bit(BPState *s) {
    memset(s->table, CTR_WEAK_NOT_TAKEN, sizeof(s->table));
}

static int predict_2bit(const BPState *s, uint32_t pc, uint32_t target) {
    (void)s; (void)pc; (void)target;
    return 0; /* TODO(lab3) */
}

static void update_2bit(BPState *s, uint32_t pc, uint32_t target, int taken) {
    (void)s; (void)pc; (void)target; (void)taken;
    /* TODO(lab3): saturating counter 0..CTR_MAX */
}

const BranchPredictor bp_2bit = {
    "2bit",
    "dynamic: 2-bit saturating counters (64 entries, start weakly not taken)",
    reset_2bit,
    predict_2bit,
    update_2bit,
};
