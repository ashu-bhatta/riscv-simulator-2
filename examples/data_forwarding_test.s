addi x1, x0, 5        # x1 = 5
addi x2, x1, 3        # RAW: uses x1 => x2 = 8
add  x3, x2, x1       # RAW: uses x2, x1 => x3 = 13
sub  x4, x3, x2       # RAW: x4 = 13 - 8 = 5
add  x5, x4, x3       # RAW: x5 = 5 + 13 = 18