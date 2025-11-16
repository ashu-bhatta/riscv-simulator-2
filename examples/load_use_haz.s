.data
        my_val: .word 42
        .text
        la x5, my_val
        lw x6, 0(x5)        # Load word from memory
        addi x7, x6, 1      # Load-use hazard. Pipeline must stall. Should produce 43.