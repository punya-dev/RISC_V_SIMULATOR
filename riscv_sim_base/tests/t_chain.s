# long distance-1 chain + priority case (must give t1 = 2) + store/branch operand hazards
.text
.globl main
main:
    addi t0, zero, 1
    addi t0, t0, 1
    addi t1, t0, 0
    add  t2, t1, t1
    add  t3, t2, t2
    add  t4, t3, t3
    li   gp, buf
    sw   t4, 0(gp)
    lw   t5, 0(gp)
    beq  t5, t4, same
    addi t6, zero, 111
same:
    addi a7, zero, 10
    ecall
.data
buf:
    .word 0
