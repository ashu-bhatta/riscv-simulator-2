# Branch Pattern: Always Not Taken
# Pattern: N, N, N, N, N, N, N, N
# Expected: x10=136, x11=16, x12=152
    .text

main:
    addi x1, x0, 0        # value = 0
    addi x2, x0, 16       # limit = 16
    addi x3, x0, 0        # counter = 0
    addi x10, x0, 0       # sum = 0
    addi x11, x0, 0       # not_taken_count = 0

loop:
    addi x1, x1, 1        # value++
    
    add x6, x0, x0        # load impossible value
    beq  x1, x6, done     # always not taken (x1 never equals 0)
    
    # Branch not taken path
    addi x11, x11, 1      # not_taken_count++
    add  x10, x10, x1     # sum += value
    
    addi x3, x3, 1        # counter++
    blt  x3, x2, loop     # loop while counter < 16
    
    beq  x0, x0, done     # unconditional jump to done

done:
    add  x12, x10, x11    # x12 = sum + not_taken_count
    add x5, x0, x0 
    add x5, x0, x0 
    nop