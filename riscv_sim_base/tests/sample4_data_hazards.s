.text
.globl main
main:
 addi t0, zero, 5
 addi t1, t0, 10
 add t2, t1, t0
 sub t3, t2, t1
 addi t4, zero, 8
 addi t5, zero, 9
 add t6, t4, zero
 addi s0, zero, 1
 addi s1, zero, 2
 add s2, s0, s1
 addi zero, t0, 99
 add s3, zero, t1
 li gp, data
 lw s4, 0(gp)
 addi s5, s4, 1
 lw s6, 4(gp)
 add s7, s6, s5
 add s8, s7, s5
 sw s8, 8(gp)
 lw s9, 8(gp)
 addi s10, zero, 4
 addi s11, zero, 4
 beq s10, s11, show
 addi s9, zero, 0
show:
 add a0, s9, zero
 addi a7, zero, 1
 ecall
 addi a7, zero, 10
 ecall
.data
data:
 .word 7
 .word 20
 .word 0
