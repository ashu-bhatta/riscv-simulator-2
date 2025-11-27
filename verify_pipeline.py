# Compares register dumps from 2 processor types (can change below in code)
# python3 verify_pipeline.py ../examples/pipeline_test1.s

import subprocess
import json
import sys
import os
import time

def run_vm(vm_path, assembly_file, processor_type, output_json_path):
    """
    Runs the VM with the specified processor type and assembly file.
    Waits for VM_PROGRAM_END before sending exit command.
    """
    print(f"Running VM with processor_type={processor_type}...")
    
    # Start the process
    try:
        process = subprocess.Popen(
            [vm_path, "--start-vm"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1, # Line buffered
            cwd=os.path.dirname(vm_path)
        )
    except Exception as e:
        print(f"Failed to start VM: {e}")
        return False

    # Commands to start execution
    commands = [
        f"mconfig Execution processor_type {processor_type}",
        f"load {assembly_file}",
        "run"
    ]
    
    try:
        # Send initial commands
        for cmd in commands:
            process.stdin.write(cmd + "\n")
        process.stdin.flush()
        
        # Read output until VM_PROGRAM_END or timeout
        start_time = time.time()
        timeout_seconds = 30
        program_ended = False
        
        while True:
            # Check for timeout
            if time.time() - start_time > timeout_seconds:
                print("Error: Timeout waiting for VM_PROGRAM_END")
                process.terminate()
                return False
                
            # Non-blocking read line (using select or just readline if we assume it won't block forever if we have output)
            # Since we don't have easy non-blocking I/O in portable python without extra libs, 
            # and the VM should be outputting, we'll use readline. 
            # CAUTION: If VM hangs without output, this blocks. 
            # For this script, we assume VM is chatty or finishes.
            line = process.stdout.readline()
            if not line:
                break
                
            print(f"[{processor_type}] {line.strip()}")
            
            if "VM_PROGRAM_END" in line:
                program_ended = True
                break
                
        if not program_ended:
            print("Error: VM process ended without VM_PROGRAM_END signal")
            return False
            
        # Send exit command
        process.stdin.write("exit\n")
        process.stdin.flush()
        
        # Wait for clean exit
        process.wait(timeout=5)
        
        if process.returncode != 0:
            print(f"Error running VM ({processor_type}): Return code {process.returncode}")
            # print(process.stderr.read()) # Stderr might be empty if everything went to stdout
            return False

        # Check if registers_dump.json was generated/updated
        registers_dump_path = os.path.join(os.path.dirname(vm_path), "vm_state", "registers_dump.json")
        if not os.path.exists(registers_dump_path):
            print(f"Error: registers_dump.json not found at {registers_dump_path}")
            return False
            
        # Copy the dump to the specific output path
        with open(registers_dump_path, 'r') as src, open(output_json_path, 'w') as dst:
            dst.write(src.read())
            
        return True

    except Exception as e:
        print(f"Exception running VM: {e}")
        process.terminate()
        return False

def compare_registers(file1, file2):
    """
    Compares two register dump JSON files.
    """
    try:
        with open(file1, 'r') as f1, open(file2, 'r') as f2:
            data1 = json.load(f1)
            data2 = json.load(f2)
            
        # Compare GP registers
        gp1 = data1.get("gp_registers", {})
        gp2 = data2.get("gp_registers", {})
        
        mismatches = []
        
        all_regs = set(gp1.keys()) | set(gp2.keys())
        for reg in sorted(all_regs, key=lambda x: int(x[1:])): # Sort by register number (x0, x1...)
            val1 = gp1.get(reg, "MISSING")
            val2 = gp2.get(reg, "MISSING")
            if val1 != val2:
                mismatches.append(f"{reg}: Single-Stage={val1}, Multi-Stage={val2}")
                
        # Compare FP registers
        fp1 = data1.get("fp_registers", {})
        fp2 = data2.get("fp_registers", {})
        
        all_fregs = set(fp1.keys()) | set(fp2.keys())
        for reg in sorted(all_fregs, key=lambda x: int(x[1:])):
            val1 = fp1.get(reg, "MISSING")
            val2 = fp2.get(reg, "MISSING")
            if val1 != val2:
                mismatches.append(f"{reg}: Single-Stage={val1}, Multi-Stage={val2}")

        if mismatches:
            print("FAIL: Register mismatches found:")
            for m in mismatches:
                print(f"  {m}")
            return False
        else:
            print("PASS: Registers match.")
            return True

    except Exception as e:
        print(f"Exception comparing files: {e}")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 verify_pipeline.py <assembly_file>")
        sys.exit(1)
        
    assembly_file_arg = sys.argv[1]
    # Convert to absolute path to avoid issues with CWD
    assembly_file = os.path.abspath(assembly_file_arg)
    
    # Paths
    base_dir = os.getcwd()
    build_dir = os.path.join(base_dir, "build")
    vm_path = os.path.join(build_dir, "vm")
    
    if not os.path.exists(vm_path):
        print(f"Error: VM executable not found at {vm_path}")
        sys.exit(1)
        
    single_stage_out = "single_stage_registers.json"
    multi_stage_out = "multi_stage_registers.json"
    
    print(f"Testing file: {assembly_file}")
    
    # 1. Run Single Stage
    if not run_vm(vm_path, assembly_file, "single_stage", single_stage_out):
        print("Failed to run single-stage simulation.")
        sys.exit(1)
        
    # 2. Run Multi Stage
    if not run_vm(vm_path, assembly_file, "multi_stage_with_both", multi_stage_out):
        print("Failed to run multi-stage simulation.")
        sys.exit(1)
        
    # 3. Compare
    print("-" * 30)
    print("Comparing results...")
    success = compare_registers(single_stage_out, multi_stage_out)
    
    # Cleanup (optional)
    # os.remove(single_stage_out)
    # os.remove(multi_stage_out)
    
    if not success:
        sys.exit(1)

if __name__ == "__main__":
    main()
