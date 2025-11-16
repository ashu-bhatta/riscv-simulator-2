# Project Plan: 5-Stage Pipelined Processor for the RISC-V Simulator

## Members
- **Om Dave** (CO22BTECH11006)  
- **Ashutosh Bhatta** (MA22BTECH11007)

---

## 1. Project Scope

The goal is to transform the current single-cycle execution model into a classic **5-stage RISC-V pipeline** (IF, ID, EX, MEM, WB).

### In Scope
- **Pipelined Architecture:**  
  Implement a new VM class `RV5SVM` (RISC-V 5-stage Pipelined VM), inheriting from `VmBase`, to represent the 5-stage pipeline.

- **Pipeline Registers:**  
  Introduce data structures to act as pipeline registers (`IF/ID`, `ID/EX`, `EX/MEM`, `MEM/WB`) to hold state between stages.

- **Data Hazard Handling:**
  - Implement a **Hazard Detection Unit** to identify RAW (Read-After-Write) dependencies.
  - Implement **Data Forwarding** (Bypassing) from the `EX/MEM` and `MEM/WB` stages back to the EX stage.
  - Implement **Pipeline Stalling** (bubble insertion) for load-use hazards where forwarding is insufficient.

- **Control Hazard Handling:**
  - Implement a basic **branch prediction scheme** (predict-not-taken).
  - On misprediction, **flush** the pipeline to discard incorrectly fetched instructions.

- **Configuration:**  
  Integrate the new pipelined model into the configuration system (`config.h`) under the `multi_stage` processor type.

- **Statistics:**  
  Update VM statistics like `cpi_`, `stall_cycles_`, and `branch_mispredictions_` in `VmBase` to reflect pipelined execution.

---

## 2. Implementation Steps

Implementation will be modular—building the pipeline first, then adding hazard management.

### Step 1: Create the Pipelined VM Structure

1. **Define New VM Class:**  
   - Create new files:  
     `include/vm/rv5s/rv5s_vm.h` and `src/vm/rv5s/rv5s_vm.cpp`  
   - Define `RV5SVM` inheriting from `VmBase`.

2. **Define Pipeline Registers:**  
   Define structs for each pipeline register in `rv5s_vm.h`:
   - `IF_ID_Register`: `instruction`, `pc`
   - `ID_EX_Register`: `pc`, `rs1_val`, `rs2_val`, `imm`, `rd_addr`, `control_signals`
   - `EX_MEM_Register`: `alu_result`, `rs2_val`, `rd_addr`, `control_signals`
   - `MEM_WB_Register`: `memory_result`, `alu_result`, `rd_addr`, `control_signals`

3. **Adapt Existing Logic:**  
   Refactor methods from `RVSSVM` (`Fetch`, `Decode`, `Execute`, `WriteMemory`, `WriteBack`) to operate as independent pipeline stages.

4. **Update `Step()` Method:**  
   Execute pipeline stages in reverse order (`WB → MEM → EX → ID → IF`) per cycle to simulate parallelism and avoid data races.

---

### Step 2: Implement Data Hazard Detection and Forwarding

1. **Hazard Detection Unit:**  
   - Implement in the ID stage.  
   - Check if `rs1` or `rs2` of the ID-stage instruction match `rd` of EX/MEM-stage instructions.

2. **Forwarding Unit:**  
   - Used in EX stage to decide ALU input sources.  
   - Sources: Register file, `EX/MEM.alu_result`, or `MEM/WB.memory_result`.

3. **Load-Use Hazard Stalling:**  
   - If the ID-stage instruction depends on a load in EX stage:
     - Stall the pipeline.
     - Prevent updates to `PC` and `IF/ID` register.
     - Inject a **bubble** (NOP instruction) into `ID/EX`.

---

### Step 3: Implement Control Hazard Handling

1. **Branch Logic:**  
   Move branch target calculation and comparison to ID stage.

2. **Predict-Not-Taken:**  
   IF stage always fetches from `PC + 4`.

3. **Flush on Misprediction:**  
   - When a branch is taken:
     - Flush instructions in `IF/ID` and `ID/EX` registers.
     - Update `pc_` to the correct branch target.
     - Increment `branch_mispredictions_`.

---

## 3. Verification Plan

Testing will use custom assembly programs and extend existing tests.

### 1. Unit Tests for Hazard Scenarios

#### Data Hazard Test (`data_hazard_test.s`)
```asm
.text
addi x5, x0, 5      # x5 = 5
addi x6, x0, 10     # x6 = 10
add x7, x5, x6      # RAW on x5, x6 → 15
sub x8, x7, x5      # RAW on x7 → 10
