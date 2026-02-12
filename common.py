#!/usr/bin/env python3
"""
common.py – Shared utilities for pong_dx12 metaprogramming.
Now with robust comma‑separated field parsing.
"""

import sys
import os
import re
from pathlib import Path
from datetime import datetime
from typing import List, Tuple, Dict, Optional, Union

# ----------------------------------------------------------------------
# ANSI color codes – only used if stdout is a TTY
# ----------------------------------------------------------------------
_USE_COLOR = hasattr(sys.stdout, 'isatty') and sys.stdout.isatty()

def _color(code: int, text: str) -> str:
    return f"\033[{code}m{text}\033[0m" if _USE_COLOR else text

def log_info(msg: str) -> None:
    print(f"[INFO] {msg}")

def log_success(msg: str) -> None:
    print(_color(32, f"[ OK ] {msg}"))

def log_warning(msg: str) -> None:
    print(_color(33, f"[WARN] {msg}"), file=sys.stderr)

def log_error(msg: str) -> None:
    print(_color(31, f"[ERR ] {msg}"), file=sys.stderr)

# ----------------------------------------------------------------------
# File system helpers
# ----------------------------------------------------------------------
def ensure_dir(path: Union[str, Path]) -> Path:
    p = Path(path)
    p.mkdir(parents=True, exist_ok=True)
    return p

def write_file_if_changed(path: Union[str, Path], content: str) -> bool:
    path = Path(path)
    if path.exists():
        try:
            with open(path, 'r', encoding='utf-8') as f:
                old = f.read()
            if old == content:
                log_info(f"Already up‑to‑date: {path}")
                return False
        except:
            pass
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)
    log_success(f"Written: {path}")
    return True

# ----------------------------------------------------------------------
# Generated file header
# ----------------------------------------------------------------------
def make_header(tool_name: str = None, comment: str = "GENERATED") -> str:
    if tool_name:
        tool_line = f"//   by {tool_name}\n"
    else:
        tool_line = ""
    return (
        f"//------------------------------------------------------------------------\n"
        f"// {comment} – DO NOT EDIT\n"
        f"//   This file was automatically generated.\n"
        f"{tool_line}"
        f"//   Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
        f"//------------------------------------------------------------------------\n"
        f"\n"
    )

# ----------------------------------------------------------------------
# C declaration parser (handles commas, arrays, pointers)
# ----------------------------------------------------------------------
StructField = Tuple[str, str, bool, Optional[str], bool]  # (type, name, is_array, array_size, is_pointer)

def parse_declaration_line(line: str) -> List[StructField]:
    """
    Parse a single C declaration line like:
        int a;
        int b, c, d;
        float* e;
        ID3D12* ptr[4];
    Returns list of (type, name, is_array, array_size, is_pointer).
    """
    line = line.rstrip(';').strip()
    if not line:
        return []

    # Split by commas – simple because config fields never have commas inside brackets
    parts = line.split(',')
    # First part contains type + first variable name
    first = parts[0].strip()
    # Split first part into tokens; last token is variable name, preceding are type
    tokens = first.split()
    if len(tokens) < 2:
        log_warning(f"Invalid declaration line: {line}")
        return []
    var_name = tokens[-1]
    type_name = ' '.join(tokens[:-1])
    fields = []

    # Helper to extract array and pointer info from a variable name string
    def parse_variable(var: str) -> Tuple[str, bool, Optional[str], bool]:
        is_array = False
        array_size = None
        is_pointer = False
        # check for array brackets
        m = re.match(r'^(\w+)\[(\w*)\]$', var)
        if m:
            is_array = True
            var = m.group(1)
            array_size = m.group(2) if m.group(2) else None
        # check for leading '*'
        if var.startswith('*'):
            is_pointer = True
            var = var.lstrip('*').strip()
        return var, is_array, array_size, is_pointer

    # Parse first variable
    clean_name, is_arr, arr_sz, is_ptr = parse_variable(var_name)
    # if pointer star was in type (e.g. "float* e"), we already have '*'
    if '*' in type_name:
        is_ptr = True
        type_name = type_name.replace('*', '').strip()
    fields.append((type_name, clean_name, is_arr, arr_sz, is_ptr))

    # Remaining parts are variable names only (no type)
    for part in parts[1:]:
        v = part.strip()
        if not v:
            continue
        clean_name, is_arr, arr_sz, is_ptr = parse_variable(v)
        fields.append((type_name, clean_name, is_arr, arr_sz, is_ptr))

    return fields

# ----------------------------------------------------------------------
# C struct field extraction
# ----------------------------------------------------------------------
def parse_struct_fields(content: str, struct_name: str) -> List[StructField]:
    """
    Extract fields from a named struct.
    Works for: typedef struct {...} Name;  or  struct Name {...};  or  static struct {...} var;
    Returns list of StructField tuples.
    """
    patterns = [
        rf'typedef\s+struct\s*\{{(.*?)\}}\s*{struct_name}\s*;',
        rf'struct\s+{struct_name}\s*\{{(.*?)\}};',
        rf'static\s+struct\s*\{{(.*?)\}}\s*\w+\s*;',
    ]
    body = None
    for pat in patterns:
        m = re.search(pat, content, re.DOTALL)
        if m:
            body = m.group(1)
            break
    if body is None:
        log_warning(f"Struct '{struct_name}' not found")
        return []

    # remove comments
    body = re.sub(r'//.*?$', '', body, flags=re.MULTILINE)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.DOTALL)

    fields = []
    for decl in body.split(';'):
        decl = decl.strip()
        if not decl or decl.startswith('#'):
            continue
        fields.extend(parse_declaration_line(decl))
    return fields

def parse_nested_structs(content: str) -> Dict[str, List[StructField]]:
    """
    Find all anonymous struct definitions like: struct { ... } member;
    Returns dict mapping member name -> list of its fields.
    """
    nested = {}
    pattern = r'struct\s*\{(.*?)\}\s*(\w+)\s*;'
    for m in re.finditer(pattern, content, re.DOTALL):
        body = m.group(1)
        name = m.group(2)
        # remove comments
        body = re.sub(r'//.*?$', '', body, flags=re.MULTILINE)
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.DOTALL)
        fields = []
        for decl in body.split(';'):
            decl = decl.strip()
            if not decl or decl.startswith('#'):
                continue
            fields.extend(parse_declaration_line(decl))
        nested[name] = fields
    return nested

def parse_com_pointers(content: str, struct_names: List[str]) -> List[Dict]:
    """
    Specialised for D3D12 COM pointers (used by meta_ondestroy).
    Extracts ID3D* / IDXGI* members from given structs.
    """
    resources = []
    for struct_name in struct_names:
        fields = parse_struct_fields(content, struct_name)
        for typ, name, is_array, array_size, _ in fields:
            if typ.startswith('ID3D') or typ.startswith('IDXGI'):
                resources.append({
                    'struct': struct_name,
                    'type': typ,
                    'name': name,
                    'is_array': is_array,
                    'array_size': array_size
                })
    return resources

# ----------------------------------------------------------------------
# Argument parser helper
# ----------------------------------------------------------------------
def add_common_args(parser):
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Print verbose output')
    parser.add_argument('--force', '-f', action='store_true',
                        help='Force regeneration even if up‑to‑date')
    return parser

# ----------------------------------------------------------------------
# End of common.py
# ----------------------------------------------------------------------