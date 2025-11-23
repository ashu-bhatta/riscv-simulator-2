# Branch Pattern: TNTNTNTN (Alternating, starting with Taken)
# Pattern: T, N, T, N, T, N, T, N, ...
# Expected: x10=56, x11=64, x12=120
    .text

main:
    addi x1, x0, 0        # i = 0
    addi x2, x0, 16       # iterations = 16
    addi x3, x0, 1        # toggle (start with 1 so first branch is taken)
    addi x10, x0, 0       # taken_sum = 0
    addi x11, x0, 0       # not_taken_sum = 0

loop:
    beq  x3, x0, not_taken    # if toggle == 0 -> NOT TAKEN

    # Taken path:
    add  x10, x10, x1     # taken_sum += i
    jal  x0, cont

not_taken:
    add  x11, x11, x1     # not_taken_sum += i

cont:
    xori x3, x3, 1        # toggle = toggle ^ 1
    addi x1, x1, 1        # i++
    blt  x1, x2, loop

    add  x12, x10, x11    # x12 = taken_sum + not_taken_sum = 120
    nop
    nop
