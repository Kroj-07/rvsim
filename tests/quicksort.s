.globl _start
_start:
    lui  sp, 0x10
    lui  s0, 0x1
    li t0,6
    sw t0,0(s0)
    li t0,4
    sw t0,4(s0)
    li t0,3
    sw t0,8(s0)
    li t0,2
    sw t0,12(s0)
    li t0,1
    sw t0,16(s0)
    li t0,8
    sw t0,20(s0)
    li t0,9
    sw t0,24(s0)
    li a1,0
    li a2,6
    jal ra, quicksort
done:
    j done

quicksort:
    bge  a1, a2, qs_ret
    addi sp, sp, -16
    sw   ra, 12(sp)
    sw   s1, 8(sp)
    sw   s2, 4(sp)
    sw   s3, 0(sp)
    mv   s2, a1
    mv   s3, a2
    slli t0, s3, 2
    add  t0, t0, s0
    lw   t1, 0(t0)
    addi t2, s2, -1
    mv   t3, s2
part_loop:
    bge  t3, s3, part_end
    slli t4, t3, 2
    add  t4, t4, s0
    lw   t5, 0(t4)
    blt  t1, t5, part_next
    addi t2, t2, 1
    slli t6, t2, 2
    add  t6, t6, s0
    lw   a3, 0(t6)
    sw   t5, 0(t6)
    sw   a3, 0(t4)
part_next:
    addi t3, t3, 1
    j    part_loop
part_end:
    addi t2, t2, 1
    slli t6, t2, 2
    add  t6, t6, s0
    lw   a3, 0(t6)
    sw   t1, 0(t6)
    sw   a3, 0(t0)
    mv   s1, t2
    mv   a1, s2
    addi a2, s1, -1
    jal  ra, quicksort
    addi a1, s1, 1
    mv   a2, s3
    jal  ra, quicksort
    lw   ra, 12(sp)
    lw   s1, 8(sp)
    lw   s2, 4(sp)
    lw   s3, 0(sp)
    addi sp, sp, 16
qs_ret:
    ret
