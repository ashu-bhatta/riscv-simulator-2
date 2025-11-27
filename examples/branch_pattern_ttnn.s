# Branch Pattern: TTNN (Two Taken, Two Not Taken)
# Pattern: T, T, N, N, T, T, N, N, ...
# Expected: x10=28, x11=92, x12=120
    .text

main:
    addi x1, x0, 0        # i = 0
    addi x2, x0, 16       # iterations = 16
    addi x3, x0, 0        # pattern_state (0-3)
    addi x10, x0, 0       # taken_sum = 0
    addi x11, x0, 0       # not_taken_sum = 0

loop:
    # Calculate if we should take (pattern_state < 2)
    addi x6, x0, 2
    blt  x3, x6, taken    # if pattern_state < 2 -> TAKEN

    # Not-taken path:
    add  x11, x11, x1     # not_taken_sum += i
    jal  x0, cont

taken:
    add  x10, x10, x1     # taken_sum += i

cont:
    # Update pattern state (cycle 0->1->2->3->0)
    addi x3, x3, 1
    addi x6, x0, 4
    blt x3, x6, no_reset
    addi x3, x0, 0        # reset to 0

no_reset:
    addi x1, x1, 1        # i++
    blt  x1, x2, loop

    add  x12, x10, x11    # x12 = taken_sum + not_taken_sum = 120
    nop
    nop
