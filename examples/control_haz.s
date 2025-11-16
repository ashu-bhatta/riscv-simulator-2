.text
        addi x5, x0, 1
        beq x5, x0, skip    # Branch not taken
        addi x6, x0, 10     # This should execute
        j end
        skip:
        addi x7, x0, 20     # This should be skipped
        end:
        # Final register state should be x6=10, x7=0