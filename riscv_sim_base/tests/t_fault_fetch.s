# jump out of bounds: instruction fetch fault
.text
.globl main
main:
    li   t0, 0x7ffffff0
    jalr zero, 0(t0)
    addi a7, zero, 10
    ecall
