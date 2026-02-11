import re
from pathlib import Path

def parse_dx12_resources(header_path):
    """Parse the header file and extract D3D12 resource declarations."""
    
    with open(header_path, 'r') as f:
        content = f.read()
    
    resources = []
    
    # Pattern to match COM pointer declarations in structs
    # Matches: ID3D12Something *name or ID3D12Something *name[N]
    pattern = r'(ID3D\w+|IDXGISwapChain\d*)\s*\*\s*(\w+)(\[[^\]]*\])?\s*(=\s*[^;]*)?;'
    
    # Find all struct definitions with their names
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
                    'is_array': False
                })
    
    return resources

def determine_cleanup_order(resources):
    """
    Determine the correct cleanup order based on D3D12 dependency rules.
    Generally: child resources before parents, in reverse creation order.
    """
    
    # Define priority tiers (lower number = release earlier)
    priority_map = {
        # Release per-frame resources first
        'constantBuffer': 1,
        'm_constantBuffer': 1,
        
        # Release other graphics resources
        'texture': 2,
        'm_texture': 2,
        'vertexBuffer': 2,
        'm_vertexBuffer': 2,
        'depthStencil': 2,
        'm_depthStencil': 2,
        
        # Release pipeline state objects
        'rootSignature': 3,
        'm_rootSignature': 3,
        'pipelineState': 3,
        'm_pipelineState': 3,
        'commandList': 3,
        'm_commandList': 3,
        
        # Release per-frame command objects
        'commandAllocators': 4,
        'm_commandAllocators': 4,
        'renderTargets': 4,
        'm_renderTargets': 4,
        
        # Release descriptor heaps
        'dsvHeap': 5,
        'm_dsvHeap': 5,
        'mainHeap': 5,
        'm_mainHeap': 5,
        'rtvHeap': 5,
        'm_rtvHeap': 5,
        
        # Release swap chain
        'swapChain': 6,
        'm_swapChain': 6,
        
        # Release command queue
        'commandQueue': 7,
        'm_commandQueue': 7,
        
        # Release device last
        'device': 8,
        'm_device': 8,
        
        # Sync objects (fence is COM object too!)
        'fence': 0,
        'm_fence': 0,
    }
    
    # Sort resources by priority
    def get_priority(resource):
        name = resource['name']
        return priority_map.get(name, 999)  # Unknown resources go last
    
    return sorted(resources, key=get_priority)

def generate_cleanup_code(resources):
    """Generate C++ cleanup code."""
    
    lines = []
    lines.append("#pragma once")
    lines.append("#include \"renderer_dx12.h\"")
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
                lines.append(f"    for (UINT i = 0; i < {res['array_size']}; i++)")
                lines.append("    {")
                lines.append(f"        if ({res['struct']}.{res['name']}[i])")
                lines.append("        {")
                lines.append(f"            {res['struct']}.{res['name']}[i]->Unmap(0, nullptr);")
                lines.append(f"            {res['struct']}.{res['name']}[i]->Release();")
                lines.append(f"            {res['struct']}.{res['name']}[i] = nullptr;")
                # Only add this line if the struct is graphics_resources
                if res['struct'] == 'graphics_resources':
                    lines.append(f"            {res['struct']}.m_pCbvDataBegin[i] = nullptr;")
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
            lines.append(f"    for (UINT i = 0; i < {res['array_size']}; i++)")
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

def get_category_comment(name):
    """Get a descriptive comment for the resource category."""
    # Normalize name (remove m_ prefix if present)
    clean_name = name[2:] if name.startswith('m_') else name
    
    if clean_name in ['fence']:
        return "Release sync objects"
    elif clean_name in ['texture', 'vertexBuffer', 'indexBuffer', 'depthStencil']:
        return "Release graphics resources"
    elif clean_name in ['rootSignature', 'pipelineState', 'commandList']:
        return "Release pipeline objects"
    elif clean_name in ['commandAllocators', 'renderTargets']:
        return "Release per-frame resources"
    elif clean_name in ['dsvHeap', 'mainHeap', 'rtvHeap']:
        return "Release descriptor heaps"
    elif clean_name == 'swapChain':
        return "Release swap chain"
    elif clean_name == 'commandQueue':
        return "Release command queue"
    elif clean_name == 'device':
        return "Release device (last)"
    else:
        return "Release other resources"

def main():
    # Path to your header file
    header_path = Path("src/renderer_dx12.h")
    
    # Try both forward and backward slashes
    if not header_path.exists():
        header_path = Path("src\\renderer_dx12.h")
    
    if not header_path.exists():
        # Try current directory
        header_path = Path("renderer_dx12.h")
    
    if not header_path.exists():
        print(f"Error: renderer_dx12.h not found!")
        print("Tried:")
        print("  - src/renderer_dx12.h")
        print("  - src\\renderer_dx12.h")
        print("  - renderer_dx12.h")
        return
    
    print(f"Parsing header file: {header_path}")
    resources = parse_dx12_resources(header_path)
    
    print(f"\nFound {len(resources)} COM resources:")
    for res in resources:
        array_info = f"[{res['array_size']}]" if res['is_array'] else ""
        print(f"  - {res['struct']}.{res['name']}{array_info} ({res['type']})")
    
    print("\nDetermining cleanup order...")
    ordered_resources = determine_cleanup_order(resources)
    
    print("\nGenerating cleanup code...")
    cleanup_code = generate_cleanup_code(ordered_resources)
    
    # Write to file
    output_dir = header_path.parent if header_path.parent.name == 'src' else Path('src')
    if not output_dir.exists():
        output_dir = Path('.')
    
    output_path = output_dir / "generated\\OnDestroy_generated.cpp"
    with open(output_path, 'w') as f:
        f.write(cleanup_code)
    
    print(f"\n✓ Generated cleanup code written to: {output_path}")    
    # print("\nPreview:")
    # print("=" * 80)
    # print(cleanup_code)
    # print("=" * 80)

if __name__ == "__main__":
    main()