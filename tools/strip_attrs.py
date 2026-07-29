#!/usr/bin/env python3
"""Strip .riscv.attributes from all .o members in a .a archive, handling duplicate member names."""
import subprocess, sys, os, tempfile, shutil, re
from collections import Counter

OBJCOPY = "/home/ubuntu/k230_sdk/toolchain/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin/riscv64-unknown-linux-musl-objcopy"

def strip_archive(archive_path):
    name = os.path.basename(archive_path)

    # Get member list with ar t
    result = subprocess.run(["ar", "t", archive_path], capture_output=True, text=True)
    members = [m for m in result.stdout.strip().split('\n') if m]

    # Count duplicates
    counts = Counter(members)
    dups = {m: c for m, c in counts.items() if c > 1}

    # Extract all members
    tmpdir = tempfile.mkdtemp()
    subprocess.run(["cp", archive_path, tmpdir + "/orig.a"], check=True)

    cwd = os.getcwd()
    os.chdir(tmpdir)

    # ar x extracts one copy of each member name (last one wins for duplicates)
    subprocess.run(["ar", "x", "orig.a"], check=True)

    # Strip all extracted .o files
    for f in os.listdir("."):
        if f.endswith(".o"):
            subprocess.run([OBJCOPY, "--remove-section=.riscv.attributes", f],
                          capture_output=True)

    # For duplicates: manually extract each occurrence
    seen = {}
    for member in members:
        if member not in dups:
            continue
        seen[member] = seen.get(member, 0) + 1
        dup_name = f"{member[:-2]}_dup{seen[member]}.o"
        # ar x with member name extracts one occurrence
        subprocess.run(["ar", "x", "orig.a", member], check=True)
        if os.path.exists(member) and not os.path.exists(dup_name):
            os.rename(member, dup_name)
            subprocess.run([OBJCOPY, "--remove-section=.riscv.attributes", dup_name],
                          capture_output=True)

    # Rebuild archive
    obj_files = sorted([f for f in os.listdir(".") if f.endswith(".o") and f != "orig.a"])
    if obj_files:
        subprocess.run(["ar", "rcs", archive_path] + obj_files, check=True)

    os.chdir(cwd)
    shutil.rmtree(tmpdir)
    print(f"  OK {name}: {len(members)} members, {len(dups)} with duplicates")

# Main
if len(sys.argv) > 1:
    # Process specified files
    for f in sys.argv[1:]:
        strip_archive(f)
else:
    # Process all .a files in deps
    base = "/home/ubuntu/hjl/dice_game/deps"
    dirs = [
        f"{base}/nncase/lib",
        f"{base}/nncase/rvvlib",
        f"{base}/mpp/userapps/lib",
        f"{base}/opencv/lib",
        f"{base}/opencv/lib/opencv4/3rdparty",
    ]
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.endswith(".a"):
                strip_archive(os.path.join(d, f))

print("Done")
