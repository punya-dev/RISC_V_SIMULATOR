# x0 is never a dependence and never written
.text
.globl main
main:
    addi zero, zero, 5
    add  t0, zero, zero
    addi zero, t0, 99
    add  t1, zero, t0
    addi t2, zero, 7
    add  zero, t2, t2
    sub  t3, t2, zero
    addi a7, zero, 10
    ecall
