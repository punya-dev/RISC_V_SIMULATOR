# load from out-of-bounds address: oracle halts with MEM_FAULT
.text
.globl main
main:
    addi t2, zero, 1
    li   t0, 0x7ffffff0
    lw   t1, 0(t0)
    addi a7, zero, 10
    ecall
