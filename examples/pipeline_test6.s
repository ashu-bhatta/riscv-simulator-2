# Branch: beq x3, x0, taken
# Pattern (Alternating): N, T, N, T, ...
# 1-bit vs 2-bit
    .text

main:
    addi x1, x0, 0        # i = 0
    addi x2, x0, 8        # iterations = 8
    addi x3, x0, 0        # toggle
    addi x4, x0, 0        # taken_count
    addi x5, x0, 0        # not_taken_count

loop:
    xori x3, x3, 1        # toggle = toggle ^ 1

    beq  x3, x0, taken    # if toggle == 0 -> TAKEN, else NOT taken

    # Not-taken path:
    addi x5, x5, 1        # not_taken_count++
    jal  x0, cont

taken:
    addi x4, x4, 1        # taken_count++

cont:
    addi x1, x1, 1        # i++
    blt  x1, x2, loop

    nop
    nop
