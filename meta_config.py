#!/usr/bin/env python3
"""
Generate inline functions for config_ini_io.h with INI sections
"""

import re

def parse_struct_fields(struct_body):
    """Parse fields from a struct body, handling nested structs"""
    fields = []
    brace_depth = 0
    current_line = ""
    
    lines = struct_body.strip().split('\n')
    for line in lines:
        line = line.strip()
        if not line or line.startswith('//'):
            continue
            
        # Handle braces for nested structs
        brace_depth += line.count('{') - line.count('}')
        
        # Skip lines that are struct definitions themselves
        if 'struct' in line and '{' in line and brace_depth == 1:
            continue
            
        # Add to current line if we're inside braces or it's a continuation
        if brace_depth > 0 or line.endswith(','):
            current_line += " " + line
            if not line.endswith(','):
                # Parse the accumulated line
                parts = current_line.strip(';').split()
                if len(parts) >= 2:
                    type_name = parts[0]
                    var_names = ' '.join(parts[1:])
                    for var_part in var_names.split(','):
                        var_part = var_part.strip()
                        if var_part:
                            fields.append((type_name, var_part))
                current_line = ""
        else:
            # Parse single line
            parts = line.strip(';').split()
            if len(parts) >= 2:
                type_name = parts[0]
                var_names = ' '.join(parts[1:])
                for var_part in var_names.split(','):
                    var_part = var_part.strip()
                    if var_part:
                        fields.append((type_name, var_part))
    
    return fields

def get_format_specifier(type_name):
    """Get format specifier for a type"""
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
        return '%d'

def get_parser_function(type_name):
    """Get parser function for a type"""
    if 'float' in type_name or 'double' in type_name:
        return 'SDL_atof'
    else:
        return 'SDL_atoi'

def generate_config_functions():
    """Generate config_functions.h with INI sections"""
    
    # Read the current src/config_ini_io.h
    with open('src/config_ini_io.h', 'r') as f:
        content = f.read()
    
    # Parse the ConfigData struct and find all nested structs
    sections = []
    
    # First, find the ConfigData struct
    configdata_pattern = r'typedef\s+struct\s*\{([^}]+(?:{[^}]*}[^}]*)*)\}\s*ConfigData\s*;'
    match = re.search(configdata_pattern, content, re.DOTALL)
    
    if not match:
        print("Error: Could not find ConfigData struct in src/config_ini_io.h")
        return
    
    config_body = match.group(1)
    
    # Look for nested structs in the form: struct { ... } SectionName;
    # This pattern matches anonymous structs with a name
    section_pattern = r'struct\s*\{([^}]+)\}\s*(\w+)\s*;'
    
    section_matches = re.findall(section_pattern, config_body, re.DOTALL)
    
    for section_body, section_name in section_matches:
        fields = parse_struct_fields(section_body)
        if fields:
            sections.append({
                'name': section_name,
                'fields': fields
            })
    
    print(f"Found {len(sections)} sections:")
    for section in sections:
        field_names = [name for _, name in section['fields']]
        print(f"  [{section['name']}]: {field_names}")
    
    # Generate SaveConfig format string and arguments
    format_parts = []
    arg_lines = []
    
    for section in sections:
        format_parts.append(f"[{section['name']}]")
        for type_name, var_name in section['fields']:
            fmt = get_format_specifier(type_name)
            format_parts.append(f"{var_name}={fmt}")
            arg_lines.append(f"config->{section['name']}.{var_name}")
        format_parts.append("")  # Empty line between sections
    
    # Remove last empty line
    if format_parts and format_parts[-1] == "":
        format_parts.pop()
    
    format_string = '\\n'.join(format_parts) + '\\n'
    args_string = ',\n                 '.join(arg_lines)
    
    # Generate parsing blocks for LoadConfig
    parse_blocks = []
    current_section_var = None
    
    parse_blocks.append('    char* line = data;')
    parse_blocks.append('    char current_section[64] = {0};')
    parse_blocks.append('    ')
    parse_blocks.append('    while (*line) {')
    parse_blocks.append('        // Skip whitespace')
    parse_blocks.append('        while (*line == \' \' || *line == \'\\t\') line++;')
    parse_blocks.append('        ')
    parse_blocks.append('        // Check for section header')
    parse_blocks.append('        if (*line == \'[\') {')
    parse_blocks.append('            char* section_end = SDL_strchr(line, \']\');')
    parse_blocks.append('            if (section_end) {')
    parse_blocks.append('                size_t len = section_end - line - 1;')
    parse_blocks.append('                if (len < sizeof(current_section) - 1) {')
    parse_blocks.append('                    SDL_strlcpy(current_section, line + 1, len + 1);')
    parse_blocks.append('                }')
    parse_blocks.append('                line = section_end + 1;')
    parse_blocks.append('            }')
    parse_blocks.append('        } else {')
    parse_blocks.append('            // Parse key=value pairs')
    
    # Generate section-specific parsing
    for section_idx, section in enumerate(sections):
        section_condition = f'            if (SDL_strcmp(current_section, "{section["name"]}") == 0) {{'
        parse_blocks.append(section_condition)
        
        for field_idx, (type_name, var_name) in enumerate(section['fields']):
            length = len(var_name) + 1  # +1 for '='
            parser = get_parser_function(type_name)
            
            if field_idx == 0:
                parse_blocks.append(f'                if (SDL_strncmp(line, "{var_name}=", {length}) == 0) {{')
            else:
                parse_blocks.append(f'                }} else if (SDL_strncmp(line, "{var_name}=", {length}) == 0) {{')
            
            parse_blocks.append(f'                    config->{section["name"]}.{var_name} = {parser}(line + {length});')
        
        # Close the if-else chain for this section
        if section['fields']:
            parse_blocks.append('                }')
        
        parse_blocks.append('            }')
    
    parse_blocks.append('            ')
    parse_blocks.append('            // Skip to next line')
    parse_blocks.append('            while (*line && *line != \'\\n\') line++;')
    parse_blocks.append('        }')
    parse_blocks.append('        ')
    parse_blocks.append('        if (*line == \'\\n\') line++;')
    parse_blocks.append('    }')
    
    parse_logic = '\n'.join(parse_blocks)
    
    # Write to config_functions.h
    functions_content = f"""/* Generated by gen_config_functions.py - DO NOT EDIT MANUALLY */
#pragma once
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
    
    with open('src/generated/config_functions.h', 'w') as f:
        f.write(functions_content)
    
    print("\nGenerated config_functions.h with INI sections successfully!")
    print("Include this file in your config_ini_io.h and call the generated functions.")

if __name__ == "__main__":
    generate_config_functions()