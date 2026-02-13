#!/usr/bin/env python3
"""
detect_bad_spelling.py - Basic scan for bad English spelling in source code.

Usage:
    python detect_bad_spelling.py

Output:
    analysis/bad_english_report.md
"""

import re
import sys
from pathlib import Path
from datetime import datetime

# ----------------------------------------------------------------------
# bad → British spelling dictionary (basic)
# ----------------------------------------------------------------------
BAD_TO_BRITISH = {
    "color": "colour",
    "colors": "colours",
    "colorful": "colourful",
    "center": "centre",
    "centers": "centres",
    "centered": "centred",
    "dialog": "dialogue",
    "dialogs": "dialogues",
    "initialize": "initialise",
    "initializes": "initialises",
    "initialized": "initialised",
    "initializing": "initialising",
    "realize": "realise",
    "realizes": "realises",
    "realized": "realised",
    "realizing": "realising",
    "analyze": "analyse",
    "analyzes": "analyses",
    "analyzed": "analysed",
    "analyzing": "analysing",
    "behavior": "behaviour",
    "behaviors": "behaviours",
    "customize": "customise",
    "customizes": "customises",
    "customized": "customised",
    "customizing": "customising",
    "organize": "organise",
    "organizes": "organises",
    "organized": "organised",
    "organizing": "organising",
    "license": "licence",        # noun
    "licenses": "licences",
    "defense": "defence",
    "defenses": "defences",
    "offense": "offence",
    "offenses": "offences",
    "practise": "practice",      # verb vs noun - simplified
    "practised": "practised",    # both?    
    "aluminum": "aluminium",    
    "fiber": "fibre",
    "fibers": "fibres",
    "liter": "litre",
    "liters": "litres",
    "meter": "metre",
    "meters": "metres",
    "gray": "grey",
    "grays": "greys",
}

# ----------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------
PROJECT_ROOT = Path.cwd()
SOURCE_DIRS = [PROJECT_ROOT / "src"]
SOURCE_FILES = [PROJECT_ROOT / "main.cpp"]
OUTPUT_DIR = PROJECT_ROOT / "analysis"
OUTPUT_FILE = OUTPUT_DIR / "bad_english_report.md"

# Extensions to scan
VALID_EXTENSIONS = {".cpp", ".h", ".hpp", ".c", ".cc", ".cxx"}

# Directories to skip (substring match)
SKIP_PATHS = ["generated", "external", "build", "release", ".git"]

# ----------------------------------------------------------------------
# File scanner
# ----------------------------------------------------------------------
def should_skip_file(path: Path) -> bool:
    """Return True if the file is generated or external."""
    if any(part in SKIP_PATHS for part in path.parts):
        return True
    if path.suffix.lower() not in VALID_EXTENSIONS:
        return True
    return False

def collect_source_files() -> list[Path]:
    """Return list of all .cpp/.h files to scan."""
    files = []
    for dir_path in SOURCE_DIRS:
        if dir_path.exists():
            for ext in VALID_EXTENSIONS:
                files.extend(dir_path.rglob(f"*{ext}"))
    for file_path in SOURCE_FILES:
        if file_path.exists():
            files.append(file_path)
    # Deduplicate and filter
    unique = set()
    filtered = []
    for f in files:
        if f in unique:
            continue
        unique.add(f)
        if not should_skip_file(f):
            filtered.append(f)
    return sorted(filtered)

# ----------------------------------------------------------------------
# Spelling scanner
# ----------------------------------------------------------------------
def scan_file(file_path: Path) -> list[dict]:
    """Scan a single file for bad spelling."""
    findings = []
    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            lines = f.readlines()
    except Exception as e:
        print(f"⚠ Could not read {file_path}: {e}", file=sys.stderr)
        return findings

    for line_num, line in enumerate(lines, start=1):
        # Simple word tokenisation: sequences of letters
        for match in re.finditer(r"\b[a-zA-Z]+\b", line):
            word = match.group()
            word_lower = word.lower()
            if word_lower in BAD_TO_BRITISH:
                findings.append({
                    "file": str(file_path),
                    "line": line_num,
                    "column": match.start() + 1,
                    "original": word,
                    "suggestion": BAD_TO_BRITISH[word_lower],
                    "context": line.strip(),
                })
    return findings

# ----------------------------------------------------------------------
# Markdown report writer
# ----------------------------------------------------------------------
def write_report(all_findings: list[dict]) -> None:
    """Generate a Markdown report from all findings."""
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    lines = []
    lines.append(f"# bad English Spelling Report\n")
    lines.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    lines.append(f"Dictionary size: {len(BAD_TO_BRITISH)} entries\n")
    lines.append(f"Files scanned: {len(set(f['file'] for f in all_findings))}\n")
    lines.append(f"Total matches: {len(all_findings)}\n")
    lines.append("---\n")

    if not all_findings:
        lines.append("✅ No bad spelling detected.\n")
    else:
        # Group by file
        by_file = {}
        for f in all_findings:
            by_file.setdefault(f["file"], []).append(f)

        for file_path in sorted(by_file.keys()):
            lines.append(f"## `{file_path}`\n")
            lines.append("| Line | Column | Original | Suggestion | Context |")
            lines.append("|------|--------|---------|------------|---------|")
            for finding in by_file[file_path]:
                # Escape pipe characters in context
                ctx = finding["context"].replace("|", "\\|")
                if len(ctx) > 60:
                    ctx = ctx[:57] + "..."
                lines.append(
                    f"| {finding['line']} | {finding['column']} | "
                    f"`{finding['original']}` | `{finding['suggestion']}` | {ctx} |"
                )
            lines.append("")

    OUTPUT_FILE.write_text("\n".join(lines), encoding="utf-8")
    print(f"✅ Report written to {OUTPUT_FILE}")

# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------
def main():
    print("🔍 Scanning for bad English spelling...")
    files = collect_source_files()
    print(f"📁 Found {len(files)} source files to scan.")

    all_findings = []
    for f in files:
        findings = scan_file(f)
        if findings:
            print(f"  {f}: {len(findings)} match(es)")
            all_findings.extend(findings)

    write_report(all_findings)
    print("✅ Done.")

if __name__ == "__main__":
    main()