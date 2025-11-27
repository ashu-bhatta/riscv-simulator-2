    .data
A:  .word 1
B:  .word 2
C:  .word 0

    .text
main:
    la   x10, A
    lw   x1, 0(x10)       # x1 = 1

    la   x11, B
    lw   x2, 0(x11)       # x2 = 2

    add  x3, x1, x2       # x3 = 3

    la   x12, C
    sw   x3, 0(x12)       # C = 3

    lw   x4, 0(x12)       # store->load hazard: x4 must read 3

    nop
    nop