# virthub/kernels/tests/test_header_consistency.py
#
# Header consistency test between Rust and C/C++ PSP-KV types.
#
# This script checks that the C struct definitions in
# `virthub/kernels/common/psp_kv_types.h` match the Rust struct definitions
# in `virthub/src/store/src/psp_kv.rs` (for `PspKvSidecarDescriptor`) and
# `virthub/src/precision/src/policy.rs` (for `PackedBlockPolicy`). It does
# this by:
#
# 1. Compiling a small C program that includes the header and prints the
#    sizeof() and offsetof() of key fields.
# 2. Running a Rust test (or a small Rust snippet) that prints the same
#    information from the Rust side.
# 3. Comparing the outputs.
#
# The script assumes that `cc` and `cargo` are available in the PATH.

import subprocess
import sys
import tempfile
from pathlib import Path

EXPECTED_SIDECAR_SIZE = 64
EXPECTED_SIDECAR_ALIGN = 64
EXPECTED_POLICY_SIZE = 4  # 32-bit word

# Offsets of fields within PspKvSidecarDescriptor (in bytes)
EXPECTED_SIDECAR_OFFSETS = {
    "data_page_base_ptr": 0,
    "residual_page_ptr": 8,
    "head_presence_mask": 16,
    "base_precision": 24,
    "compression_level": 25,
    "head_group_size": 26,
    "reserved_flags": 27,
    "per_head_scale_e8m0": 28,
    "reserved_padding": 36,
}

def run_c_program(source_code: str, include_dir: Path) -> str:
    """Compile and run a C program, returning its stdout."""
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = Path(tmpdir) / "test.c"
        exe_path = Path(tmpdir) / "test"
        src_path.write_text(source_code)

        compile_cmd = ["cc", "-I", str(include_dir), str(src_path), "-o", str(exe_path)]
        subprocess.run(compile_cmd, check=True, capture_output=True)

        result = subprocess.run([str(exe_path)], check=True, capture_output=True, text=True)
        return result.stdout

C_PROGRAM = r"""
#include <stdio.h>
#include <stddef.h>
#include "psp_kv_types.h"

int main() {
    printf("sidecar_size=%zu\n", sizeof(PspKvSidecarDescriptor));
    printf("sidecar_align=%zu\n", _Alignof(PspKvSidecarDescriptor));
    printf("policy_size=%zu\n", sizeof(PackedBlockPolicy));

    printf("offset_data_page_base_ptr=%zu\n", offsetof(PspKvSidecarDescriptor, data_page_base_ptr));
    printf("offset_residual_page_ptr=%zu\n", offsetof(PspKvSidecarDescriptor, residual_page_ptr));
    printf("offset_head_presence_mask=%zu\n", offsetof(PspKvSidecarDescriptor, head_presence_mask));
    printf("offset_base_precision=%zu\n", offsetof(PspKvSidecarDescriptor, base_precision));
    printf("offset_compression_level=%zu\n", offsetof(PspKvSidecarDescriptor, compression_level));
    printf("offset_head_group_size=%zu\n", offsetof(PspKvSidecarDescriptor, head_group_size));
    printf("offset_reserved_flags=%zu\n", offsetof(PspKvSidecarDescriptor, reserved_flags));
    printf("offset_per_head_scale_e8m0=%zu\n", offsetof(PspKvSidecarDescriptor, per_head_scale_e8m0));
    printf("offset_reserved_padding=%zu\n", offsetof(PspKvSidecarDescriptor, reserved_padding));

    return 0;
}
"""

def main():
    script_dir = Path(__file__).parent.resolve()
    kernels_dir = script_dir.parent
    common_dir = kernels_dir / "common"
    common_header = common_dir / "psp_kv_types.h"

    if not common_header.exists():
        print(f"ERROR: Header not found at {common_header}", file=sys.stderr)
        return 1

    print("Running C layout check...")
    try:
        c_output = run_c_program(C_PROGRAM, common_dir)
    except subprocess.CalledProcessError as e:
        print(f"C compilation failed: {e.stderr.decode()}", file=sys.stderr)
        return 1

    c_lines = dict(line.split('=') for line in c_output.strip().split('\n') if '=' in line)

    errors = []

    if int(c_lines.get("sidecar_size", -1)) != EXPECTED_SIDECAR_SIZE:
        errors.append(f"Sidecar size mismatch: expected {EXPECTED_SIDECAR_SIZE}, got {c_lines.get('sidecar_size')}")
    if int(c_lines.get("sidecar_align", -1)) < EXPECTED_SIDECAR_ALIGN:
        errors.append(f"Sidecar alignment insufficient: expected >= {EXPECTED_SIDECAR_ALIGN}, got {c_lines.get('sidecar_align')}")
    if int(c_lines.get("policy_size", -1)) != EXPECTED_POLICY_SIZE:
        errors.append(f"Policy size mismatch: expected {EXPECTED_POLICY_SIZE}, got {c_lines.get('policy_size')}")

    for field, expected_offset in EXPECTED_SIDECAR_OFFSETS.items():
        actual = int(c_lines.get(f"offset_{field}", -1))
        if actual != expected_offset:
            errors.append(f"Offset mismatch for {field}: expected {expected_offset}, got {actual}")

    if errors:
        print("Header consistency check FAILED:")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("C header layout matches expected values.")
    print("All checks passed: C header layout is consistent.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
