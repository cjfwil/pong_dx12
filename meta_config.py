#!/usr/bin/env python3
"""
meta_config.py – Generate INI serialization for ConfigData.

This script parses src/config_ini_io.h, extracts nested struct sections
(DisplaySettings, GraphicsSettings, etc.) and generates
src/generated/config_functions.h with Save/Load functions.

Now uses common.py for logging, file I/O, header generation, and struct parsing.
"""

import os
import re
import sys
from pathlib import Path

# ----------------------------------------------------------------------
# Local imports (common must be in Python path)
# ----------------------------------------------------------------------
try:
    import common
except ImportError:
    # If running as script, add parent directory or assume sibling
    sys.path.insert(0, str(Path(__file__).parent))
    import common

# ----------------------------------------------------------------------
# Format and parser helpers (could be moved to common later)
# ----------------------------------------------------------------------
def get_format_specifier(type_name: str) -> str:
    """Return printf format specifier for a C type."""
    if 'float' in type_name:
        return '%f'
    elif 'double' in type_name:
        return '%lf'
    elif type_name == 'bool':
        return '%d'
    elif type_name.startswith('uint') or type_name == 'unsigned':
        return '%u'
    elif type_name.startswith('int') or type_name == 'int':
        return '%d'
    elif 'char' in type_name and '*' in type_name:
        return '%s'
    else:
        return '%d'  # fallback

def get_parser_function(type_name: str) -> str:
    """Return SDL parser function name for a type."""
    if 'float' in type_name or 'double' in type_name:
        return 'SDL_atof'
    else:
        return 'SDL_atoi'

