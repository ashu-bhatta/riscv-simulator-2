#!/usr/bin/env python3
"""
Comprehensive Branch Pattern Testing and Analysis Script

This script:
1. VERIFICATION PHASE: Verifies correctness by comparing single-stage vs multi-stage execution
2. ANALYSIS PHASE: Analyzes performance statistics for different branch prediction strategies

If verification fails, the script reports errors and exits. Otherwise, it proceeds to analysis.
"""

proc1_type = "single_stage"
proc2_type = "multi_stage_with_both"

import subprocess
import json
import sys
import os
import time

def run_vm_get_registers(vm_path, assembly_file, config_commands, wait_for_stats=False):
    """
    Runs the VM with the specified configuration and returns register dump and stats.
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

def verify_pattern_correctness(vm_path, pattern_file, pattern_name):
    """
    VERIFICATION PHASE: Test all prediction strategies against single-stage reference.
    Returns (passed, stats_dict) where stats_dict contains performance data for each strategy.
    """
    print(f"\n{'='*80}")
    print(f"VERIFICATION: {pattern_name}")
    print(f"{'='*80}")
    
    # Get reference (single-stage)
    print("Running single-stage (reference)...")
    ref_regs, ref_stats = run_vm_get_registers(
        vm_path,
        pattern_file,
        ["mconfig Execution processor_type " + proc1_type]
    )
    
    if not ref_regs:
        print("❌ Failed to get single-stage reference")
        return False, {}
    
    print(f"✓ Reference: cycles={ref_stats.get('cycles', 'N/A')}")
    
    # Test configurations
    configs = [ 
        ("None", ["mconfig Execution processor_type " + proc2_type, "mconfig Execution branch_prediction none"]),
        ("Static NT", ["mconfig Execution processor_type " + proc2_type, "mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy not_taken"]),
        ("Static T", ["mconfig Execution processor_type " + proc2_type, "mconfig Execution branch_prediction static", "mconfig Execution static_branch_policy taken"]),
        ("Dynamic 1-bit", ["mconfig Execution processor_type " + proc2_type, "mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 1"]),
        ("Dynamic 2-bit", ["mconfig Execution processor_type " + proc2_type, "mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 2"]),
        ("Dynamic 4-bit", ["mconfig Execution processor_type " + proc2_type, "mconfig Execution branch_prediction dynamic", "mconfig Execution branch_prediction_bits 4"]),
    ]

    all_passed = True
    stats_results = {}
    
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
            stats_results[name] = test_stats
    
    return all_passed, stats_results

def analyze_pattern_performance(pattern_name, stats_results):
    """
    ANALYSIS PHASE: Analyze and display performance statistics for different predictors.
    """
    if not stats_results:
        return
    
    print(f"\n{'='*80}")
    print(f"ANALYSIS: {pattern_name}")
    print(f"{'='*80}")
    print(f"{'Strategy':<20} | {'Cycles':<8} | {'Mispred':<8} | {'CPI':<8} | {'Accuracy':<10}")
    print("-" * 80)
    
    for name, stats in stats_results.items():
        cycles = stats.get('cycles', 'N/A')
        mispred = stats.get('mispredictions', 'N/A')
        cpi = stats.get('cpi', 'N/A')
        
        # Calculate accuracy
        if stats.get('instructions') and stats.get('mispredictions') is not None:
            accuracy = 100.0 * (1.0 - stats['mispredictions'] / max(stats['instructions'], 1))
        else:
            accuracy = 'N/A'
        
        if isinstance(cpi, float):
            cpi_str = f"{cpi:.2f}"
        else:
            cpi_str = str(cpi)
        
        if isinstance(accuracy, float):
            acc_str = f"{accuracy:.1f}%"
        else:
            acc_str = str(accuracy)
        
        print(f"{name:<20} | {cycles:<8} | {mispred:<8} | {cpi_str:<8} | {acc_str:<10}")
    
    # Find best predictor
    valid_results = [(name, stats) for name, stats in stats_results.items() if stats.get('cycles')]
    if valid_results:
        best = min(valid_results, key=lambda x: x[1]['cycles'])
        print(f"\n✨ Best: {best[0]} - {best[1]['cycles']} cycles, {best[1].get('mispredictions', 'N/A')} mispredictions")

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
        # ("branch_pattern_always_taken.s", "Always Taken"),
        # ("branch_pattern_never_taken.s", "Always Not Taken"),
        # ("branch_pattern_tntn.s", "Alternating (TNTN)"),
        # ("branch_pattern_ttnn.s", "TTNN Pattern"),
        # ("branch_pattern_biased_taken.s", "Biased 75% Taken")
        ("taylor_series.s", "Taylor Series"),
    ]

    print("=" * 80)
    print("BRANCH PREDICTION TEST SUITE")
    print("=" * 80)
    print("\nPhase 1: VERIFICATION (comparing single-stage vs multi-stage)")
    print("Phase 2: ANALYSIS (performance comparison of prediction strategies)")
    print()

    verification_failed = False
    all_stats = {}
    
    # Phase 1: VERIFICATION
    for pattern_file, pattern_name in patterns:
        full_path = os.path.join(examples_dir, pattern_file)
        if os.path.exists(full_path):
            passed, stats_results = verify_pattern_correctness(vm_path, full_path, pattern_name)
            if not passed:
                verification_failed = True
            else:
                all_stats[pattern_name] = stats_results
        else:
            print(f"\n⚠️  Warning: Pattern file not found: {full_path}")
            verification_failed = True

    # Check verification results
    if verification_failed:
        print(f"\n{'='*80}")
        print("❌ VERIFICATION FAILED - Correctness issues detected!")
        print("Fix the issues above before proceeding to performance analysis.")
        print(f"{'='*80}\n")
        sys.exit(1)
    
    print(f"\n{'='*80}")
    print("✓ ALL VERIFICATION TESTS PASSED - Proceeding to analysis phase")
    print(f"{'='*80}")
    
    # Phase 2: ANALYSIS
    for pattern_name, stats_results in all_stats.items():
        analyze_pattern_performance(pattern_name, stats_results)
    
    # Summary
    print(f"\n{'='*80}")
    print("SUMMARY: Best Predictor for Each Pattern")
    print(f"{'='*80}")
    for pattern_name, stats_results in all_stats.items():
        if stats_results:
            valid_results = [(name, stats) for name, stats in stats_results.items() if stats.get('cycles')]
            if valid_results:
                best = min(valid_results, key=lambda x: x[1]['cycles'])
                print(f"{pattern_name:<30} → {best[0]:<20} ({best[1]['cycles']} cycles, {best[1].get('mispredictions', 'N/A')} mispred)")
    
    print(f"\n{'='*80}")
    print("✓ ALL TESTS COMPLETED SUCCESSFULLY")
    print(f"{'='*80}\n")
    
    sys.exit(0)

if __name__ == "__main__":
    main()
