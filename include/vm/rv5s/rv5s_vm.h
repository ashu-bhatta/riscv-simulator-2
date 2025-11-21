/**
 * @file rv5s_vm.h
 * @brief RV5S (5-Stage Pipeline) VM definition
 * @author Om Dave & Ashutosh Bhatta
 */
#ifndef RV5S_VM_H
#define RV5S_VM_H

#include "vm/vm_base.h"
#include "vm/rvss/rvss_control_unit.h"

#include <stack>
#include <vector>
#include <iostream>
#include <cstdint>

// Forward declarations for pipeline registers
struct IF_ID_Register;
struct ID_EX_Register;
struct EX_MEM_Register;
struct MEM_WB_Register;

/**
 * @brief Pipeline register between IF and ID stages
 * Carries instruction and PC from Fetch to Decode
 */
struct IF_ID_Register {
  uint64_t pc = 0;                // Program counter of this instruction
  uint32_t instruction = 0x00000013; // Instruction word (default: NOP/addi x0, x0, 0)
  bool valid = false;             // Is this a valid instruction (not a bubble)?
  
  void Reset() {
    pc = 0;
    instruction = 0x00000013; // NOP
    valid = false;
  }
};

/**
 * @brief Pipeline register between ID and EX stages
 * Carries decoded values and control signals
 */
struct ID_EX_Register {
  // Data values
  uint64_t pc = 0;
  uint64_t rs1_val = 0;           // Value read from rs1
  uint64_t rs2_val = 0;           // Value read from rs2
  int64_t imm = 0;                // Sign-extended immediate
  
  // Register addresses
  uint8_t rs1_addr = 0;
  uint8_t rs2_addr = 0;
  uint8_t rd_addr = 0;
  
  // Control signals
  RVSSControlUnit::ControlSignals control;
  
  // Original instruction (for debugging and special handling)
  uint32_t instruction = 0x00000013;
  bool valid = false;
  
  // Branch-specific
  bool branch_flag = false;
  int64_t branch_target = 0;
  // Prediction info
  bool predicted_taken = false;
  
  void Reset() {
    pc = 0;
    rs1_val = 0;
    rs2_val = 0;
    imm = 0;
    rs1_addr = 0;
    rs2_addr = 0;
    rd_addr = 0;
    control = RVSSControlUnit::ControlSignals();
    instruction = 0x00000013;
    valid = false;
    branch_flag = false;
    branch_target = 0;
  }
};

/**
 * @brief Pipeline register between EX and MEM stages
 * Carries ALU result and control signals
 */
struct EX_MEM_Register {
  // Data values
  uint64_t pc = 0;
  int64_t alu_result = 0;         // Result from ALU/Execute
  uint64_t rs2_val = 0;           // Store data (for memory writes)
  uint8_t rd_addr = 0;
  
  // Control signals
  RVSSControlUnit::ControlSignals control;
  
  uint32_t instruction = 0x00000013;
  bool valid = false;

  // Branch resolution
  bool branch_taken = false;
  uint64_t branch_target = 0;
  bool predicted_taken = false;
  
  void Reset() {
    pc = 0;
    alu_result = 0;
    rs2_val = 0;
    rd_addr = 0;
    control = RVSSControlUnit::ControlSignals();
    instruction = 0x00000013;
    valid = false;
    branch_taken = false;
    branch_target = 0;
  }
};

/**
 * @brief Pipeline register between MEM and WB stages
 * Carries memory result and final writeback data
 */
struct MEM_WB_Register {
  int64_t alu_result = 0;         // ALU result (for R-type, I-type)
  int64_t memory_result = 0;      // Data read from memory (for loads)
  uint8_t rd_addr = 0;
  
  // Control signals
  RVSSControlUnit::ControlSignals control;
  
  uint32_t instruction = 0x00000013;
  bool valid = false;

  int64_t prev_alu_result = 0;
  int64_t prev_memory_result = 0;
  uint8_t prev_rd_addr = 0;

  RVSSControlUnit::ControlSignals prev_control;

  uint32_t prev_instruction = 0x00000013;
  bool prev_valid = false;
  
  void Reset() {
    alu_result = 0;
    memory_result = 0;
    rd_addr = 0;
    control = RVSSControlUnit::ControlSignals();
    instruction = 0x00000013;
    valid = false;
  }
};

/**
 * @brief 5-Stage Pipelined RISC-V VM
 * Implements IF -> ID -> EX -> MEM -> WB pipeline
 */
class RV5SVM : public VmBase {
public:
  // Structures to record architectural changes for undo/redo
  struct RegisterChange {
    unsigned int reg_index;
    unsigned int reg_type; // 0 = GPR, 1 = CSR, 2 = FPR
    uint64_t old_value;
    uint64_t new_value;
  };

  struct MemoryChange {
    uint64_t address;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;
  };

