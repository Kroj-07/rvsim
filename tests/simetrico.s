.globl _start
_start:
    lui  sp, 0x10
    lui  s0, 0x1
    li t0,6
    sw t0,0(s0)
    li t0,4
    sw t0,4(s0)
    sw t0,8(s0)
    li t0,1
    sw t0,12(s0)
    li t0,2
    sw t0,16(s0)
    sw t0,20(s0)
    li t0,1
    sw t0,24(s0)
    li a1,7
    jal ra, is_symmetric
done:
    j done

is_symmetric:
    li t0, 0
    li t1, 1
lvl:
    bge t0, a1, yes
    li t2, 0
cmp:
    bge t2, t1, nextlvl
    add t3, t0, t2
    slli t3, t3, 2
    add t3, t3, s0
    lw t4, 0(t3)
    add t5, t0, t1
    addi t5, t5, -1
    sub t5, t5, t2
    slli t5, t5, 2
    add t5, t5, s0
    lw t6, 0(t5)
    bne t4, t6, no
    addi t2, t2, 1
    j cmp
nextlvl:
    add t0, t0, t1
    slli t1, t1, 1
    j lvl
yes:
    li a0, 1
    ret
no:
    li a0, 0
    ret
