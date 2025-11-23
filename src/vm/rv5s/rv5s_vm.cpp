 /* @file rv5s_vm.cpp
 * @brief RV5S (5-Stage Pipeline) VM implementation
 * @author Om Dave & Ashutosh Bhatta
 */

#include "vm/rv5s/rv5s_vm.h"
#include "utils.h"
#include "globals.h"
#include "common/instructions.h"
#include "config.h"

#include <cctype>
#include <cstdint>
#include <iostream>
#include <tuple>
#include <stack>  
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

using instruction_set::Instruction;
using instruction_set::get_instr_encoding;

// ============================================================================
// Constructor & Destructor
// ============================================================================

RV5SVM::RV5SVM() : VmBase() {
    FlushPipeline();
    total_cycles_ = 0;
    bubbles_inserted_ = 0;
    // Initialize branch predictor table (2-bit counters initialized to weakly not-taken = 1)
    bp_mask_ = kBpTableSize - 1;
    branch_table_.assign(RV5SVM::kBpTableSize, 1);
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

RV5SVM::~RV5SVM() = default;

// ============================================================================
// Pipeline Stage: IF (Instruction Fetch)
// ============================================================================

void RV5SVM::IF() {
    // Only fetch if PC write is enabled (not stalled)
    if (!pc_write_) {
        return; // Stalled, don't fetch
    }

    // Only fetch if within program bounds
    if (program_counter_ >= program_size_) {
        if_id_reg_.valid = false;
        return;
    }

    // Fetch instruction from memory
    uint32_t instruction = memory_controller_.ReadWord(program_counter_);

    // Update IF/ID register only if write is enabled
    if (if_id_write_) {
        if_id_reg_.pc = program_counter_;
        if_id_reg_.instruction = instruction;
        if_id_reg_.valid = true;
    }

    UpdateProgramCounter(4);
}

// ============================================================================
// Pipeline Stage: ID (Instruction Decode)
// ============================================================================

void RV5SVM::ID() {
    
    if (hazard_stall_cycles_ > 0) {
        pc_write_ = false;
        if_id_write_ = false;
        InsertBubble();
        hazard_stall_cycles_--;
        return;
    }

    // If IF/ID register is not valid, propagate a bubble
    if (!if_id_reg_.valid) {
        id_ex_reg_.Reset();
        return;
    }

    uint32_t instruction = if_id_reg_.instruction;
    uint64_t pc = if_id_reg_.pc;

    // Decode instruction fields
    uint8_t opcode = instruction & 0b1111111;
    // uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t funct7 = (instruction >> 25) & 0b1111111;
    uint8_t rs1 = (instruction >> 15) & 0b11111;
    uint8_t rs2 = (instruction >> 20) & 0b11111;
    // uint8_t rs3 = (instruction >> 27) & 0b11111;
    uint8_t rd = (instruction >> 7) & 0b11111;

    // Generate control signals
    control_unit_.SetControlSignals(instruction);

    // Read source registers
    uint64_t rs1_val = registers_.ReadGpr(rs1);
    uint64_t rs2_val = registers_.ReadGpr(rs2);

    // For floating-point instructions, read from FPR
    if (instruction_set::isFInstruction(instruction) ||
        instruction_set::isDInstruction(instruction)) {
        rs1_val = registers_.ReadFpr(rs1);
        rs2_val = registers_.ReadFpr(rs2);

        // For FP memory ops and conversions, rs1 might be GPR (base address)
        if (funct7 == 0b1101000 || funct7 == 0b1111000 ||
            funct7 == 0b1101001 || funct7 == 0b1111001 ||
            opcode == 0b0000111 || opcode == 0b0100111) {
            rs1_val = registers_.ReadGpr(rs1);
        }
    }

    // Generate immediate
    int32_t imm = ImmGenerator(instruction);

    uint64_t detect_rs1 = rs1;
    uint64_t detect_rs2 = rs2;

    uint8_t stalls_needed = DetectHazard(detect_rs1, detect_rs2);
    if (globals::pipeline_hazard_detection_enabled && stalls_needed != 0) {
        // Insert one bubble this cycle and schedule the remaining stalls
        pc_write_ = false;
        if_id_write_ = false;
        InsertBubble();
        if (stalls_needed > 1) hazard_stall_cycles_ = static_cast<uint8_t>(stalls_needed - 1);
    } else {
        pc_write_ = true;
        if_id_write_ = true;
        // Populate ID/EX register
        id_ex_reg_.pc = pc;
        id_ex_reg_.rs1_val = rs1_val;
        id_ex_reg_.rs2_val = rs2_val;
        id_ex_reg_.imm = static_cast<int64_t>(imm);
        id_ex_reg_.rs1_addr = rs1;
        id_ex_reg_.rs2_addr = rs2;
        id_ex_reg_.rd_addr = rd;
        id_ex_reg_.control = control_unit_.GetControlSignals();
        id_ex_reg_.instruction = instruction;
        id_ex_reg_.valid = true;
        id_ex_reg_.branch_flag = false;
        id_ex_reg_.branch_target = 0;

        // If this is a branch instruction, compute target and consult predictor
        if (id_ex_reg_.control.branch) {
            id_ex_reg_.branch_target = static_cast<int64_t>(pc) + static_cast<int64_t>(id_ex_reg_.imm);
            if (globals::branch_prediction_mode > 0) {
                id_ex_reg_.predicted_taken = GetBranchPrediction(pc);
                if (id_ex_reg_.predicted_taken) {
                    // If predicted taken, update PC to branch target so IF fetches correct instruction
                    program_counter_ = static_cast<uint64_t>(id_ex_reg_.branch_target);
                    // Also invalidate the instruction currently in IF/ID since we redirected fetch
                    if_id_reg_.Reset();
                }
            } else {
                id_ex_reg_.predicted_taken = false;
            }
        }
    }
}

// ============================================================================
// Pipeline Stage: EX (Execute)
// ============================================================================

void RV5SVM::EX() {
    // If ID/EX register is not valid, propagate a bubble
    if (!id_ex_reg_.valid) {
        ex_mem_reg_.Reset();
        return;
    }

    uint32_t instruction = id_ex_reg_.instruction;
    uint8_t opcode = instruction & 0b1111111;
    uint8_t funct3 = (instruction >> 12) & 0b111;

    // Handle special cases: syscall, float, double, CSR
    if (opcode == get_instr_encoding(Instruction::kecall).opcode &&
        funct3 == get_instr_encoding(Instruction::kecall).funct3) {
        HandleSyscall();
        ex_mem_reg_.Reset();
        return;
    }

    if (instruction_set::isFInstruction(instruction)) {
        ExecuteFloat(id_ex_reg_);
    }
    else if (instruction_set::isDInstruction(instruction)) {
        ExecuteDouble(id_ex_reg_);
    }
    else if (opcode == 0b1110011) { // CSR
        ExecuteCsr(id_ex_reg_);
    }
    else {
        ExecuteAlu(id_ex_reg_);
    }

    // Populate EX/MEM register
    ex_mem_reg_.pc = id_ex_reg_.pc;
    ex_mem_reg_.alu_result = current_alu_result_;
    ex_mem_reg_.rs2_val = id_ex_reg_.rs2_val;
    ex_mem_reg_.rd_addr = id_ex_reg_.rd_addr;
    ex_mem_reg_.control = id_ex_reg_.control;
    ex_mem_reg_.instruction = instruction;
    ex_mem_reg_.valid = true;
    ex_mem_reg_.branch_taken = id_ex_reg_.branch_flag;
    ex_mem_reg_.branch_target = id_ex_reg_.branch_target;
    ex_mem_reg_.predicted_taken = id_ex_reg_.predicted_taken;
}

void RV5SVM::ExecuteAlu(ID_EX_Register& id_ex) {
    uint32_t instruction = id_ex.instruction;
    uint8_t opcode = instruction & 0b1111111;
    uint8_t funct3 = (instruction >> 12) & 0b111;

    uint64_t operand1 = id_ex.rs1_val;
    uint64_t operand2 = id_ex.rs2_val;

    // Apply data forwarding if enabled: check if operand sources should be forwarded
    if (globals::pipeline_forwarding_enabled) {
        if (id_ex.rs1_addr != 0) {
            operand1 = ResolveForwarding(id_ex.rs1_addr, operand1);
        }
        if (!id_ex.control.alu_src && id_ex.rs2_addr != 0) {
            // Only forward rs2 when it's coming from a register (not an immediate)
            operand2 = ResolveForwarding(id_ex.rs2_addr, operand2);
        }
    }

    // Select second operand: register or immediate
    if (id_ex.control.alu_src) {
        operand2 = static_cast<uint64_t>(id_ex.imm);
    }

    // Perform ALU operation
    bool overflow = false;
    alu::AluOp aluOperation = control_unit_.GetAluSignal(instruction, id_ex.control.alu_op);
    std::tie(current_alu_result_, overflow) = alu_.execute(aluOperation, operand1, operand2);

    // Handle branches and jumps
    if (id_ex.control.branch) {
        if (opcode == get_instr_encoding(Instruction::kjalr).opcode) {
            // TODO: Test and update
            // JALR: target = (rs1 + imm) & ~1
            current_return_address_ = id_ex.pc + 4;
            id_ex.branch_target = current_alu_result_ & ~1ULL;
            id_ex.branch_flag = true;
            current_alu_result_ = current_return_address_; // Write PC+4 to rd
        }
        else if (opcode == get_instr_encoding(Instruction::kjal).opcode) {
            // TODO: Test and update
            // JAL: target = PC + imm
            current_return_address_ = id_ex.pc + 4;
            id_ex.branch_target = id_ex.pc + id_ex.imm;
            id_ex.branch_flag = true;
            current_alu_result_ = current_return_address_; // Write PC+4 to rd
        }
        else if (opcode==get_instr_encoding(Instruction::kbeq).opcode ||
               opcode==get_instr_encoding(Instruction::kbne).opcode ||
               opcode==get_instr_encoding(Instruction::kblt).opcode ||
               opcode==get_instr_encoding(Instruction::kbge).opcode ||
               opcode==get_instr_encoding(Instruction::kbltu).opcode ||
               opcode==get_instr_encoding(Instruction::kbgeu).opcode) { // Conditional branches
            bool branch_condition = false;
            switch (funct3) {
            case 0b000: branch_condition = (current_alu_result_ == 0); break; // BEQ
            case 0b001: branch_condition = (current_alu_result_ != 0); break; // BNE
            case 0b100: branch_condition = (current_alu_result_ == 1); break; // BLT
            case 0b101: branch_condition = (current_alu_result_ == 0); break; // BGE
            case 0b110: branch_condition = (current_alu_result_ == 1); break; // BLTU
            case 0b111: branch_condition = (current_alu_result_ == 0); break; // BGEU
            }

            if (branch_condition) {
                id_ex.branch_target = id_ex.pc + id_ex.imm;
                id_ex.branch_flag = true;
            }
        }
    }

    // Handle AUIPC
    if (opcode == get_instr_encoding(Instruction::kauipc).opcode) {
        current_alu_result_ = id_ex.pc + (id_ex.imm << 12);
    }
}

//TODO: Test and update
void RV5SVM::ExecuteFloat(ID_EX_Register& id_ex) {
    uint32_t instruction = id_ex.instruction;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t rs3 = (instruction >> 27) & 0b11111;
    uint8_t rm = funct3;

    if (rm == 0b111) {
        rm = registers_.ReadCsr(0x002); // Read rounding mode from fcsr
    }

    uint64_t rs1_val = id_ex.rs1_val;
    uint64_t rs2_val = id_ex.rs2_val;
    uint64_t rs3_val = registers_.ReadFpr(rs3);

    if (id_ex.control.alu_src) {
        rs2_val = static_cast<uint64_t>(id_ex.imm);
    }

    alu::AluOp aluOperation = control_unit_.GetAluSignal(instruction, id_ex.control.alu_op);
    uint8_t fcsr_status = 0;
    std::tie(current_alu_result_, fcsr_status) = alu::Alu::fpexecute(aluOperation, rs1_val, rs2_val, rs3_val, rm);

    registers_.WriteCsr(0x003, fcsr_status); // Update fflags
}

//TODO: Test and update
void RV5SVM::ExecuteDouble(ID_EX_Register& id_ex) {
    uint32_t instruction = id_ex.instruction;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t rs3 = (instruction >> 27) & 0b11111;
    uint8_t rm = funct3;

    uint64_t rs1_val = id_ex.rs1_val;
    uint64_t rs2_val = id_ex.rs2_val;
    uint64_t rs3_val = registers_.ReadFpr(rs3);

    if (id_ex.control.alu_src) {
        rs2_val = static_cast<uint64_t>(id_ex.imm);
    }

    alu::AluOp aluOperation = control_unit_.GetAluSignal(instruction, id_ex.control.alu_op);
    uint8_t fcsr_status = 0;
    std::tie(current_alu_result_, fcsr_status) = alu::Alu::dfpexecute(aluOperation, rs1_val, rs2_val, rs3_val, rm);
}

//TODO: Test and update
void RV5SVM::ExecuteCsr(ID_EX_Register& id_ex) {
    uint32_t instruction = id_ex.instruction;
    uint16_t csr = (instruction >> 20) & 0xFFF;
    uint64_t csr_val = registers_.ReadCsr(csr);

    csr_target_address_ = csr;
    csr_old_value_ = csr_val;
    csr_write_val_ = id_ex.rs1_val;
    csr_uimm_ = id_ex.rs1_addr;

    current_alu_result_ = csr_old_value_; // rd gets old CSR value
}

void RV5SVM::HandleSyscall() {
    int64_t syscall_number = registers_.ReadGpr(17);

    switch (syscall_number) {
    case SYSCALL_PRINT_INT: {
        if (!globals::vm_as_backend) {
            std::cout << "[Syscall output: ";
        }
        else {
            std::cout << "VM_STDOUT_START";
        }
        std::cout << static_cast<int64_t>(registers_.ReadGpr(10));
        if (!globals::vm_as_backend) {
            std::cout << "]" << std::endl;
        }
        else {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_FLOAT: {
        if (!globals::vm_as_backend) {
            std::cout << "[Syscall output: ";
        }
        else {
            std::cout << "VM_STDOUT_START";
        }
        float float_value;
        uint64_t raw = registers_.ReadGpr(10);
        std::memcpy(&float_value, &raw, sizeof(float_value));
        std::cout << std::setprecision(std::numeric_limits<float>::max_digits10) << float_value;
        if (!globals::vm_as_backend) {
            std::cout << "]" << std::endl;
        }
        else {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_DOUBLE: {
        if (!globals::vm_as_backend) {
            std::cout << "[Syscall output: ";
        }
        else {
            std::cout << "VM_STDOUT_START";
        }
        double double_value;
        uint64_t raw = registers_.ReadGpr(10);
        std::memcpy(&double_value, &raw, sizeof(double_value));
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10) << double_value;
        if (!globals::vm_as_backend) {
            std::cout << "]" << std::endl;
        }
        else {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_STRING: {
        if (!globals::vm_as_backend) {
            std::cout << "[Syscall output: ";
        }
        PrintString(registers_.ReadGpr(10));
        if (!globals::vm_as_backend) {
            std::cout << "]" << std::endl;
        }
        break;
    }
    case SYSCALL_EXIT: {
        stop_requested_ = true;
        if (!globals::vm_as_backend) {
            std::cout << "VM_EXIT" << std::endl;
        }
        output_status_ = "VM_EXIT";
        std::cout << "Exited with exit code: " << registers_.ReadGpr(10) << std::endl;
        exit(0);
        break;
    }
    case SYSCALL_READ: {
        uint64_t file_descriptor = registers_.ReadGpr(10);
        uint64_t buffer_address = registers_.ReadGpr(11);
        uint64_t length = registers_.ReadGpr(12);

        if (file_descriptor == 0) {
            std::string input;
            {
                std::cout << "VM_STDIN_START" << std::endl;
                output_status_ = "VM_STDIN_START";
                std::unique_lock<std::mutex> lock(input_mutex_);
                input_cv_.wait(lock, [this]() { return !input_queue_.empty(); });
                output_status_ = "VM_STDIN_END";
                std::cout << "VM_STDIN_END" << std::endl;
                input = input_queue_.front();
                input_queue_.pop();
            }

            for (size_t i = 0; i < input.size() && i < length; ++i) {
                memory_controller_.WriteByte(buffer_address + i, static_cast<uint8_t>(input[i]));
            }
            if (input.size() < length) {
                memory_controller_.WriteByte(buffer_address + input.size(), '\0');
            }

            registers_.WriteGpr(10, std::min(static_cast<uint64_t>(length), static_cast<uint64_t>(input.size())));
        }
        else {
            std::cerr << "Unsupported file descriptor: " << file_descriptor << std::endl;
        }
        break;
    }
    case SYSCALL_WRITE: {
        uint64_t file_descriptor = registers_.ReadGpr(10);
        uint64_t buffer_address = registers_.ReadGpr(11);
        uint64_t length = registers_.ReadGpr(12);

        if (file_descriptor == 1) {
            std::cout << "VM_STDOUT_START";
            output_status_ = "VM_STDOUT_START";
            uint64_t bytes_printed = 0;
            for (uint64_t i = 0; i < length; ++i) {
                char c = memory_controller_.ReadByte(buffer_address + i);
                std::cout << c;
                bytes_printed++;
            }
            std::cout << std::flush;
            output_status_ = "VM_STDOUT_END";
            std::cout << "VM_STDOUT_END" << std::endl;
            registers_.WriteGpr(10, bytes_printed);
        }
        else {
            std::cerr << "Unsupported file descriptor: " << file_descriptor << std::endl;
        }
        break;
    }
    default: {
        std::cerr << "Unknown syscall number: " << syscall_number << std::endl;
        break;
    }
    }
}

// ============================================================================
// Pipeline Stage: MEM (Memory Access)
// ============================================================================

void RV5SVM::MEM() {
    // If EX/MEM register is not valid, propagate a bubble
    if (!ex_mem_reg_.valid) {
        mem_wb_reg_.prev_alu_result = mem_wb_reg_.alu_result;
        mem_wb_reg_.prev_memory_result = mem_wb_reg_.memory_result;
        mem_wb_reg_.prev_rd_addr = mem_wb_reg_.rd_addr;
        mem_wb_reg_.prev_instruction = mem_wb_reg_.instruction;
        mem_wb_reg_.prev_valid = mem_wb_reg_.valid;
        mem_wb_reg_.prev_control = mem_wb_reg_.control;
        mem_wb_reg_.Reset();
        return;
    }

    uint32_t instruction = ex_mem_reg_.instruction;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t rs2 = (instruction >> 20) & 0b11111;

    current_mem_result_ = 0;

    // Memory Read (Load instructions)
    if (ex_mem_reg_.control.mem_read) {
        uint64_t addr = ex_mem_reg_.alu_result;

        if (instruction_set::isFInstruction(instruction)) {
            // FLW
            current_mem_result_ = memory_controller_.ReadWord(addr);
        }
        else if (instruction_set::isDInstruction(instruction)) {
            // FLD
            current_mem_result_ = memory_controller_.ReadDoubleWord(addr);
        }
        else {
            // Integer loads
            switch (funct3) {
            case 0b000: current_mem_result_ = static_cast<int8_t>(memory_controller_.ReadByte(addr)); break; // LB
            case 0b001: current_mem_result_ = static_cast<int16_t>(memory_controller_.ReadHalfWord(addr)); break; // LH
            case 0b010: current_mem_result_ = static_cast<int32_t>(memory_controller_.ReadWord(addr)); break; // LW
            case 0b011: current_mem_result_ = memory_controller_.ReadDoubleWord(addr); break; // LD
            case 0b100: current_mem_result_ = static_cast<uint8_t>(memory_controller_.ReadByte(addr)); break; // LBU
            case 0b101: current_mem_result_ = static_cast<uint16_t>(memory_controller_.ReadHalfWord(addr)); break; // LHU
            case 0b110: current_mem_result_ = static_cast<uint32_t>(memory_controller_.ReadWord(addr)); break; // LWU
            }
        }
    }

    // Memory Write (Store instructions)
    if (ex_mem_reg_.control.mem_write) {
        uint64_t addr = ex_mem_reg_.alu_result;

        if (instruction_set::isFInstruction(instruction)) {
            // FSW
            uint32_t val = registers_.ReadFpr(rs2) & 0xFFFFFFFF;
            std::vector<uint8_t> old_bytes(4);
            for (size_t i = 0; i < 4; ++i) old_bytes[i] = memory_controller_.ReadByte(addr + i);
            memory_controller_.WriteWord(addr, val);
            if (recording_enabled_) {
                std::vector<uint8_t> new_bytes(4);
                for (size_t i = 0; i < 4; ++i) new_bytes[i] = memory_controller_.ReadByte(addr + i);
                current_delta_.memory_changes.push_back({addr, old_bytes, new_bytes});
            }
        }
        else if (instruction_set::isDInstruction(instruction)) {
            // FSD
            std::vector<uint8_t> old_bytes(8);
            for (size_t i = 0; i < 8; ++i) old_bytes[i] = memory_controller_.ReadByte(addr + i);
            memory_controller_.WriteDoubleWord(addr, registers_.ReadFpr(rs2));
            if (recording_enabled_) {
                std::vector<uint8_t> new_bytes(8);
                for (size_t i = 0; i < 8; ++i) new_bytes[i] = memory_controller_.ReadByte(addr + i);
                current_delta_.memory_changes.push_back({addr, old_bytes, new_bytes});
            }
        }
        else {
            // Integer stores - re-read rs2 for correct value
            uint64_t store_val = registers_.ReadGpr(rs2);
            switch (funct3) {
            case 0b000: { // SB
                uint8_t oldb = memory_controller_.ReadByte(addr);
                memory_controller_.WriteByte(addr, store_val & 0xFF);
                if (recording_enabled_) {
                    std::vector<uint8_t> old_bytes = {oldb};
                    std::vector<uint8_t> new_bytes = {memory_controller_.ReadByte(addr)};
                    current_delta_.memory_changes.push_back({addr, old_bytes, new_bytes});
                }
                break;
            }
            case 0b001: { // SH
                std::vector<uint8_t> old_bytes(2);
                for (size_t i = 0; i < 2; ++i) old_bytes[i] = memory_controller_.ReadByte(addr + i);
                memory_controller_.WriteHalfWord(addr, store_val & 0xFFFF);
                if (recording_enabled_) {
                    std::vector<uint8_t> new_bytes(2);
                    for (size_t i = 0; i < 2; ++i) new_bytes[i] = memory_controller_.ReadByte(addr + i);
                    current_delta_.memory_changes.push_back({addr, old_bytes, new_bytes});
                }
                break;
            }
            case 0b010: { // SW
                std::vector<uint8_t> old_bytes(4);
                for (size_t i = 0; i < 4; ++i) old_bytes[i] = memory_controller_.ReadByte(addr + i);
                memory_controller_.WriteWord(addr, store_val & 0xFFFFFFFF);
                if (recording_enabled_) {
                    std::vector<uint8_t> new_bytes(4);
                    for (size_t i = 0; i < 4; ++i) new_bytes[i] = memory_controller_.ReadByte(addr + i);
                    current_delta_.memory_changes.push_back({addr, old_bytes, new_bytes});
                }
                break;
            }
            case 0b011: { // SD
                std::vector<uint8_t> old_bytes(8);
                for (size_t i = 0; i < 8; ++i) old_bytes[i] = memory_controller_.ReadByte(addr + i);
                memory_controller_.WriteDoubleWord(addr, store_val);
                if (recording_enabled_) {
                    std::vector<uint8_t> new_bytes(8);
                    for (size_t i = 0; i < 8; ++i) new_bytes[i] = memory_controller_.ReadByte(addr + i);
                    current_delta_.memory_changes.push_back({addr, old_bytes, new_bytes});
                }
                break;
            }
            }
        }
    }

    // Handle branch taken: flush IF and ID stages
    if (ex_mem_reg_.branch_taken) {
        program_counter_ = ex_mem_reg_.branch_target;
        if_id_reg_.Reset();
        id_ex_reg_.Reset();
    }

    // Update branch predictor for branches resolved in MEM stage if enabled
    if (ex_mem_reg_.valid && ex_mem_reg_.control.branch && globals::branch_prediction_mode > 0) {
        // Update predictor using the PC where the branch was fetched
        UpdateBranchPredictor(ex_mem_reg_.pc, ex_mem_reg_.branch_taken);

        // If actual outcome differs from prediction, it's a misprediction
        if (ex_mem_reg_.predicted_taken != ex_mem_reg_.branch_taken) {
            branch_mispredictions_++;
            // Flush IF and ID since they may contain wrong-path instructions
            if_id_reg_.Reset();
            id_ex_reg_.Reset();
        }
    }

    mem_wb_reg_.prev_alu_result = mem_wb_reg_.alu_result;
    mem_wb_reg_.prev_memory_result = mem_wb_reg_.memory_result;
    mem_wb_reg_.prev_rd_addr = mem_wb_reg_.rd_addr;
    mem_wb_reg_.prev_instruction = mem_wb_reg_.instruction;
    mem_wb_reg_.prev_valid = mem_wb_reg_.valid;
    mem_wb_reg_.prev_control = mem_wb_reg_.control;

    if(mem_wb_reg_.prev_valid){
        std::cout<<"MEM_WB prev_alu_result: "<<mem_wb_reg_.prev_alu_result<<std::endl;
        std::cout<<"MEM_WB prev_memory_result: "<<mem_wb_reg_.prev_memory_result<<std::endl;
        std::cout<<"MEM_WB prev_rd_addr: "<<static_cast<uint32_t>(mem_wb_reg_.prev_rd_addr)<<std::endl;
        std::cout<<"MEM_WB prev_instruction: "<<mem_wb_reg_.prev_instruction<<std::endl;
    }

    mem_wb_reg_.alu_result = ex_mem_reg_.alu_result;
    mem_wb_reg_.memory_result = current_mem_result_;
    mem_wb_reg_.rd_addr = ex_mem_reg_.rd_addr;
    mem_wb_reg_.control = ex_mem_reg_.control;
    mem_wb_reg_.instruction = instruction;
    mem_wb_reg_.valid = true;
}

// ============================================================================
// Pipeline Stage: WB (Write Back)
// ============================================================================

void RV5SVM::WB() {
    // If MEM/WB register is not valid, nothing to write back
    if (!mem_wb_reg_.valid) {
        return;
    }

    uint32_t instruction = mem_wb_reg_.instruction;
    uint8_t opcode = instruction & 0b1111111;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t funct7 = (instruction >> 25) & 0b1111111;
    uint8_t rd = mem_wb_reg_.rd_addr;
    int32_t imm = ImmGenerator(instruction);

    // Write back to register file
    if (mem_wb_reg_.control.reg_write && rd != 0) {
        // Floating-point instructions
        if (instruction_set::isFInstruction(instruction)) {
            // Check if result goes to GPR (for comparison, conversion ops)
            if (funct7 == 0b1010000 || funct7 == 0b1100000 || funct7 == 0b1110000) {
                uint64_t old_reg = registers_.ReadGpr(rd);
                registers_.WriteGpr(rd, mem_wb_reg_.alu_result);
                if (recording_enabled_) {
                    current_delta_.register_changes.push_back({rd, 0, old_reg, static_cast<uint64_t>(mem_wb_reg_.alu_result)});
                }
            }
            else {
                // Write to FPR
                uint64_t write_val = (mem_wb_reg_.control.mem_to_reg) ?
                    mem_wb_reg_.memory_result : mem_wb_reg_.alu_result;
                uint64_t old_reg = registers_.ReadFpr(rd);
                registers_.WriteFpr(rd, write_val);
                if (recording_enabled_) {
                    current_delta_.register_changes.push_back({rd, 2, old_reg, static_cast<uint64_t>(write_val)});
                }
            }
        }
        else if (instruction_set::isDInstruction(instruction)) {
            // Double-precision FP
            if (funct7 == 0b1010001 || funct7 == 0b1100001 || funct7 == 0b1110001) {
                registers_.WriteGpr(rd, mem_wb_reg_.alu_result);
            }
            else {
                uint64_t write_val = (mem_wb_reg_.control.mem_to_reg) ?
                    mem_wb_reg_.memory_result : mem_wb_reg_.alu_result;
                registers_.WriteFpr(rd, write_val);
            }
        }
        else if (opcode == 0b1110011) {
            // CSR instructions
            {
                uint64_t old_reg = registers_.ReadGpr(rd);
                registers_.WriteGpr(rd, mem_wb_reg_.alu_result); // rd gets old CSR value
                if (recording_enabled_) {
                    current_delta_.register_changes.push_back({rd, 0, old_reg, static_cast<uint64_t>(mem_wb_reg_.alu_result)});
                }
            }

            // Update CSR based on funct3
            switch (funct3) {
            case 0b001: {
                uint64_t old_csr = registers_.ReadCsr(csr_target_address_);
                registers_.WriteCsr(csr_target_address_, csr_write_val_);
                if (recording_enabled_) current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, csr_write_val_});
                break;
            }
            case 0b010: {
                if (csr_write_val_ != 0) {
                    uint64_t old_csr = registers_.ReadCsr(csr_target_address_);
                    uint64_t new_csr = old_csr | csr_write_val_;
                    registers_.WriteCsr(csr_target_address_, new_csr);
                    if (recording_enabled_) current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, new_csr});
                }
                break;
            }
            case 0b011: {
                if (csr_write_val_ != 0) {
                    uint64_t old_csr = registers_.ReadCsr(csr_target_address_);
                    uint64_t new_csr = old_csr & ~csr_write_val_;
                    registers_.WriteCsr(csr_target_address_, new_csr);
                    if (recording_enabled_) current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, new_csr});
                }
                break;
            }
            case 0b101: {
                uint64_t old_csr = registers_.ReadCsr(csr_target_address_);
                registers_.WriteCsr(csr_target_address_, csr_uimm_);
                if (recording_enabled_) current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, csr_uimm_});
                break;
            }
            case 0b110: {
                if (csr_uimm_ != 0) {
                    uint64_t old_csr = registers_.ReadCsr(csr_target_address_);
                    uint64_t new_csr = old_csr | csr_uimm_;
                    registers_.WriteCsr(csr_target_address_, new_csr);
                    if (recording_enabled_) current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, new_csr});
                }
                break;
            }
            case 0b111: {
                if (csr_uimm_ != 0) {
                    uint64_t old_csr = registers_.ReadCsr(csr_target_address_);
                    uint64_t new_csr = old_csr & ~csr_uimm_;
                    registers_.WriteCsr(csr_target_address_, new_csr);
                    if (recording_enabled_) current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, new_csr});
                }
                break;
            }
            }
        }
        else {
            // Integer instructions
            uint64_t write_val = mem_wb_reg_.alu_result;

            // Handle memory-to-register (loads)
            if (mem_wb_reg_.control.mem_to_reg) {
                write_val = mem_wb_reg_.memory_result;
            }

            // Handle LUI specially
            if (opcode == get_instr_encoding(Instruction::klui).opcode) {
                write_val = static_cast<uint64_t>(imm << 12);
            }

            uint64_t old_reg = registers_.ReadGpr(rd);
            registers_.WriteGpr(rd, write_val);
            if (recording_enabled_) current_delta_.register_changes.push_back({rd, 0, old_reg, write_val});
        }
    }

    // Increment instructions retired ONLY in WB stage
    instructions_retired_++;
}

