.globl _start
_start:
    lui  s0, 0x2
    li   t0, -5
    li   t1, 3
    sub  t2, t0, t1        # -8
    sw   t2, 0(s0)
    sll  t2, t1, t1        # 24
    sw   t2, 4(s0)
    sra  t2, t0, t1        # -1
    sw   t2, 8(s0)
    srl  t2, t0, t1        # 0x1FFFFFFF
    sw   t2, 12(s0)
    slt  t2, t0, t1        # 1
    sw   t2, 16(s0)
    sltu t2, t0, t1        # 0
    sw   t2, 20(s0)
    xor  t2, t0, t1        # -8
    sw   t2, 24(s0)
    slti t2, t0, -4        # 1
    sw   t2, 28(s0)
    sltiu t2, t1, -1       # 1
    sw   t2, 32(s0)
    srai t2, t0, 1         # -3
    sw   t2, 36(s0)
    srli t2, t0, 28        # 15
    sw   t2, 40(s0)
    xori t2, t1, 0x55      # 0x56
    sw   t2, 44(s0)
    ori  t2, t1, 0x70      # 0x73
    sw   t2, 48(s0)
    andi t2, t0, 0xFF      # 0xFB
    sw   t2, 52(s0)
    lui  t2, 0xABCDE
    addi t2, t2, 0x123     # 0xABCDE123
    sw   t2, 56(s0)
    sb   t2, 64(s0)        # 0x23
    sh   t2, 66(s0)        # E123 at +66
    lb   t3, 64(s0)        # 0x23
    sw   t3, 72(s0)
    lh   t3, 66(s0)        # 0xFFFFE123
    sw   t3, 76(s0)
    lhu  t3, 66(s0)        # 0xE123
    sw   t3, 80(s0)
    lbu  t3, 67(s0)        # 0xE1
    sw   t3, 84(s0)
    lb   t3, 67(s0)        # 0xFFFFFFE1
    sw   t3, 88(s0)
    auipc t3, 0            # pc of this instr
    sw   t3, 92(s0)
    bne  t0, t1, b1
    li   t4, 99
b1: blt  t0, t1, b2
    li   t4, 99
b2: bltu t1, t0, b3
    li   t4, 99
b3: bge  t1, t0, b4
    li   t4, 99
b4: bgeu t0, t1, b5
    li   t4, 99
b5: li   t4, 7
    sw   t4, 96(s0)
    jal  ra, f
    sw   ra, 100(s0)
    ebreak
f:  jalr x0, 0(ra)
