/**
 * @file rvss_control_unit.h
 * @brief RVSS Control Unit
 * @author Vishank Singh, https://github.com/VishankSingh
 */
#ifndef RVSS_CONTROL_UNIT_H
#define RVSS_CONTROL_UNIT_H

#include "../control_unit_base.h"


class RVSSControlUnit : public ControlUnit {
 public:
  struct ControlSignals {
    bool reg_write = false;
    bool branch = false;
    bool alu_src = false;
    bool mem_read = false;
    bool mem_write = false;
    bool mem_to_reg = false;
    bool pc_src = false;
    uint8_t alu_op = 0;
    bool jump = false; // Added for JAL/JALR differentiation
    
    // Default constructor (all signals false/zero)
    ControlSignals() = default;
    
    // Constructor from control unit state
    ControlSignals(bool reg_w, bool br, bool alu_s, bool mem_r, 
                   bool mem_w, bool mem_to_r, bool pc_s, uint8_t alu_o)
      : reg_write(reg_w), branch(br), alu_src(alu_s), mem_read(mem_r),
        mem_write(mem_w), mem_to_reg(mem_to_r), pc_src(pc_s), alu_op(alu_o) {}
  };
  // ============================================================================
  
  RVSSControlUnit() = default;
  ~RVSSControlUnit() override = default;

  void SetControlSignals(uint32_t instruction) override;
  
  alu::AluOp GetAluSignal(uint32_t instruction, bool ALUOp) override;
  
  // ============ NEW: Method to get current control signals as struct ============
  ControlSignals GetControlSignals() const {
    return ControlSignals(
      GetRegWrite(),
      GetBranch(),
      GetAluSrc(),
      GetMemRead(),
      GetMemWrite(),
      GetMemToReg(),
      false, // pc_src (not currently used)
      GetAluOp()
    );
  }
  // ============================================================================
};

#endif // RVSS_CONTROL_UNIT_H