// Helper to get a readable name for an instruction (for logging)
std::string InstructionToString(uint32_t instr) {
    if (instr == 0x00000013) return "nop";
    uint8_t opcode = instr & 0x7F;
    switch (opcode) {
        case 0b0110111: return "lui";
        case 0b0010111: return "auipc";
        case 0b1101111: return "jal";
        case 0b1100111: return "jalr";
        case 0b1100011: return "branch";
        case 0b0000011: return "load";
        case 0b0100011: return "store";
        case 0b0010011: return "addi/slti";
        case 0b0110011: return "R-type";
        case 0b1110011: return "system";
        default: return "unknown";
    }
}

void RV5SVM::PrintPipelineStatus() {
    std::cout << "-------------------- Cycle " << total_cycles_ << " --------------------" << std::endl;
    std::cout << "PC: 0x" << std::hex << program_counter_ << std::dec << std::endl;

    // IF/ID Register
    std::cout << "IF/ID : ";
    if (if_id_reg_.valid) {
        std::cout << "PC=0x" << std::hex << if_id_reg_.pc << ", Instr=0x" << if_id_reg_.instruction
                  << " (" << InstructionToString(if_id_reg_.instruction) << ")" << std::dec << std::endl;
    } else {
        std::cout << "[bubble]" << std::endl;
    }

    // ID/EX Register
    std::cout << "ID/EX : ";
    if (id_ex_reg_.valid) {
        std::cout << "PC=0x" << std::hex << id_ex_reg_.pc << ", Instr=0x" << id_ex_reg_.instruction
                  << " (" << InstructionToString(id_ex_reg_.instruction) << ")" << std::dec << std::endl;
    } else {
        std::cout << "[bubble]" << std::endl;
    }

    // EX/MEM Register
    std::cout << "EX/MEM: ";
    if (ex_mem_reg_.valid) {
        std::cout << "PC=0x" << std::hex << ex_mem_reg_.pc << ", Instr=0x" << ex_mem_reg_.instruction
                  << " (" << InstructionToString(ex_mem_reg_.instruction) << ")" << std::dec
                  << ", ALU_Result=0x" << std::hex << ex_mem_reg_.alu_result << std::dec << std::endl;
    } else {
        std::cout << "[bubble]" << std::endl;
    }

    // MEM/WB Register
    std::cout << "MEM/WB: ";
    if (mem_wb_reg_.valid) {
        std::cout << "PC=0x" << std::hex << (ex_mem_reg_.pc - 4) << ", Instr=0x" << mem_wb_reg_.instruction
                  << " (" << InstructionToString(mem_wb_reg_.instruction) << ")" << std::dec
                  << ", WriteData=0x" << std::hex << (mem_wb_reg_.control.mem_to_reg ? mem_wb_reg_.memory_result : mem_wb_reg_.alu_result)
                  << std::dec << std::endl;
    } else {
        std::cout << "[bubble]" << std::endl;
    }
    std::cout << "----------------------------------------------------" << std::endl;
}

