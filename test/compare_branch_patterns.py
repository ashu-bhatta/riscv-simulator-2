#!/usr/bin/env python3
"""
Branch Prediction Pattern Comparison Tool
Tests various branch prediction strategies against different branch patterns.
"""

import subprocess
import sys
import os
import time

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
        "mconfig Execution processor_type multi_stage_with_both",
        f"load {assembly_file}"
    ] + config_commands + ["run"]

    stats = {}
    
    try:
        for cmd in commands:
            process.stdin.write(cmd + "\n")
        process.stdin.flush()
        
        start_time = time.time()
        timeout_seconds = 30
        program_ended = False
        
        while True:
            if time.time() - start_time > timeout_seconds:
                print("Timeout")
                process.terminate()
                return None
                
            line = process.stdout.readline()
            if not line:
                break
            
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
                
            if "========================================" in line and program_ended:
                if stats.get('cycles'):
                    break
                
        process.stdin.write("exit\n")
        process.stdin.flush()
        process.wait()
        
        return stats

    except Exception as e:
        print(f"Exception: {e}")
        process.terminate()
        return None

def test_pattern(vm_path, pattern_file, pattern_name):
    """Test all prediction strategies on a specific branch pattern."""
    print(f"\n{'='*80}")
    print(f"Testing: {pattern_name}")
    print(f"{'='*80}")
    print(f"{'Strategy':<25} | {'Cycles':<10} | {'Mispred':<10} | {'CPI':<10} | {'Accuracy':<10}")
    print("-" * 80)

    configs = [
        ("None (Always Not Taken)", ["mconfig Execution branch_prediction none"]),
        ("Static (Not Taken)", ["mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy not_taken"]),
        ("Static (Taken)", ["mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy taken"]),
        ("Dynamic (1-bit)", ["mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 1"]),
        ("Dynamic (2-bit)", ["mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 2"]),
        ("Dynamic (4-bit)", ["mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 4"]),
    ]

    results = []
    for name, cmds in configs:
        stats = run_vm_config(vm_path, pattern_file, cmds)
        if stats:
            # Calculate accuracy (assuming we know total branches from instructions)
            accuracy = 100.0 * (1.0 - stats.get('mispredictions', 0) / max(stats.get('instructions', 1), 1))
            results.append((name, stats))
            print(f"{name:<25} | {stats.get('cycles', 'N/A'):<10} | {stats.get('mispredictions', 'N/A'):<10} | {stats.get('cpi', 'N/A'):<10.2f} | {accuracy:<9.1f}%")
        else:
            print(f"{name:<25} | FAILED")
    
    return results

def main():
    # Paths
    base_dir = os.getcwd()
    if base_dir.endswith("test"):
        base_dir = os.path.dirname(base_dir)
        
    build_dir = os.path.join(base_dir, "build")
    vm_path = os.path.join(build_dir, "vm")
    examples_dir = os.path.join(base_dir, "examples")
    
    if not os.path.exists(vm_path):
        print(f"Error: VM executable not found at {vm_path}")
        sys.exit(1)

    # Test patterns
    patterns = [
        # ("branch_pattern_always_taken.s", "Always Taken (except last)"),
        # ("branch_pattern_never_taken.s", "Always Not Taken"),
        # ("branch_pattern_tntn.s", "Alternating (T-N-T-N)"),
        # ("branch_pattern_ttnn.s", "TTNN Pattern"),
        # ("branch_pattern_biased_taken.s", "Biased 75% Taken"),
        # ("pipeline_test6.s", "Alternating (from test6)"),
        ("taylor_series.s", "Taylor Series"),
    ]

    all_results = {}
    for pattern_file, pattern_name in patterns:
        full_path = os.path.join(examples_dir, pattern_file)
        if os.path.exists(full_path):
            results = test_pattern(vm_path, full_path, pattern_name)
            all_results[pattern_name] = results
        else:
            print(f"\nWarning: Pattern file not found: {full_path}")

    # Summary
    print(f"\n{'='*80}")
    print("SUMMARY: Best Predictor for Each Pattern")
    print(f"{'='*80}")
    for pattern_name, results in all_results.items():
        if results:
            best = min(results, key=lambda x: x[1].get('cycles', float('inf')))
            print(f"{pattern_name:<40} -> {best[0]} ({best[1].get('cycles')} cycles, {best[1].get('mispredictions')} mispred)")

if __name__ == "__main__":
    main()
