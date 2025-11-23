# Branch Pattern: Biased (75% Taken, 25% Not Taken)
# Pattern: T, T, T, N, T, T, T, N, ...
# Expected: x10=84, x11=36, x12=120
    .text

main:
    addi x1, x0, 0        # i = 0
    addi x2, x0, 16       # iterations = 16
    addi x3, x0, 0        # cycle counter (0-3)
    addi x10, x0, 0       # taken_sum = 0
    addi x11, x0, 0       # not_taken_sum = 0

loop:
    # Take if cycle != 3
    addi x6, x0, 3
    beq  x3, x6, not_taken

    # Taken path (75% of the time):
    add  x10, x10, x1     # taken_sum += i
    jal  x0, cont

not_taken:
    add  x11, x11, x1     # not_taken_sum += i

cont:
    # Update cycle (0->1->2->3->0)
    addi x3, x3, 1
    addi x6, x0, 4
    blt x3, x6, no_reset
    addi x3, x0, 0

no_reset:
    addi x1, x1, 1        # i++
    blt  x1, x2, loop

    add  x12, x10, x11    # x12 = taken_sum + not_taken_sum = 120
    nop
    nop