  struct StepDelta {
    uint64_t old_pc = 0;
    uint64_t new_pc = 0;

    // Pipeline register snapshots (before and after)
    IF_ID_Register old_if_id;
    ID_EX_Register old_id_ex;
    EX_MEM_Register old_ex_mem;
    MEM_WB_Register old_mem_wb;

    IF_ID_Register new_if_id;
    ID_EX_Register new_id_ex;
    EX_MEM_Register new_ex_mem;
    MEM_WB_Register new_mem_wb;

    std::vector<RegisterChange> register_changes;
    std::vector<MemoryChange> memory_changes;

    // Optional branch predictor snapshot (copied when dynamic predictor is enabled)
    std::vector<uint8_t> branch_table_snapshot;

    uint64_t old_total_cycles = 0;
    uint64_t new_total_cycles = 0;
    uint64_t old_instructions_retired = 0;
    uint64_t new_instructions_retired = 0;
  };

  // Undo/Redo stacks and current delta
  std::stack<StepDelta> undo_stack_;
  std::stack<StepDelta> redo_stack_;
  StepDelta current_delta_;

  // When false, MEM/WB/WB shouldn't append to current_delta_ (used during undo/redo restore)
  bool recording_enabled_ = true;

  // Pipeline registers
  IF_ID_Register if_id_reg_;
  ID_EX_Register id_ex_reg_;
  EX_MEM_Register ex_mem_reg_;
  MEM_WB_Register mem_wb_reg_;
  
  // Control unit
  RVSSControlUnit control_unit_;
  
  // Pipeline control flags
  bool pc_write_ = true;          // Can PC be updated?
  bool if_id_write_ = true;       // Can IF/ID register be updated?
  uint8_t hazard_stall_cycles_ = 0;
  
  // Statistics
  uint64_t total_cycles_ = 0;
  uint64_t bubbles_inserted_ = 0;
  
  std::atomic<bool> stop_requested_ = false;

  // Constructor & Destructor
  RV5SVM();
  ~RV5SVM();

  // ============ Pipeline Stage Methods ============
  
  /**
   * @brief Instruction Fetch (IF) Stage
   * Fetches instruction from memory and updates IF/ID register
   */
  void IF();
  
  /**
   * @brief Instruction Decode (ID) Stage
   * Decodes instruction, reads registers, generates control signals
   */
  void ID();
  
  /**
   * @brief Execute (EX) Stage
   * Performs ALU operations, calculates addresses, resolves branches
   */
  void EX();
  
  /**
   * @brief Memory Access (MEM) Stage
   * Reads from or writes to memory
   */
  void MEM();
  
  /**
   * @brief Write Back (WB) Stage
   * Writes results back to register file
   */
  void WB();

  // ============ Helper Methods (similar to RVSS) ============
  void ExecuteAlu(ID_EX_Register& id_ex);
  void ExecuteFloat(ID_EX_Register& id_ex);
  void ExecuteDouble(ID_EX_Register& id_ex);
  void ExecuteCsr(ID_EX_Register& id_ex);
  void HandleSyscall();
  
  // ============ VmBase Overrides ============
  void Run() override;
  void DebugRun() override;
  void Step() override;
  void Undo() override;
  void Redo() override;
  void Reset() override;

  void RequestStop() { stop_requested_ = true; }
  bool IsStopRequested() const { return stop_requested_; }
  void ClearStop() { stop_requested_ = false; }
  
  void PrintType() {
    std::cout << "rv5svm (5-stage pipeline)" << std::endl;
  }
  
  // ============ Pipeline-Specific Methods ============
  void FlushPipeline();           // Clear all pipeline registers (for reset)
  void InsertBubble();            // Insert a NOP bubble in ID/EX
  bool IsPipelineEmpty();         // Check if all stages are empty

  uint8_t DetectHazard(uint64_t rs1, uint64_t rs2);
  uint64_t ResolveForwarding(uint8_t src_reg, uint64_t reg_value); 

  // Branch predictor helpers
  bool GetBranchPrediction(uint64_t pc);
  void UpdateBranchPredictor(uint64_t pc, bool taken);
  
private:
  void PrintPipelineStatus();

  // Temporary storage during execution (similar to RVSS)
  int64_t current_alu_result_ = 0;
  int64_t current_mem_result_ = 0;
  uint64_t current_return_address_ = 0;
  
  // CSR intermediate values
  uint16_t csr_target_address_ = 0;
  uint64_t csr_old_value_ = 0;
  uint64_t csr_write_val_ = 0;
  uint8_t csr_uimm_ = 0;

  // Branch predictor (2-bit bimodal)
  std::vector<uint8_t> branch_table_;
  size_t bp_mask_ = 0;
  static constexpr size_t kBpTableSize = 1024; // must be power of two
};

#endif // RV5S_VM_H