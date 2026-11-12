# all load/store widths, sign/zero extension, lui/auipc, shifts, slt/sltu, sub
.text
.globl main
main:
    li   gp, d
    lw   t0, 0(gp)
    addi t1, t0, 1
    lb   t2, 4(gp)
    lbu  t3, 4(gp)
    lh   t4, 4(gp)
    lhu  t5, 4(gp)
    sb   t1, 8(gp)
    sh   t1, 10(gp)
    lw   t6, 8(gp)
    lui  s0, 0x12345
    auipc s1, 0x1
    slli s2, t0, 3
    srai s3, t2, 1
    srli s4, t2, 1
    sub  s5, t0, t1
    slt  s6, t2, zero
    sltu s7, t2, zero
    addi a7, zero, 10
    ecall
.data
d:
    .word 5
    .word 0xfff0
    .word 0
