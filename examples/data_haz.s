.text
        addi x5, x0, 5      # x5 = 5
        addi x6, x0, 10     # x6 = 10
        add x7, x5, x6      # RAW on x5, x6. Should produce 15.
        sub x8, x7, x5      # RAW on x7. Should produce 10.