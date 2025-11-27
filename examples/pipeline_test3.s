    .data
val:    .word 7
res:    .word 0

    .text
main:
    la   x10, val         # x10 -> &val
    lw   x1, 0(x10)       # x1 = 7
    add  x2, x1, x1       # RAW load-use: x2 should be 14
    la   x11, res
    sw   x2, 0(x11)       # res = 14

    nop
    nop