# Pattern: T, T, T, …, T, N
# Branch: blt x1, x2, loop

    .text
main:
    addi x1, x0, 0        # i = 0
    addi x2, x0, 10       # limit = 10
    addi x3, x0, 0        # sum = 0

loop:
    add  x3, x3, x1       # sum += i
    addi x1, x1, 1        # i++
    blt  x1, x2, loop     # branch: taken for i=1..9, not taken at i=10

    nop
    nop
