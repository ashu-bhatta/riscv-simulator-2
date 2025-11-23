.text
addi x5, x0, 0
nop
nop
beq x5, x0, skip # Branch not taken
addi x6, x0, 10 # This should execute
beq x0, x0, end
skip:
addi x7, x0, 20 # This should be skipped
end:
nop
# Final register state should be x6=10, x7=0