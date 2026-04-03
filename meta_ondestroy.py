#!/usr/bin/env python3
"""
meta_ondestroy.py - Generate OnDestroy_generated.cpp.

Parses renderer_dx12.cpp, finds D3D12 COM pointers in structs:
  PipelineDX12, GraphicsResources, SyncState (members of g_engine)
orders them by dependency, emits cleanup code.
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

def parse_dx12_resources(content: str) -> list:
    resources = []
    structs = {
        'PipelineDX12': 'g_engine.pipeline_dx12',
        'GraphicsResources': 'g_engine.graphics_resources',
        'SyncState': 'g_engine.sync_state'
    }

    for struct_name, prefix in structs.items():
        # Find struct body using brace counting
        pattern = rf'struct\s+{struct_name}\s*\{{'
        match = re.search(pattern, content)
        if not match:
            common.log_warning(f"Could not find start of struct {struct_name}")
            continue
        start = match.end()
        brace_count = 1
        i = start
        while i < len(content) and brace_count > 0:
            if content[i] == '{':
                brace_count += 1
            elif content[i] == '}':
                brace_count -= 1
            i += 1
        if brace_count != 0:
            common.log_warning(f"Unmatched braces in struct {struct_name}")
            continue
        body = content[start:i-1]

        # Remove comments
        body = re.sub(r'//.*?$', '', body, flags=re.MULTILINE)
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.DOTALL)

        # Split into lines and parse each line
        lines = body.split('\n')
        for line in lines:
            line = line.strip()
            if not line:
                continue

            # Look for COM pointer declarations
            m = re.search(r'(ID3D\w+|IDXGISwapChain\d*)\s*\*\s*(\w+)', line)
            if not m:
                continue

            type_name = m.group(1)
            var_name = m.group(2)          # includes leading 'm_' if present

            # Check for array brackets after the variable name
            is_array = False
            array_size = None
            rest = line[line.find(var_name) + len(var_name):]
            if '[' in rest:
                is_array = True
                bracket_start = rest.find('[')
                bracket_end = rest.find(']', bracket_start)
                if bracket_end != -1:
                    array_size = rest[bracket_start+1:bracket_end].strip()
                    if not array_size:
                        array_size = 'g_FrameCount'   # fallback for empty brackets

            resources.append({
                'struct': prefix,
                'type': type_name,
                'name': var_name,
                'is_array': is_array,
                'array_size': array_size
            })

    return resources

# ----------------------------------------------------------------------
# Cleanup ordering - original priority map (unchanged)
# ----------------------------------------------------------------------
PRIORITY_MAP = {
    # Sync objects (release first)
    'm_fence': 0,
    # Constant buffers (need Unmap)
    'm_PerFrameConstantBuffer': 1,
    'm_PerSceneConstantBuffer': 1,
    # Graphics resources
    'm_defaultTexture': 2,
    'm_heightmapTexture': 2,
    'm_heightfieldVertexBuffer': 2,
    'm_heightfieldIndexBuffer': 2,
    'm_heightfieldVertexUpload': 2,
    'm_heightfieldIndexUpload': 2,
    'm_vertexBuffer': 2,
    'm_indexBuffer': 2,
    'm_depthStencil': 2,
    # Pipeline objects
    'm_rootSignature': 3,
    'm_pipelineStates': 3,
    'm_commandList': 3,
    'm_commandAllocators': 4,
    'm_renderTargets': 4,
    'm_msaaRenderTargets': 4,
    'm_msaaDepthStencil': 4,
    # Descriptor heaps
    'm_dsvHeap': 5,
    'm_mainHeap': 5,
    'm_rtvHeap': 5,
    'm_imguiHeap': 5,
    'm_msaaRtvHeap': 5,
    # Swap chain
    'm_swapChain': 6,
    # Command queue
    'm_commandQueue': 7,
    # Device (last)
    'm_device': 8,
    # Extra arrays
    'm_heightmapResources': 2,
    'm_skyResources': 2,
    'm_modelAlbedoTextures': 2,
}

def get_priority(resource: dict) -> int:
    name = resource['name']
    # Some array names may not be in map; default to high number
    return PRIORITY_MAP.get(name, 999)

def get_category_comment(name: str) -> str:
    """Return descriptive comment for a resource category."""
    categories = {
        'm_fence': 'Release sync objects',
        'm_PerFrameConstantBuffer': 'Unmap and release constant buffers',
        'm_PerSceneConstantBuffer': 'Unmap and release constant buffers',
        'm_defaultTexture': 'Release graphics resources',
        'm_heightmapTexture': 'Release graphics resources',
        'm_heightfieldVertexBuffer': 'Release graphics resources',
        'm_heightfieldIndexBuffer': 'Release graphics resources',
        'm_vertexBuffer': 'Release graphics resources',
        'm_indexBuffer': 'Release graphics resources',
        'm_depthStencil': 'Release graphics resources',
        'm_rootSignature': 'Release pipeline objects',
        'm_pipelineStates': 'Release pipeline objects',
        'm_commandList': 'Release pipeline objects',
        'm_commandAllocators': 'Release per‑frame resources',
        'm_renderTargets': 'Release per‑frame resources',
        'm_msaaRenderTargets': 'Release MSAA resources',
        'm_msaaDepthStencil': 'Release MSAA resources',
        'm_dsvHeap': 'Release descriptor heaps',
        'm_mainHeap': 'Release descriptor heaps',
        'm_rtvHeap': 'Release descriptor heaps',
        'm_imguiHeap': 'Release other resources',
        'm_msaaRtvHeap': 'Release other resources',
        'm_swapChain': 'Release swap chain',
        'm_commandQueue': 'Release command queue',
        'm_device': 'Release device (last)',
        'm_heightmapResources': 'Release texture arrays',
        'm_skyResources': 'Release texture arrays',
        'm_modelAlbedoTextures': 'Release texture arrays',
    }
    return categories.get(name, 'Release other resources')

# ----------------------------------------------------------------------
# Code generation - adapted for g_engine prefix
# ----------------------------------------------------------------------
def generate_cleanup_code(resources: list) -> str:
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
    lines.append("    WaitForAllFrames();")
    lines.append("")
    
    # Special case: release m_models array using its Release method
    lines.append("    // Release model resources")
    lines.append("    for (UINT i = 0; i < MAX_LOADED_MODELS; i++)")
    lines.append("    {")
    lines.append("        g_engine.graphics_resources.m_models[i].Release();")
    lines.append("    }")
    lines.append("")
    
    # Separate constant buffers for special handling (Unmap)
    cb_names = {'m_PerFrameConstantBuffer', 'm_PerSceneConstantBuffer'}
    cb_resources = [r for r in resources if r['name'] in cb_names]
    other_resources = [r for r in resources if r['name'] not in cb_names]
    
    # 1. Constant buffers (unmap then release)
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
                if res['name'] == 'm_PerFrameConstantBuffer':
                    lines.append(f"            {res['struct']}.m_pCbvDataBegin[i] = nullptr;")
                lines.append("        }")
                lines.append("    }")
            else:
                lines.append(f"    if ({res['struct']}.{res['name']})")
                lines.append("    {")
                lines.append(f"        {res['struct']}.{res['name']}->Unmap(0, nullptr);")
                lines.append(f"        {res['struct']}.{res['name']}->Release();")
                lines.append(f"        {res['struct']}.{res['name']} = nullptr;")
                if res['name'] == 'm_PerSceneConstantBuffer':
                    lines.append(f"        {res['struct']}.m_pPerSceneCbvDataBegin = nullptr;")
                lines.append("    }")
        lines.append("")
    
    # 2. Other resources, grouped by category
    current_category = None
    for res in sorted(other_resources, key=lambda r: (get_priority(r), r['name'])):
        # Special handling for the 3D pipeline states array
        if res['name'] == 'm_pipelineStates':
            lines.append("    // Release pipeline state objects")
            lines.append("    for (UINT tech = 0; tech < RENDER_COUNT; ++tech)")
            lines.append("    {")
            lines.append("        for (UINT blend = 0; blend < BLEND_COUNT; ++blend)")
            lines.append("        {")
            lines.append("            for (UINT msaa = 0; msaa < 4; ++msaa)")
            lines.append("            {")
            lines.append(f"                if ({res['struct']}.{res['name']}[tech][blend][msaa])")
            lines.append("                {")
            lines.append(f"                    {res['struct']}.{res['name']}[tech][blend][msaa]->Release();")
            lines.append(f"                    {res['struct']}.{res['name']}[tech][blend][msaa] = nullptr;")
            lines.append("                }")
            lines.append("            }")
            lines.append("        }")
            lines.append("    }")
            continue
        
        category = get_category_comment(res['name'])
        if category != current_category:
            if current_category is not None:
                lines.append("")
            lines.append(f"    // {category}")
            current_category = category
        
        if res['is_array']:
            arr_sz = res['array_size'] if res['array_size'] else 'g_FrameCount'
            # For m_wireframePSO, size is 4
            if res['name'] == 'm_wireframePSO':
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
    lines.append("    if (g_engine.sync_state.m_fenceEvent)")
    lines.append("    {")
    lines.append("        CloseHandle(g_engine.sync_state.m_fenceEvent);")
    lines.append("        g_engine.sync_state.m_fenceEvent = nullptr;")
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
    if not input_path.exists():
        common.log_error(f"Header not found: {input_path}")
        return False

    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()

    resources = parse_dx12_resources(content)
    common.log_info(f"Found {len(resources)} COM resources")
    
    # Optional verbose listing
    if common._USE_COLOR:
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

    return common.write_file_if_changed(output_path, full_content)

# ----------------------------------------------------------------------
# Convenience wrapper for unified driver
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