addi x1, x0, 1        # x1 = 1
addi x2, x0, 2        # x2 = 2
addi x3, x0, 3        # x3 = 3
addi x4, x0, 4        # x4 = 4

add  x5, x1, x2       # x5 = 3
add  x6, x3, x4       # x6 = 7
sub  x7, x6, x5       # x7 = 7 - 3 = 4
and  x8, x5, x6       # x8 = 3 & 7 = 3

# End
nop
nop