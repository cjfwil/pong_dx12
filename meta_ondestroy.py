#!/usr/bin/env python3
"""
meta_ondestroy.py - Generate OnDestroy_generated.cpp.

Parses renderer_dx12.cpp, finds D3D12 COM pointers in known structs,
orders them by dependency, emits cleanup code.

Now uses common.py for logging, file I/O, and header generation,
but retains its own specialised parser (the original one works).
"""

import re
import sys
from pathlib import Path

# ----------------------------------------------------------------------
# Local imports
# ----------------------------------------------------------------------
try:
    import common
except ImportError:
    sys.path.insert(0, str(Path(__file__).parent))
    import common

# ----------------------------------------------------------------------
# Parser - exact original, proven to work
# ----------------------------------------------------------------------
def parse_dx12_resources(content: str) -> list:
    """Parse the header content and extract D3D12 resource declarations."""
    resources = []
    
    # Pattern to match COM pointer declarations in structs
    # Matches: ID3D12Something *name or ID3D12Something *name[N]
    pattern = r'(ID3D\w+|IDXGISwapChain\d*)\s*\*\s*(\w+)(\[[^\]]*\])?\s*(=\s*[^;]*)?;'
    
    # Find all static struct definitions with their names
    struct_pattern = r'static struct\s*\{(.+?)\}\s*(\w+);'
    
    for struct_match in re.finditer(struct_pattern, content, re.DOTALL):
        struct_body = struct_match.group(1)
        struct_name = struct_match.group(2)
        
        # Only process our known structs
        if struct_name not in ['pipeline_dx12', 'graphics_resources', 'sync_state']:
            continue
        
        # Find all COM pointers in this specific struct
        for match in re.finditer(pattern, struct_body):
            type_name = match.group(1)
            var_name = match.group(2)
            is_array = match.group(3) is not None
            
            if is_array:
                array_size = match.group(3).strip('[]')
                resources.append({
                    'struct': struct_name,
                    'type': type_name,
                    'name': var_name,
                    'is_array': True,
                    'array_size': array_size
                })
            else:
                resources.append({
                    'struct': struct_name,
                    'type': type_name,
                    'name': var_name,
                    'is_array': False,
                    'array_size': None
                })
    
    return resources

# ----------------------------------------------------------------------
# Cleanup ordering - original priority map
# ----------------------------------------------------------------------
PRIORITY_MAP = {
    # Sync objects (release first)
    'fence': 0,
    'm_fence': 0,
    # Constant buffers (need Unmap)
    'constantBuffer': 1,
    'm_constantBuffer': 1,
    # Graphics resources
    'texture': 2,
    'm_texture': 2,
    'vertexBuffer': 2,
    'm_vertexBuffer': 2,
    'depthStencil': 2,
    'm_depthStencil': 2,
    # Pipeline objects
    'rootSignature': 3,
    'm_rootSignature': 3,
    'pipelineState': 3,
    'm_pipelineState': 3,
    'commandList': 3,
    'm_commandList': 3,
    # Per-frame resources
    'commandAllocators': 4,
    'm_commandAllocators': 4,
    'renderTargets': 4,
    'm_renderTargets': 4,
    # Descriptor heaps
    'dsvHeap': 5,
    'm_dsvHeap': 5,
    'mainHeap': 5,
    'm_mainHeap': 5,
    'rtvHeap': 5,
    'm_rtvHeap': 5,
    # Swap chain
    'swapChain': 6,
    'm_swapChain': 6,
    # Command queue
    'commandQueue': 7,
    'm_commandQueue': 7,
    # Device (last)
    'device': 8,
    'm_device': 8,
}

def get_priority(resource: dict) -> int:
    """Return priority value for sorting (lower = earlier cleanup)."""
    name = resource['name']
    return PRIORITY_MAP.get(name, 999)

def get_category_comment(name: str) -> str:
    """Return descriptive comment for a resource category."""
    clean = name[2:] if name.startswith('m_') else name
    categories = {
        'fence': 'Release sync objects',
        'texture': 'Release graphics resources',
        'vertexBuffer': 'Release graphics resources',
        'indexBuffer': 'Release graphics resources',
        'depthStencil': 'Release graphics resources',
        'rootSignature': 'Release pipeline objects',
        'pipelineState': 'Release pipeline objects',
        'commandList': 'Release pipeline objects',
        'commandAllocators': 'Release per-frame resources',
        'renderTargets': 'Release per-frame resources',
        'dsvHeap': 'Release descriptor heaps',
        'mainHeap': 'Release descriptor heaps',
        'rtvHeap': 'Release descriptor heaps',
        'swapChain': 'Release swap chain',
        'commandQueue': 'Release command queue',
        'device': 'Release device (last)',
        'imguiHeap': 'Release other resources',
        'msaaRtvHeap': 'Release other resources',
        'msaaRenderTargets': 'Release other resources',
        'msaaDepthStencil': 'Release other resources',
        'PerSceneConstantBuffer': 'Release other resources',
        'PerFrameConstantBuffer': 'Release other resources',
    }
    return categories.get(clean, 'Release other resources')

