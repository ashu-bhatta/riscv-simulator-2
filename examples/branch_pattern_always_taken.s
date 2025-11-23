# Branch Pattern: Always Taken (except last)
# Pattern: T, T, T, T, T, T, T, T, N
# Expected: x10=120, x11=16, x12=136
    .text

main:
    addi x1, x0, 0        # i = 0
    addi x2, x0, 16       # iterations = 16
    addi x10, x0, 0       # sum = 0
    addi x11, x0, 0       # count = 0

loop:
    add  x10, x10, x1     # sum += i (0+1+2+...+15 = 120)
    addi x11, x11, 1      # count++
    addi x1, x1, 1        # i++
    blt  x1, x2, loop     # always taken until i >= 16

    add  x12, x10, x11    # x12 = sum + count = 120 + 16 = 136
    nop
    nop
