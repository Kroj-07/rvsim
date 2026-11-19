.globl _start
_start:
    la   a0, msg
    li   a7, 4
    ecall
    li   a0, 42
    li   a7, 1
    ecall
    li   a0, 10
    li   a7, 11
    ecall
    li   a7, 5
    ecall
    add  a0, a0, a0
    li   a7, 1
    ecall
    li   a0, 0
    li   a7, 10
    ecall
msg: .asciz "Hola RISC-V! Resultado: "
