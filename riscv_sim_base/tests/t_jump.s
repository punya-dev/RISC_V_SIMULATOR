# jal / jalr. The function sits BEFORE main so nothing executable follows the exit ecall.
.text
.globl main
f:
    addi t0, zero, 3
    jalr zero, 0(ra)
main:
    jal  ra, f
    addi a0, zero, 7
    addi a7, zero, 1
    ecall
    addi a7, zero, 10
    ecall
