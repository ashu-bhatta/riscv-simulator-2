import subprocess
import time
import os

vm_path = "../build/vm"
registers_dump_path = "../build/vm_state/registers_dump.json"

if os.path.exists(registers_dump_path):
    initial_mtime = os.path.getmtime(registers_dump_path)
else:
    initial_mtime = 0

print(f"Initial mtime: {initial_mtime}")

process = subprocess.Popen(
    [vm_path],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    cwd="../build"
)

commands = """
load ../examples/pipeline_test2.s
run
exit
"""

stdout, stderr = process.communicate(input=commands)

print("STDOUT:", stdout)
print("STDERR:", stderr)

if os.path.exists(registers_dump_path):
    final_mtime = os.path.getmtime(registers_dump_path)
    print(f"Final mtime: {final_mtime}")
    if final_mtime > initial_mtime:
        print("registers_dump.json was updated.")
    else:
        print("registers_dump.json was NOT updated.")
else:
    print("registers_dump.json does not exist.")
