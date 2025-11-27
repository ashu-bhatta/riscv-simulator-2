import subprocess
import sys
import os
import time
import re

def run_vm_config(vm_path, assembly_file, config_commands):
    """
    Runs the VM with the specified configuration commands.
    Returns a dictionary of statistics.
    """
    try:
        process = subprocess.Popen(
            [vm_path, "--start-vm"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=os.path.dirname(vm_path)
        )
    except Exception as e:
        print(f"Failed to start VM: {e}")
        return None

    # Base commands
    commands = [
        "mconfig Execution processor_type multi_stage", # Ensure we use the pipeline
        f"load {assembly_file}"
    ] + config_commands + ["run"]

    stats = {}
    
    try:
        # Send commands
        for cmd in commands:
            process.stdin.write(cmd + "\n")
        process.stdin.flush()
        
        # Read output
        start_time = time.time()
        timeout_seconds = 30
        program_ended = False
        
        while True:
            if time.time() - start_time > timeout_seconds:
                print("Timeout waiting for VM")
                process.terminate()
                return None
                
            line = process.stdout.readline()
            if not line:
                break
            
            # Debug: print line to stderr
            # sys.stderr.write(f"VM: {line}")
            
            # Parse statistics
            if "Total cycles:" in line:
                stats['cycles'] = int(line.split(":")[1].strip())
            elif "Instructions retired:" in line:
                stats['instructions'] = int(line.split(":")[1].strip())
            elif "Branch mispredictions:" in line:
                stats['mispredictions'] = int(line.split(":")[1].strip())
            elif "Bubbles inserted:" in line:
                stats['bubbles'] = int(line.split(":")[1].strip())
            elif "CPI (Cycles Per Instruction):" in line:
                stats['cpi'] = float(line.split(":")[1].strip())
                
            if "VM_PROGRAM_END" in line:
                program_ended = True
                
            # Break if we see the end of the stats block
            if "Pipeline Execution Statistics:" in line:
                in_stats = True
            if "========================================" in line and program_ended:
                # We saw the end of the stats block (it prints separator before and after, so we need to be careful)
                # The code prints separator, then "Pipeline Execution Statistics:", then stats, then separator.
                # So if we are in_stats and see separator, we are done.
                if stats.get('cycles'): # Ensure we actually parsed something
                    break
                
        process.stdin.write("exit\n")
        process.stdin.flush()
        process.wait()
        
        return stats

    except Exception as e:
        print(f"Exception: {e}")
        process.terminate()
        return None

def main():
    # Paths
    base_dir = os.getcwd()
    # Adjust if running from test/ subdir
    if base_dir.endswith("test"):
        base_dir = os.path.dirname(base_dir)
        
    build_dir = os.path.join(base_dir, "build")
    vm_path = os.path.join(build_dir, "vm")
    
    # Use pipeline_test6.s from examples
    assembly_file = os.path.join(base_dir, "examples", "pipeline_test6.s")
    
    if not os.path.exists(vm_path):
        print(f"Error: VM executable not found at {vm_path}")
        # Try looking in root build
        vm_path = os.path.join(base_dir, "build", "vm")
        if not os.path.exists(vm_path):
             print("Please build the project first.")
             sys.exit(1)

    if not os.path.exists(assembly_file):
        print(f"Error: Assembly file not found at {assembly_file}")
        sys.exit(1)

    print(f"Comparing Branch Prediction Strategies using {os.path.basename(assembly_file)}")
    print("-" * 80)
    print(f"{'Strategy':<25} | {'Cycles':<10} | {'Mispred':<10} | {'CPI':<10} | {'Bubbles':<10}")
    print("-" * 80)

    configs = [
        ("None (Always Not Taken)", ["mconfig Execution branch_prediction none"]),
        ("Static (Not Taken)", ["mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy not_taken"]),
        ("Static (Taken)", ["mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy taken"]),
        ("Dynamic (2-bit)", ["mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 2"])
    ]

    for name, cmds in configs:
        stats = run_vm_config(vm_path, assembly_file, cmds)
        if stats:
            print(f"{name:<25} | {stats.get('cycles', 'N/A'):<10} | {stats.get('mispredictions', 'N/A'):<10} | {stats.get('cpi', 'N/A'):<10.2f} | {stats.get('bubbles', 'N/A'):<10}")
        else:
            print(f"{name:<25} | FAILED")

if __name__ == "__main__":
    main()
