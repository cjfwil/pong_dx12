#!/usr/bin/env python3
"""
Ultra-fast LOC counter for C/C++ and HLSL with per-file breakdown and exclusions.

Usage:
  python loc_fast.py [path] [--verbose] [--exclude name1,name2,...]

Defaults:
  path = .
  excludes = release,build,out,.git
"""

from pathlib import Path
import sys
import argparse

# map extensions to language group
EXT_GROUPS = {
    ".cpp": "cpp",
    ".c":   "cpp",
    ".cc":  "cpp",
    ".cxx": "cpp",
    ".h":   "cpp",
    ".hpp": "cpp",
    ".hh":  "cpp",
    ".hlsl":"hlsl",
    ".fx":  "hlsl",
    ".py":  "python"   # ← NEW
}

DEFAULT_EXCLUDES = {"release", "build", "out", ".git", "__pycache__"}


def count_file_lines(path: Path):
    total = blank = comment = code = 0
    in_block = False
    try:
        with path.open('r', encoding='utf-8', errors='ignore') as f:
            for raw in f:
                total += 1
                line = raw.rstrip('\n\r')
                if not line.strip():
                    blank += 1
                    continue

                i = 0
                n = len(line)
                has_code = False

                while i < n:
                    if in_block:
                        j = line.find('*/', i)
                        if j == -1:
                            i = n
                            break
                        else:
                            i = j + 2
                            in_block = False
                            continue

                    ch = line[i]

                    if ch == '/' and i + 1 < n and line[i+1] == '/':
                        break

                    if ch == '/' and i + 1 < n and line[i+1] == '*':
                        in_block = True
                        i += 2
                        continue

                    if not ch.isspace():
                        has_code = True
                        break

                    i += 1

                if has_code:
                    code += 1
                else:
                    comment += 1

    except Exception as e:
        print(f"Warning: failed to read {path}: {e}", file=sys.stderr)

    return total, blank, comment, code


def scan_folder(root: Path, excludes, verbose=False):
    totals = {
        "files": 0,
        "total": 0,
        "blank": 0,
        "comment": 0,
        "code": 0,
        "cpp_code": 0,
        "hlsl_code": 0,
        "python_code": 0   # ← NEW
    }
    per_file = []

    for p in sorted(root.rglob('*')):
        if not p.is_file():
            continue
        if any(part in excludes for part in p.parts):
            continue

        ext = p.suffix.lower()
        if ext not in EXT_GROUPS:
            continue

        totals["files"] += 1
        t, b, c, co = count_file_lines(p)

        totals["total"] += t
        totals["blank"] += b
        totals["comment"] += c
        totals["code"] += co

        group = EXT_GROUPS[ext]
        if group == "cpp":
            totals["cpp_code"] += co
        elif group == "hlsl":
            totals["hlsl_code"] += co
        elif group == "python":     # ← NEW
            totals["python_code"] += co

        per_file.append((p, group, t, b, c, co))

    # verbose table (unchanged)
    if verbose and per_file:
        path_strs = [str(p) for (p, *_ ) in per_file]
        max_path = max(len(s) for s in path_strs)
        totals_cols = list(zip(*[(t, b, c, co) for (_, _, t, b, c, co) in per_file]))

        def num_width(col_idx, header):
            if not totals_cols:
                return len(header)
            max_num = max(len(str(x)) for x in totals_cols[col_idx])
            return max(len(header), max_num)

        w_total = num_width(0, "total")
        w_blank = num_width(1, "blank")
        w_comment = num_width(2, "comment")
        w_code = num_width(3, "code")

        header_path = "File"
        header_fmt = f"{{:<{max_path}}}  |  {{:>{w_total}}}  {{:>{w_blank}}}  {{:>{w_comment}}}  {{:>{w_code}}}"
        print(header_fmt.format(header_path, "total", "blank", "comment", "code"))

        sep = "-" * max_path + "  |  " + "-" * w_total + "  " + "-" * w_blank + "  " + "-" * w_comment + "  " + "-" * w_code
        print(sep)

        for p, group, t, b, c, co in per_file:
            print(header_fmt.format(str(p), t, b, c, co))
        print()

    return totals, per_file


def main():
    ap = argparse.ArgumentParser(description="Ultra-fast LOC counter for C/C++ and HLSL")
    ap.add_argument("path", nargs="?", default=".")
    ap.add_argument("--verbose", "-v", action="store_true", help="Show per-file breakdown")
    ap.add_argument("--exclude", "-e", default=",".join(sorted(DEFAULT_EXCLUDES)),
                    help="Comma-separated directory names to exclude")
    args = ap.parse_args()

    root = Path(args.path)
    if not root.exists():
        print(f"Path not found: {root}", file=sys.stderr)
        sys.exit(2)

    excludes = set(x.strip() for x in args.exclude.split(",") if x.strip())
    totals, per_file = scan_folder(root, excludes, verbose=args.verbose)

    print(f"Scanned files: {totals['files']}")
    print(f"Total lines:   {totals['total']}")
    print(f"Code lines:    {totals['code']}")
    print(f"Comment lines: {totals['comment']}")
    print(f"Blank lines:   {totals['blank']}")
    print()
    print(f"C/C++:  {totals['cpp_code']} LOC")
    print(f"HLSL:   {totals['hlsl_code']} LOC")
    print(f"Python: {totals['python_code']} LOC")   # ← NEW


if __name__ == '__main__':
    main()