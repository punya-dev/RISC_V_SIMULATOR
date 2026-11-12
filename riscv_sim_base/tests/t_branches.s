# all six branch types, taken and not taken, forward and backward
.text
.globl main
main:
    addi t0, zero, -1
    addi t1, zero, 1
    addi s0, zero, 0
    beq  t0, t1, bad1
    addi s0, s0, 1
bad1:
    bne  t0, t1, ok2
    addi s0, s0, 100
ok2:
    blt  t0, t1, ok3
    addi s0, s0, 100
ok3:
    bge  t1, t0, ok4
    addi s0, s0, 100
ok4:
    bltu t1, t0, ok5
    addi s0, s0, 100
ok5:
    bgeu t0, t1, ok6
    addi s0, s0, 100
ok6:
    addi s1, zero, 3
back:
    addi s1, s1, -1
    bne  s1, zero, back
    addi a7, zero, 10
    ecall
