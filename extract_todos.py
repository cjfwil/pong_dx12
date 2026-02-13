#!/usr/bin/env python3
"""
extract_todos.py – Scan C/C++/HLSL files for TODO comments and generate a Markdown report.

Usage:
    python extract_todos.py [--src DIR] [--out FILE] [--context LINES] [--ext EXTENSIONS]

Examples:
    python extract_todos.py --src . --out TODO_report.md
    python extract_todos.py --ext .cpp,.h,.hlsl --context 3
"""

import argparse
import re
import sys
from pathlib import Path
from collections import defaultdict

# ----------------------------------------------------------------------
# Try to use common.py if available – otherwise use internal fallbacks
# ----------------------------------------------------------------------
try:
    import common
    write_file = common.write_file_if_changed
    log_info = common.log_info
    log_success = common.log_success
    log_warning = common.log_warning
    log_error = common.log_error
except ImportError:
    # Simple fallback implementations
    def write_file(path, content):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
        print(f"Written: {path}")
        return True

    def log_info(msg):   print(f"[INFO] {msg}")
    def log_success(msg):print(f"[ OK ] {msg}")
    def log_warning(msg):print(f"[WARN] {msg}", file=sys.stderr)
    def log_error(msg):  print(f"[ERR ] {msg}", file=sys.stderr)

# ----------------------------------------------------------------------
# Default settings
# ----------------------------------------------------------------------
DEFAULT_EXTS = {'.cpp', '.h', '.hpp', '.c', '.cc', '.cxx', '.hlsl'}
DEFAULT_EXCLUDE_DIRS = {'.git', '__pycache__', 'release', 'build', 'out', 'generated'}

# ----------------------------------------------------------------------
# Core extraction logic
# ----------------------------------------------------------------------
def find_files(root: Path, extensions, exclude_dirs):
    """Yield all files under root that match given extensions, excluding certain dirs."""
    for p in root.rglob('*'):
        if not p.is_file():
            continue
        if any(part in exclude_dirs for part in p.parts):
            continue
        if p.suffix.lower() in extensions:
            yield p

def extract_todos_from_file(file_path: Path, context_lines: int):
    """
    Scan a single file for lines containing 'todo' (case‑insensitive).
    Returns list of dicts: {
        'line_num': int,
        'line': str,
        'context': list of str (surrounding lines)
    }
    """
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()
    except Exception as e:
        log_warning(f"Failed to read {file_path}: {e}")
        return []

    todos = []
    lines = [line.rstrip('\n\r') for line in lines]  # keep original line endings stripped

    for idx, line in enumerate(lines):
        if re.search(r'todo', line, re.IGNORECASE):
            # Determine context window
            start = max(0, idx - context_lines)
            end = min(len(lines), idx + context_lines + 1)
            context = lines[start:end]

            todos.append({
                'line_num': idx + 1,  # 1‑based for humans
                'line': line.strip(),
                'context': context.copy(),
                'context_start': start + 1,  # line number where context begins
            })

    return todos

def merge_todos(todos_by_file):
    """
    No merging needed per file – but we might want to sort each file's todos by line number.
    """
    for file, todos in todos_by_file.items():
        todos.sort(key=lambda t: t['line_num'])
    return todos_by_file

# ----------------------------------------------------------------------
# Markdown generation
# ----------------------------------------------------------------------
def write_markdown_report(todos_by_file, output_path: Path):
    """Generate a nicely formatted Markdown report."""
    lines = []
    lines.append("# Extracted TODOs\n")
    lines.append(f"Generated on {common.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    lines.append("")

    total_todos = sum(len(todos) for todos in todos_by_file.values())
    lines.append(f"**Total files with TODOs:** {len(todos_by_file)}  ")
    lines.append(f"**Total TODO entries:** {total_todos}\n")
    lines.append("---\n")

    for file_path in sorted(todos_by_file.keys()):
        todos = todos_by_file[file_path]
        rel_path = file_path.as_posix()
        lines.append(f"## `{rel_path}`  ({len(todos)})\n")

        for todo in todos:
            lines.append(f"### Line {todo['line_num']}\n")
            lines.append(f"**Line content:** `{todo['line']}`\n")
            lines.append("**Context:**\n")
            lines.append("```")
            # Show context with line numbers
            context = todo['context']
            start_num = todo['context_start']
            for i, ctx_line in enumerate(context):
                line_num = start_num + i
                prefix = "→" if (line_num == todo['line_num']) else " "
                lines.append(f"{prefix} {line_num:4d}: {ctx_line}")
            lines.append("```\n")

        lines.append("---\n")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text('\n'.join(lines), encoding='utf-8')
    log_success(f"TODO report written to {output_path}")
    return True

# ----------------------------------------------------------------------
# CLI entry point
# ----------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        description="Extract TODO comments from C/C++/HLSL files and generate a Markdown report."
    )
    parser.add_argument('--src', '-s', type=Path, default=Path('.'),
                        help='Root directory to scan (default: current directory)')
    parser.add_argument('--out', '-o', type=Path, default=Path('TODO_report.md'),
                        help='Output Markdown file (default: TODO_report.md)')
    parser.add_argument('--context', '-c', type=int, default=2,
                        help='Number of context lines before/after (default: 2)')
    parser.add_argument('--ext', '-e', type=str,
                        help='Comma-separated file extensions to include (default: .cpp,.h,.hpp,.c,.cc,.cxx,.hlsl)')
    parser.add_argument('--exclude-dirs', type=str,
                        help='Comma-separated directory names to exclude (default: .git,__pycache__,release,build,out,generated)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Print verbose output')

    args = parser.parse_args()

    # Configure extensions
    extensions = set()
    if args.ext:
        for e in args.ext.split(','):
            e = e.strip().lower()
            if not e.startswith('.'):
                e = '.' + e
            extensions.add(e)
    else:
        extensions = DEFAULT_EXTS

    # Configure excluded dirs
    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)
    if args.exclude_dirs:
        exclude_dirs.update(d.strip() for d in args.exclude_dirs.split(','))

    if args.verbose:
        log_info(f"Scanning {args.src} for files with extensions: {extensions}")
        log_info(f"Excluding directories containing: {exclude_dirs}")

    # Collect all relevant files
    files = list(find_files(args.src, extensions, exclude_dirs))
    log_info(f"Found {len(files)} files to scan.")

    # Extract TODOs
    todos_by_file = defaultdict(list)
    for file_path in files:
        todos = extract_todos_from_file(file_path, args.context)
        if todos:
            todos_by_file[file_path] = todos
            if args.verbose:
                log_info(f"  {file_path}: {len(todos)} TODO(s)")

    if not todos_by_file:
        log_info("No TODOs found.")
        return 0

    # Write report
    write_markdown_report(todos_by_file, args.out)
    return 0

if __name__ == '__main__':
    sys.exit(main())