# ----------------------------------------------------------------------
# Code generation - original logic
# ----------------------------------------------------------------------
def generate_cleanup_code(resources: list) -> str:
    """Generate C++ cleanup code."""
    lines = []
    lines.append("#pragma once")
    lines.append("#include \"renderer_dx12.cpp\"")
    lines.append("#define ONDESTROY_GENERATED_CPP")
    lines.append("")
    lines.append("void OnDestroy()")
    lines.append("{")
    lines.append("    // Ensure that the GPU is no longer referencing resources that are about to be")
    lines.append("    // cleaned up by the destructor.")
    lines.append("    WaitForGpu();")
    lines.append("")
    
    # Special handling for constant buffers (need to unmap)
    cb_resources = [r for r in resources if 'constantBuffer' in r['name'].lower()]
    if cb_resources:
        lines.append("    // Unmap and release constant buffers")
        for res in cb_resources:
            if res['is_array']:
                arr_sz = res['array_size'] if res['array_size'] else 'g_FrameCount'
                lines.append(f"    for (UINT i = 0; i < {arr_sz}; i++)")
                lines.append("    {")
                lines.append(f"        if ({res['struct']}.{res['name']}[i])")
                lines.append("        {")
                lines.append(f"            {res['struct']}.{res['name']}[i]->Unmap(0, nullptr);")
                lines.append(f"            {res['struct']}.{res['name']}[i]->Release();")
                lines.append(f"            {res['struct']}.{res['name']}[i] = nullptr;")
                if res['struct'] == 'graphics_resources':
                    lines.append(f"            graphics_resources.m_pCbvDataBegin[i] = nullptr;")
                lines.append("        }")
                lines.append("    }")
            else:
                lines.append(f"    if ({res['struct']}.{res['name']})")
                lines.append("    {")
                lines.append(f"        {res['struct']}.{res['name']}->Unmap(0, nullptr);")
                lines.append(f"        {res['struct']}.{res['name']}->Release();")
                lines.append(f"        {res['struct']}.{res['name']} = nullptr;")
                lines.append("    }")
        lines.append("")
    
    # Generate cleanup for other COM resources
    other_resources = [r for r in resources if 'constantBuffer' not in r['name'].lower()]
    
    current_category = None
    for res in other_resources:
        # Add category comments
        category = get_category_comment(res['name'])
        if category != current_category:
            if current_category is not None:
                lines.append("")
            lines.append(f"    // {category}")
            current_category = category
        
        if res['is_array']:
            arr_sz = res['array_size'] if res['array_size'] else 'g_FrameCount'
            # Special case: pipeline_dx12.m_pipelineStates[4] has fixed size 4
            if res['struct'] == 'pipeline_dx12' and res['name'] == 'm_pipelineStates':
                arr_sz = '4'
            lines.append(f"    for (UINT i = 0; i < {arr_sz}; i++)")
            lines.append("    {")
            lines.append(f"        if ({res['struct']}.{res['name']}[i])")
            lines.append("        {")
            lines.append(f"            {res['struct']}.{res['name']}[i]->Release();")
            lines.append(f"            {res['struct']}.{res['name']}[i] = nullptr;")
            lines.append("        }")
            lines.append("    }")
        else:
            lines.append(f"    if ({res['struct']}.{res['name']})")
            lines.append("    {")
            lines.append(f"        {res['struct']}.{res['name']}->Release();")
            lines.append(f"        {res['struct']}.{res['name']} = nullptr;")
            lines.append("    }")
    
    lines.append("")
    lines.append("    // Close fence event handle")
    lines.append("    if (sync_state.m_fenceEvent)")
    lines.append("    {")
    lines.append("        CloseHandle(sync_state.m_fenceEvent);")
    lines.append("        sync_state.m_fenceEvent = nullptr;")
    lines.append("    }")
    lines.append("}")
    
    return '\n'.join(lines)

# ----------------------------------------------------------------------
# Main generator function (for CLI and unified driver)
# ----------------------------------------------------------------------
def generate_cleanup(
    input_path: Path = Path("src/renderer_dx12.cpp"),
    output_path: Path = Path("src/generated/OnDestroy_generated.cpp"),
    force: bool = False
) -> bool:
    """Parse header and write cleanup code. Returns True if file written/updated."""
    if not input_path.exists():
        common.log_error(f"Header not found: {input_path}")
        return False

    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()

    resources = parse_dx12_resources(content)
    common.log_info(f"Found {len(resources)} COM resources")
    
    # Optional verbose listing
    if common._USE_COLOR:  # cheap way to detect verbose? We'll just log at info level.
        for res in resources:
            arr = f"[{res['array_size']}]" if res['is_array'] else ""
            common.log_info(f"  - {res['struct']}.{res['name']}{arr} ({res['type']})")

    # Sort by priority
    resources.sort(key=get_priority)

    # Generate code
    code = generate_cleanup_code(resources)

    # Prepend standard header
    header = common.make_header(tool_name="meta_ondestroy.py", comment="GENERATED ONDESTROY")
    full_content = header + code

    # Write atomically
    return common.write_file_if_changed(output_path, full_content)

# ----------------------------------------------------------------------
# Convenience wrapper for unified driver (uses default paths)
# ----------------------------------------------------------------------
def generate(force: bool = False) -> bool:
    return generate_cleanup(force=force)

# ----------------------------------------------------------------------
# CLI entry point
# ----------------------------------------------------------------------
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description="Generate OnDestroy_generated.cpp")
    common.add_common_args(parser)
    parser.add_argument('--input', '-i', type=Path,
                        default=Path("src/renderer_dx12.cpp"),
                        help="Input header (default: src/renderer_dx12.cpp)")
    parser.add_argument('--output', '-o', type=Path,
                        default=Path("src/generated/OnDestroy_generated.cpp"),
                        help="Output path (default: src/generated/OnDestroy_generated.cpp)")
    args = parser.parse_args()

    success = generate_cleanup(args.input, args.output, force=args.force)
    sys.exit(0 if success else 1)