# ----------------------------------------------------------------------
# Main generator
# ----------------------------------------------------------------------
def generate_config_functions(
    input_path: Path = Path("src/config_ini_io.h"),
    output_path: Path = Path("src/generated/config_functions.h"),
    force: bool = False
) -> bool:
    """
    Generate config_functions.h from config_ini_io.h.
    Returns True if file was written/updated, False otherwise.
    """
    # 1. Read input file
    if not input_path.exists():
        common.log_error(f"Input file not found: {input_path}")
        return False

    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # 2. Extract ConfigData struct body
    #    Pattern: typedef struct { ... } ConfigData;
    pattern = r'typedef\s+struct\s*\{([^}]+(?:{[^}]*}[^}]*)*)\}\s*ConfigData\s*;'
    match = re.search(pattern, content, re.DOTALL)
    if not match:
        common.log_error(f"Could not find ConfigData struct in {input_path}")
        return False

    config_body = match.group(1)

    # 3. Find all nested structs inside ConfigData (e.g. DisplaySettings)
    #    common.parse_nested_structs returns dict: { member_name: list_of_fields }
    nested = common.parse_nested_structs(config_body)

    # Convert dict to list of sections with name and fields
    sections = []
    for name, fields in nested.items():
        # fields are (type, name, is_array, array_size, is_pointer)
        # we only need type and variable name
        simple_fields = [(typ, var) for (typ, var, _, _, _) in fields]
        sections.append({'name': name, 'fields': simple_fields})

    if not sections:
        common.log_warning("No nested structs found inside ConfigData")
        return False

    common.log_info(f"Found {len(sections)} config sections:")
    for sec in sections:
        field_names = [name for _, name in sec['fields']]
        common.log_info(f"  [{sec['name']}]: {field_names}")

    # 4. Generate SaveConfig format string and argument list
    format_parts = []
    arg_lines = []

    for sec in sections:
        format_parts.append(f"[{sec['name']}]")
        for typ, var in sec['fields']:
            fmt = get_format_specifier(typ)
            format_parts.append(f"{var}={fmt}")
            arg_lines.append(f"config->{sec['name']}.{var}")
        format_parts.append("")  # empty line between sections

    # Remove trailing empty line
    if format_parts and format_parts[-1] == "":
        format_parts.pop()

    format_string = '\\n'.join(format_parts) + '\\n'
    args_string = ',\n                 '.join(arg_lines)

    # 5. Generate LoadConfig parsing logic
    parse_lines = []
    parse_lines.append('    char* line = data;')
    parse_lines.append('    char current_section[64] = {0};')
    parse_lines.append('')
    parse_lines.append('    while (*line) {')
    parse_lines.append('        // Skip whitespace')
    parse_lines.append('        while (*line == \' \' || *line == \'\\t\') line++;')
    parse_lines.append('')
    parse_lines.append('        // Check for section header')
    parse_lines.append('        if (*line == \'[\') {')
    parse_lines.append('            char* section_end = SDL_strchr(line, \']\');')
    parse_lines.append('            if (section_end) {')
    parse_lines.append('                size_t len = section_end - line - 1;')
    parse_lines.append('                if (len < sizeof(current_section) - 1) {')
    parse_lines.append('                    SDL_strlcpy(current_section, line + 1, len + 1);')
    parse_lines.append('                }')
    parse_lines.append('                line = section_end + 1;')
    parse_lines.append('            }')
    parse_lines.append('        } else {')
    parse_lines.append('            // Parse key=value pairs')

    # Section-specific parsing
    for sec_idx, sec in enumerate(sections):
        condition = f'            if (SDL_strcmp(current_section, "{sec["name"]}") == 0) {{'
        parse_lines.append(condition)

        for field_idx, (typ, var) in enumerate(sec['fields']):
            length = len(var) + 1  # '=' included
            parser = get_parser_function(typ)

            if field_idx == 0:
                parse_lines.append(f'                if (SDL_strncmp(line, "{var}=", {length}) == 0) {{')
            else:
                parse_lines.append(f'                }} else if (SDL_strncmp(line, "{var}=", {length}) == 0) {{')

            parse_lines.append(f'                    config->{sec["name"]}.{var} = {parser}(line + {length});')

        if sec['fields']:
            parse_lines.append('                }')

        parse_lines.append('            }')

    parse_lines.append('')
    parse_lines.append('            // Skip to next line')
    parse_lines.append('            while (*line && *line != \'\\n\') line++;')
    parse_lines.append('        }')
    parse_lines.append('')
    parse_lines.append('        if (*line == \'\\n\') line++;')
    parse_lines.append('    }')

    parse_logic = '\n'.join(parse_lines)

    # 6. Assemble final content
    header = common.make_header(tool_name="meta_config.py", comment="GENERATED CONFIG FUNCTIONS")
    functions_content = header + f"""#pragma once
#include <SDL3/SDL.h>

/* Inline function to generate the config string with sections */
static inline void Generated_SaveConfigToString(ConfigData* config, char* buffer, size_t buffer_size) {{
    SDL_snprintf(buffer, buffer_size, 
                 "{format_string}", 
                 {args_string});
}}

/* Inline function to parse config from string data with sections */
static inline void Generated_LoadConfigFromString(ConfigData* config, char* data) {{
{parse_logic}
}}
"""

    # 7. Write output atomically
    return common.write_file_if_changed(output_path, functions_content)


# ----------------------------------------------------------------------
# CLI entry point
# ----------------------------------------------------------------------
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description="Generate config_functions.h from config_ini_io.h")
    common.add_common_args(parser)
    parser.add_argument('--input', '-i', type=Path, default=Path("src/config_ini_io.h"),
                        help="Input config header (default: src/config_ini_io.h)")
    parser.add_argument('--output', '-o', type=Path, default=Path("src/generated/config_functions.h"),
                        help="Output generated file (default: src/generated/config_functions.h)")
    args = parser.parse_args()

    # Configure verbose logging
    if args.verbose:
        # Replace log_info with more verbose? Not needed, common.log_info works.
        pass

    success = generate_config_functions(args.input, args.output, force=args.force)
    if not success:
        sys.exit(1)
    sys.exit(0)