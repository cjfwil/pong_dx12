#!/usr/bin/env python3
"""
debug_analysis.py – Run MSVC /analyze static analysis on the codebase.
"""

import subprocess
import sys
from pathlib import Path

# ----------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------
ANALYZE_FLAGS = [
    "/analyze",
    "/D_DEBUG",          # keep asserts enabled
    "/Zi",               # debug symbols (optional)
    "/EHsc",
    "/MDd",              # debug runtime
    "/nologo",
    "/I", "src",
    "/I", "src/generated",
    "/Fe:analyze.exe"
]

# ----------------------------------------------------------------------
# Build function
# ----------------------------------------------------------------------
def build_analyze():
    """Run cl.exe with /analyze on main.cpp."""
    print("=== Running MSVC /analyze static analysis ===")
    
    cmd = ["cl.exe"] + ANALYZE_FLAGS + ["main.cpp"]
    print("Running:", " ".join(cmd))
    
    result = subprocess.run(cmd, shell=True)
    
    if result.returncode != 0:
        print("❌ Analysis failed (compiler errors)")
        return False
    
    print("✔ Analysis completed successfully")
    print("\n📄 Warnings and analysis output are shown above.")
    print("   Look for lines containing '[ANALYZE]' in the output.")
    return True

# ----------------------------------------------------------------------
# CLI entry point
# ----------------------------------------------------------------------
if __name__ == "__main__":
    success = build_analyze()
    sys.exit(0 if success else 1)