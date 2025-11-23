#!/usr/bin/env python3
"""
Branch Pattern Verification Script
Runs branch pattern tests on single-stage and multi-stage processors,
compares register dumps to ensure correctness across all prediction strategies.
"""

import subprocess
import json
import sys
import os
import time

def run_vm_get_registers(vm_path, assembly_file, config_commands, wait_for_stats=False):
    """
    Runs the VM with the specified configuration and returns register dump.
    Returns (registers_dict, stats_dict) or (None, None) on failure.
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
        return None, None

    commands = config_commands + [
        f"load {assembly_file}",
        "run"
    ]

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
                return None, None
                
            line = process.stdout.readline()
            if not line:
                break
            
            # Parse statistics
            if "Total cycles:" in line:
                stats['cycles'] = int(line.split(":")[1].strip())
            elif "Branch mispredictions:" in line:
                stats['mispredictions'] = int(line.split(":")[1].strip())
                
            if "VM_PROGRAM_END" in line:
                program_ended = True
                # For single-stage, break immediately. For multi-stage, continue to get stats
                if not wait_for_stats:
                    break
                # Continue reading a few more lines to get statistics for multi-stage
                continue
            
            # After seeing VM_PROGRAM_END for multi-stage, if we have stats, we can break
            if program_ended and wait_for_stats and 'cycles' in stats and 'mispredictions' in stats:
                break
                
        process.stdin.write("exit\n")
        process.stdin.flush()
        process.wait()
        
        # Read register dump
        registers_dump_path = os.path.join(os.path.dirname(vm_path), "vm_state", "registers_dump.json")
        if not os.path.exists(registers_dump_path):
            print(f"Register dump not found at {registers_dump_path}")
            return None, None
        
        with open(registers_dump_path, 'r') as f:
            registers = json.load(f)
        
        return registers.get("gp_registers", {}), stats

    except Exception as e:
        print(f"Exception: {e}")
        process.terminate()
        return None, None

def compare_registers(ref_regs, test_regs, strategy_name):
    """Compare two register dictionaries and return mismatches."""
    mismatches = []
    all_regs = set(ref_regs.keys()) | set(test_regs.keys())
    
    for reg in sorted(all_regs, key=lambda x: int(x[1:])):
        ref_val = ref_regs.get(reg, "MISSING")
        test_val = test_regs.get(reg, "MISSING")
        if ref_val != test_val:
            mismatches.append({
                'register': reg,
                'expected': ref_val,
                'got': test_val
            })
    
    return mismatches

def test_pattern_correctness(vm_path, pattern_file, pattern_name):
    """Test all prediction strategies against single-stage reference."""
    print(f"\n{'='*80}")
    print(f"Testing: {pattern_name}")
    print(f"{'='*80}")
    
    # Get reference (single-stage)
    print("Running single-stage (reference)...")
    ref_regs, ref_stats = run_vm_get_registers(
        vm_path,
        pattern_file,
        ["mconfig Execution processor_type single_stage"]
    )
    
    if not ref_regs:
        print("❌ Failed to get single-stage reference")
        return False
    
    print(f"✓ Reference: cycles={ref_stats.get('cycles', 'N/A')}")
    
    # Test configurations
    configs = [
        ("Multi-stage (None)", ["mconfig Execution processor_type multi_stage_with_both", "mconfig Execution branch_prediction none"]),
        ("Multi-stage (Static NT)", ["mconfig Execution processor_type multi_stage_with_both", "mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy not_taken"]),
        ("Multi-stage (Static T)", ["mconfig Execution processor_type multi_stage_with_both", "mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy taken"]),
        ("Multi-stage (Dynamic)", ["mconfig Execution processor_type multi_stage_with_both", "mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 2"])
    ]

    all_passed = True
    results = []
    
    for name, cmds in configs:
        test_regs, test_stats = run_vm_get_registers(vm_path, pattern_file, cmds, wait_for_stats=True)
        
        if not test_regs:
            print(f"❌ {name}: FAILED (timeout/error)")
            all_passed = False
            continue
        
        mismatches = compare_registers(ref_regs, test_regs, name)
        
        if mismatches:
            print(f"❌ {name}: REGISTER MISMATCH")
            for m in mismatches[:5]:  # Show first 5
                print(f"   {m['register']}: expected={m['expected']}, got={m['got']}")
            if len(mismatches) > 5:
                print(f"   ... and {len(mismatches) - 5} more mismatches")
            all_passed = False
        else:
            cycles = test_stats.get('cycles', 'N/A')
            mispred = test_stats.get('mispredictions', 'N/A')
            print(f"✓ {name}: PASS (cycles={cycles}, mispred={mispred})")
        
        results.append((name, test_stats, len(mismatches) == 0))
    
    # Show expected values from comments
    print(f"\n Expected final register values (from test comments):")
    # Try to extract expected values from assembly file
    try:
        with open(pattern_file, 'r') as f:
            for line in f:
                if 'Expected:' in line:
                    print(f"  {line.strip()}")
                    break
    except:
        pass
    
    print(f"\n Actual final register values:")
    for reg in sorted(['x10', 'x11', 'x12'], key=lambda x: int(x[1:])):
        if reg in ref_regs:
            print(f"  {reg} = {ref_regs[reg]}")
    
    return all_passed

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
        ("branch_pattern_always_taken.s", "Always Taken"),
        ("branch_pattern_never_taken.s", "Always Not Taken"),
        ("branch_pattern_tntn.s", "Alternating (TNTN)"),
        ("branch_pattern_ttnn.s", "TTNN Pattern"),
        ("branch_pattern_biased_taken.s", "Biased 75% Taken"),
        # ("pipeline_test1.s", "Pipeline Test 1"),
        # ("pipeline_test2.s", "Pipeline Test 2"),
        # ("pipeline_test3.s", "Pipeline Test 3"),
        # ("pipeline_test4.s", "Pipeline Test 4"),
        # ("pipeline_test5.s", "Pipeline Test 5"),
        # ("pipeline_test6.s", "Pipeline Test 6"),
        # ("pipeline_test7.s", "Pipeline Test 7"),
    ]

    all_tests_passed = True
    
    for pattern_file, pattern_name in patterns:
        full_path = os.path.join(examples_dir, pattern_file)
        if os.path.exists(full_path):
            passed = test_pattern_correctness(vm_path, full_path, pattern_name)
            if not passed:
                all_tests_passed = False
        else:
            print(f"\n⚠️  Warning: Pattern file not found: {full_path}")
            all_tests_passed = False

    # Summary
    print(f"\n{'='*80}")
    if all_tests_passed:
        print("✓ ALL TESTS PASSED - All branch predictors produce correct results!")
    else:
        print("❌ SOME TESTS FAILED - Check output above for details")
    print(f"{'='*80}\n")
    
    sys.exit(0 if all_tests_passed else 1)

if __name__ == "__main__":
    main()
