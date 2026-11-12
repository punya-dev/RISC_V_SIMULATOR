# no exit ecall: falls off the end into zeros -> ILLEGAL_INSTRUCTION
.text
.globl main
main:
    addi t0, zero, 1
    addi t1, t0, 1