// ============================================================================
// Step Method: Advances Pipeline by One Clock Cycle
// ============================================================================

void RV5SVM::Step() {

    // Check for program termination
    if (program_counter_ >= program_size_ && IsPipelineEmpty()) {
        std::cout << "VM_PROGRAM_END" << std::endl;
        output_status_ = "VM_PROGRAM_END";

        std::cout << "Program Counter: " << std::hex << program_counter_ << std::dec << std::endl;
        std::cout << "Program Size: " <<  std::hex << program_size_ << std::dec << std::endl;
        std::cout << "Cycles: " << total_cycles_ << ", Instructions Retired: " << instructions_retired_ << std::endl;
        
        return;
    }

    // In a pipelined architecture, Step() advances ALL stages by one cycle
    // This is different from RVSS where Step() executes one complete instruction

    // Record starting snapshot for undo/redo
    if (recording_enabled_) {
        current_delta_ = StepDelta();
        current_delta_.old_pc = program_counter_;
        current_delta_.old_if_id = if_id_reg_;
        current_delta_.old_id_ex = id_ex_reg_;
        current_delta_.old_ex_mem = ex_mem_reg_;
        current_delta_.old_mem_wb = mem_wb_reg_;
        current_delta_.old_total_cycles = total_cycles_;
        current_delta_.old_instructions_retired = instructions_retired_;
        if (globals::branch_prediction_mode == 2) {
            // snapshot branch table
            current_delta_.branch_table_snapshot = branch_table_;
        }
    }

    // Execute stages in REVERSE order to simulate parallel execution
    // This prevents data races on pipeline registers
    WB();   // Write Back stage (reads MEM/WB, no writes to pipeline regs)
    MEM();  // Memory stage (reads EX/MEM, writes MEM/WB)
    EX();   // Execute stage (reads ID/EX, writes EX/MEM)
    ID();   // Decode stage (reads IF/ID, writes ID/EX)
    IF();   // Fetch stage (writes IF/ID, reads PC)

    // Increment cycle count
    total_cycles_++;

    PrintPipelineStatus();

    // Record ending snapshot and push to undo stack
    if (recording_enabled_) {
        current_delta_.new_pc = program_counter_;
        current_delta_.new_if_id = if_id_reg_;
        current_delta_.new_id_ex = id_ex_reg_;
        current_delta_.new_ex_mem = ex_mem_reg_;
        current_delta_.new_mem_wb = mem_wb_reg_;
        current_delta_.new_total_cycles = total_cycles_;
        current_delta_.new_instructions_retired = instructions_retired_;
        // register_changes and memory_changes are populated elsewhere (WB/MEM writes)
        undo_stack_.push(current_delta_);
        // clear redo stack since new action invalidates redo history
        while (!redo_stack_.empty()) redo_stack_.pop();
    }


    std::cout << "VM_STEP_COMPLETED" << std::endl;
    output_status_ = "VM_STEP_COMPLETED";

    std::cout << "Program Counter: " << std::hex << program_counter_ << std::dec << std::endl;
    std::cout << "Program Size: " <<  std::hex << program_size_ << std::dec << std::endl;
    std::cout << "Cycles: " << total_cycles_ << ", Instructions Retired: " << instructions_retired_ << std::endl;

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

// ============================================================================
// Run Method: Executes Until Completion
// ============================================================================

void RV5SVM::Run() {
    ClearStop();
    uint64_t cycles_executed = 0;

    // In pipelined execution, we need to:
    // 1. Keep fetching until PC reaches program_size_
    // 2. Continue cycling until pipeline is fully drained
    // 3. Stop if execution limit is exceeded (cycle-based, not instruction-based)

    while (!stop_requested_) {
        
        // TODO: Check if we've exceeded the cycle limit (use 5x instruction limit as safety margin)
        // if (cycles_executed > vm_config::config.getInstructionExecutionLimit() * 10) {
        //     std::cerr << "Execution cycle limit exceeded" << std::endl;
        //     output_status_ = "VM_EXECUTION_LIMIT_EXCEEDED";
        //     break;
        // }

        // Check if program has finished AND pipeline is empty
        if (program_counter_ >= program_size_ && IsPipelineEmpty()) {
            std::cout << "Program completed and pipeline drained." << std::endl;
            break;
        }

        // Execute one pipeline cycle
        // Record starting snapshot for undo/redo
        if (recording_enabled_) {
            current_delta_ = StepDelta();
            current_delta_.old_pc = program_counter_;
            current_delta_.old_if_id = if_id_reg_;
            current_delta_.old_id_ex = id_ex_reg_;
            current_delta_.old_ex_mem = ex_mem_reg_;
            current_delta_.old_mem_wb = mem_wb_reg_;
            current_delta_.old_total_cycles = total_cycles_;
            current_delta_.old_instructions_retired = instructions_retired_;
            if (globals::branch_prediction_mode == 2) {
                current_delta_.branch_table_snapshot = branch_table_;
            }
        }

        WB();
        MEM();
        EX();
        ID();
        IF();

        // Record ending snapshot and push to undo stack
        if (recording_enabled_) {
            current_delta_.new_pc = program_counter_;
            current_delta_.new_if_id = if_id_reg_;
            current_delta_.new_id_ex = id_ex_reg_;
            current_delta_.new_ex_mem = ex_mem_reg_;
            current_delta_.new_mem_wb = mem_wb_reg_;
            current_delta_.new_total_cycles = total_cycles_;
            current_delta_.new_instructions_retired = instructions_retired_;
            undo_stack_.push(current_delta_);
            while (!redo_stack_.empty()) redo_stack_.pop();
        }

        total_cycles_++;
        cycles_executed++;

        // Optional: print progress every N cycles
        if (cycles_executed % 1000 == 0) {
            std::cout << "Cycles: " << total_cycles_ << ", Instructions: " << instructions_retired_ << std::endl;
        }
    }

    // Program completed
    std::cout << "VM_PROGRAM_END" << std::endl;
    output_status_ = "VM_PROGRAM_END";
    std::cout << "========================================" << std::endl;
    std::cout << "Pipeline Execution Statistics:" << std::endl;
    std::cout << "Total cycles: " << total_cycles_ << std::endl;
    std::cout << "Instructions retired: " << instructions_retired_ << std::endl;
    std::cout << "Branch mispredictions: " << branch_mispredictions_ << std::endl;
    std::cout << "Bubbles inserted: " << bubbles_inserted_ << std::endl;

    if (instructions_retired_ > 0) {
        double cpi = static_cast<double>(total_cycles_) / instructions_retired_;
        std::cout << "CPI (Cycles Per Instruction): " << cpi << std::endl;
    }
    std::cout << "========================================" << std::endl;

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

// ============================================================================
// DebugRun Method: Executes with Breakpoint Support
// ============================================================================

void RV5SVM::DebugRun() {
    ClearStop();
    uint64_t cycles_executed = 0;
    unsigned int delay_ms = vm_config::config.getRunStepDelay();

    while (!stop_requested_) {
        if (cycles_executed > vm_config::config.getInstructionExecutionLimit() * 10) {
            std::cerr << "Execution cycle limit exceeded" << std::endl;
            break;
        }

        // Check if any instruction in the pipeline is at a breakpoint
        bool breakpoint_hit = false;
        if (if_id_reg_.valid && std::find(breakpoints_.begin(), breakpoints_.end(), if_id_reg_.pc) != breakpoints_.end()) {
            std::cout << "VM_BREAKPOINT_HIT " << if_id_reg_.pc << std::endl;
            output_status_ = "VM_BREAKPOINT_HIT";
            breakpoint_hit = true;
        }

        if (breakpoint_hit) {
            break;
        }

        // Check for completion
        if (program_counter_ >= program_size_ && IsPipelineEmpty()) {
            break;
        }

        // Execute one cycle
        // Record starting snapshot for undo
        if (recording_enabled_) {
            current_delta_ = StepDelta();
            current_delta_.old_pc = program_counter_;
            current_delta_.old_if_id = if_id_reg_;
            current_delta_.old_id_ex = id_ex_reg_;
            current_delta_.old_ex_mem = ex_mem_reg_;
            current_delta_.old_mem_wb = mem_wb_reg_;
            current_delta_.old_total_cycles = total_cycles_;
            current_delta_.old_instructions_retired = instructions_retired_;
            if (globals::branch_prediction_mode == 2) {
                current_delta_.branch_table_snapshot = branch_table_;
            }
        }

        WB();
        MEM();
        EX();
        ID();
        IF();

        if (recording_enabled_) {
            current_delta_.new_pc = program_counter_;
            current_delta_.new_if_id = if_id_reg_;
            current_delta_.new_id_ex = id_ex_reg_;
            current_delta_.new_ex_mem = ex_mem_reg_;
            current_delta_.new_mem_wb = mem_wb_reg_;
            current_delta_.new_total_cycles = total_cycles_;
            current_delta_.new_instructions_retired = instructions_retired_;
            undo_stack_.push(current_delta_);
            while (!redo_stack_.empty()) redo_stack_.pop();
        }

        total_cycles_++;
        cycles_executed++;

        std::cout << "Program Counter: " << program_counter_ << std::endl;
        std::cout << "VM_STEP_COMPLETED" << std::endl;
        output_status_ = "VM_STEP_COMPLETED";

        DumpRegisters(globals::registers_dump_file_path, registers_);
        DumpState(globals::vm_state_dump_file_path);

        // Add delay for visualization
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    if (program_counter_ >= program_size_ && IsPipelineEmpty()) {
        std::cout << "VM_PROGRAM_END" << std::endl;
        output_status_ = "VM_PROGRAM_END";
    }

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

// ============================================================================
// Undo Method: Reverses One Pipeline Cycle
// ============================================================================

void RV5SVM::Undo() {
    if (undo_stack_.empty()) {
        std::cout << "VM_NO_MORE_UNDO" << std::endl;
        output_status_ = "VM_NO_MORE_UNDO";
        return;
    }

    // Pop last delta
    StepDelta delta = undo_stack_.top();
    undo_stack_.pop();

    // Disable recording while restoring state
    recording_enabled_ = false;

    // Restore pipeline registers and PC
    if_id_reg_ = delta.old_if_id;
    id_ex_reg_ = delta.old_id_ex;
    ex_mem_reg_ = delta.old_ex_mem;
    mem_wb_reg_ = delta.old_mem_wb;
    program_counter_ = delta.old_pc;

    // Restore cycles/instruction counters
    total_cycles_ = delta.old_total_cycles;
    instructions_retired_ = delta.old_instructions_retired;

    // Restore register changes (apply old_value)
    for (const auto &rc : delta.register_changes) {
        switch (rc.reg_type) {
            case 0: registers_.WriteGpr(rc.reg_index, rc.old_value); break;
            case 1: registers_.WriteCsr(rc.reg_index, rc.old_value); break;
            case 2: registers_.WriteFpr(rc.reg_index, rc.old_value); break;
            default: break;
        }
    }

    // Restore memory changes
    for (const auto &mc : delta.memory_changes) {
        for (size_t i = 0; i < mc.old_bytes_vec.size(); ++i) {
            memory_controller_.WriteByte(mc.address + i, mc.old_bytes_vec[i]);
        }
    }

    // Restore branch predictor if snapshot exists
    if (!delta.branch_table_snapshot.empty()) {
        branch_table_ = delta.branch_table_snapshot;
    }

    // Push this delta to redo stack so it can be reapplied
    redo_stack_.push(delta);

    recording_enabled_ = true;

    std::cout << "VM_UNDO_COMPLETED" << std::endl;
    output_status_ = "VM_UNDO_COMPLETED";

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

// ============================================================================
// Redo Method: Re-applies One Pipeline Cycle
// ============================================================================

void RV5SVM::Redo() {
    if (redo_stack_.empty()) {
        std::cout << "VM_NO_MORE_REDO" << std::endl;
        output_status_ = "VM_NO_MORE_REDO";
        return;
    }

    StepDelta delta = redo_stack_.top();
    redo_stack_.pop();

    // Disable recording while applying
    recording_enabled_ = false;

    // Restore 'new' state (i.e., reapply the cycle)
    if_id_reg_ = delta.new_if_id;
    id_ex_reg_ = delta.new_id_ex;
    ex_mem_reg_ = delta.new_ex_mem;
    mem_wb_reg_ = delta.new_mem_wb;
    program_counter_ = delta.new_pc;

    total_cycles_ = delta.new_total_cycles;
    instructions_retired_ = delta.new_instructions_retired;

    // Reapply register changes (set to new_value)
    for (const auto &rc : delta.register_changes) {
        switch (rc.reg_type) {
            case 0: registers_.WriteGpr(rc.reg_index, rc.new_value); break;
            case 1: registers_.WriteCsr(rc.reg_index, rc.new_value); break;
            case 2: registers_.WriteFpr(rc.reg_index, rc.new_value); break;
            default: break;
        }
    }

    // Reapply memory changes
    for (const auto &mc : delta.memory_changes) {
        for (size_t i = 0; i < mc.new_bytes_vec.size(); ++i) {
            memory_controller_.WriteByte(mc.address + i, mc.new_bytes_vec[i]);
        }
    }

    // Restore branch predictor snapshot if present
    if (!delta.branch_table_snapshot.empty()) {
        branch_table_ = delta.branch_table_snapshot;
    }

    // Push back onto undo stack to allow another undo
    undo_stack_.push(delta);

    recording_enabled_ = true;

    std::cout << "VM_REDO_COMPLETED" << std::endl;
    output_status_ = "VM_REDO_COMPLETED";

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

// ============================================================================
// Reset Method: Resets VM to Initial State
// ============================================================================

void RV5SVM::Reset() {

    // Reset pipeline-specific state
    FlushPipeline();

    // Reset counters
    total_cycles_ = 0;
    bubbles_inserted_ = 0;
    pc_write_ = true;
    if_id_write_ = true;

    // Reset intermediate results
    current_alu_result_ = 0;
    current_mem_result_ = 0;
    current_return_address_ = 0;

    // Reset CSR intermediate values
    csr_target_address_ = 0;
    csr_old_value_ = 0;
    csr_write_val_ = 0;
    csr_uimm_ = 0;

    // Reset control unit
    control_unit_.Reset();

    std::cout << "VM_RESET_COMPLETED" << std::endl;
    output_status_ = "VM_RESET_COMPLETED";

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

// ============================================================================
// Helper Methods
// ============================================================================

void RV5SVM::FlushPipeline() {
    if_id_reg_.Reset();
    id_ex_reg_.Reset();
    ex_mem_reg_.Reset();
    mem_wb_reg_.Reset();
}

void RV5SVM::InsertBubble() {
    id_ex_reg_.Reset(); // Insert NOP into ID/EX stage
    bubbles_inserted_++;
}

bool RV5SVM::IsPipelineEmpty() {
    return !if_id_reg_.valid && !id_ex_reg_.valid &&
        !ex_mem_reg_.valid && !mem_wb_reg_.valid;
}

// Detect load-use hazards
uint8_t RV5SVM::DetectHazard(uint64_t rs1, uint64_t rs2) {
    // No register sources -> no hazard
    if (rs1 == 0 && rs2 == 0) return 0;

    // Helper lambda to check match
    auto matches = [&](uint8_t rd) {
        return rd != 0 && (rd == rs1 || rd == rs2);
    };

    // If forwarding is disabled: any pending reg write to our source registers is a hazard
    if (!globals::pipeline_forwarding_enabled) {
        if (id_ex_reg_.valid && id_ex_reg_.control.reg_write && matches(id_ex_reg_.rd_addr)) {
            return 2; // producer in EX stage: need 2 NOPs to wait until WB
        }
        if (ex_mem_reg_.valid && ex_mem_reg_.control.reg_write && matches(ex_mem_reg_.rd_addr)) {
            return 1; // producer in MEM stage: need 1 NOP to wait until WB
        }
        if (mem_wb_reg_.valid && mem_wb_reg_.control.reg_write && matches(mem_wb_reg_.rd_addr)) {
            return 0; // producer in WB stage: value is being written this cycle
        }
        return 0;
    }

    // Forwarding enabled: only stall for true load-use where load data is not yet available.
    if (id_ex_reg_.valid && id_ex_reg_.control.mem_read && matches(id_ex_reg_.rd_addr)) {
        return 1; // load in EX stage -> classic load-use hazard (1 NOP)
    }

    if (ex_mem_reg_.valid && ex_mem_reg_.control.mem_read && matches(ex_mem_reg_.rd_addr)) {
        return 0; // load in MEM stage: data becomes available for forwarding this cycle
    }

    return 0;
}

uint64_t RV5SVM::ResolveForwarding(uint8_t src_reg, uint64_t reg_value) {

    if (ex_mem_reg_.valid && ex_mem_reg_.rd_addr != 0 && ex_mem_reg_.rd_addr == src_reg && ex_mem_reg_.control.reg_write) {
        if (!ex_mem_reg_.control.mem_read) {
            return static_cast<uint64_t>(ex_mem_reg_.alu_result);
        }
    }

    if (mem_wb_reg_.prev_valid && mem_wb_reg_.prev_rd_addr != 0 && mem_wb_reg_.prev_rd_addr == src_reg && mem_wb_reg_.prev_control.reg_write) {
        if (mem_wb_reg_.prev_control.mem_to_reg) {
            return static_cast<uint64_t>(mem_wb_reg_.prev_memory_result);
        } else {
            return static_cast<uint64_t>(mem_wb_reg_.prev_alu_result);
        }
    }

    return reg_value;
}


bool RV5SVM::GetBranchPrediction(uint64_t pc) {
    if (globals::branch_prediction_mode == 0) return false;

    if (globals::branch_prediction_mode == 1) {
        // Static prediction: use global static policy
        return (globals::static_branch_policy != 0);
    }

    // Dynamic (2-bit bimodal)
    size_t idx = (pc >> 2) & bp_mask_;
    int n = globals::branch_prediction_bits;
    uint8_t counter = branch_table_[idx] & ((1u << n) - 1);
    // threshold: top half of counter range predicts taken
    uint8_t threshold = (1u << (n - 1));
    return counter >= threshold;
}

void RV5SVM::UpdateBranchPredictor(uint64_t pc, bool taken) {
    if (globals::branch_prediction_mode != 2) return; // only update dynamic predictor

    size_t idx = (pc >> 2) & bp_mask_;
    int n = globals::branch_prediction_bits;
    uint8_t maxv = static_cast<uint8_t>((1u << n) - 1u);
    uint8_t counter = branch_table_[idx] & maxv;
    if (taken) {
        if (counter < maxv) counter++;
    } else {
        if (counter > 0) counter--;
    }
    branch_table_[idx] = counter;
}