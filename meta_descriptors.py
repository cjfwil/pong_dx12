#!/usr/bin/env python3
"""
meta_descriptors.py - Generate descriptor layout for C++ and HLSL.
Extracts constants from renderer_dx12.cpp and produces descriptor_layout.h.
"""

import re
import sys
from pathlib import Path

try:
    import common
except ImportError:
    sys.path.insert(0, str(Path(__file__).parent))
    import common

def extract_constant(content, name):
    """Extract integer value of a #define or static constexpr."""
    pattern_def = rf'#define\s+{name}\s+(\d+)'
    m = re.search(pattern_def, content)
    if m:
        return int(m.group(1))
    pattern_constexpr = rf'static\s+constexpr\s+UINT\s+{name}\s*=\s*(\d+)\s*;'
    m = re.search(pattern_constexpr, content)
    if m:
        return int(m.group(1))
    pattern_const = rf'const\s+UINT\s+{name}\s*=\s*(\d+)\s*;'
    m = re.search(pattern_const, content)
    if m:
        return int(m.group(1))
    return None

def generate_descriptor_header(input_path, output_path, force=False):
    if not input_path.exists():
        common.log_error(f"Input file not found: {input_path}")
        return False

    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()

    frame_count = extract_constant(content, 'g_FrameCount')
    max_heightmaps = extract_constant(content, 'MAX_HEIGHTMAP_TEXTURES')
    max_sky = extract_constant(content, 'MAX_SKY_TEXTURES')
    max_models = extract_constant(content, 'MAX_LOADED_MODELS')

    if None in (frame_count, max_heightmaps, max_sky, max_models):
        missing = []
        if frame_count is None: missing.append('g_FrameCount')
        if max_heightmaps is None: missing.append('MAX_HEIGHTMAP_TEXTURES')
        if max_sky is None: missing.append('MAX_SKY_TEXTURES')
        if max_models is None: missing.append('MAX_LOADED_MODELS')
        common.log_error(f"Could not find constants: {', '.join(missing)}")
        return False

    common.log_info(f"Found: g_FrameCount={frame_count}, MAX_HEIGHTMAP_TEXTURES={max_heightmaps}, "
                    f"MAX_SKY_TEXTURES={max_sky}, MAX_LOADED_MODELS={max_models}")

    # Compute register bases
    heightmap_reg_base = 1
    sky_reg_base = heightmap_reg_base + max_heightmaps
    albedo_reg_base = sky_reg_base + max_sky

    # Compute descriptor heap indices (C++ only)
    per_frame_start = 0
    per_scene_cbv = frame_count
    texture_srv = per_scene_cbv + 1
    heightmap_srv = texture_srv + 1
    sky_srv = heightmap_srv + max_heightmaps
    model_albedo_srv = sky_srv + max_sky
    num_descriptors = model_albedo_srv + max_models

    header = common.make_header("meta_descriptors.py", "DESCRIPTOR LAYOUT")
    content = f"""{header}
#pragma once

// ----------------------------------------------------------------------------
// Constants (shared between C++ and HLSL)
// ----------------------------------------------------------------------------
#define MAX_HEIGHTMAP_TEXTURES {max_heightmaps}
#define MAX_SKY_TEXTURES        {max_sky}
#define MAX_LOADED_MODELS       {max_models}

#ifdef __cplusplus
// ----------------------------------------------------------------------------
// C++ specific: register bases and descriptor heap indices
// ----------------------------------------------------------------------------
namespace RegisterLayout {{
    constexpr UINT HEIGHTMAP_REGISTER_BASE = {heightmap_reg_base};
    constexpr UINT HEIGHTMAP_COUNT          = MAX_HEIGHTMAP_TEXTURES;
    constexpr UINT SKY_REGISTER_BASE        = HEIGHTMAP_REGISTER_BASE + HEIGHTMAP_COUNT;
    constexpr UINT SKY_COUNT                 = MAX_SKY_TEXTURES;
    constexpr UINT ALBEDO_REGISTER_BASE      = SKY_REGISTER_BASE + SKY_COUNT;
    constexpr UINT ALBEDO_COUNT               = MAX_LOADED_MODELS;
}}

namespace DescriptorIndices {{
    constexpr UINT PER_FRAME_CBV_START = {per_frame_start};
    constexpr UINT PER_SCENE_CBV       = {per_scene_cbv};
    constexpr UINT TEXTURE_SRV          = {texture_srv};
    constexpr UINT HEIGHTMAP_SRV        = {heightmap_srv};
    constexpr UINT SKY_SRV              = {sky_srv};
    constexpr UINT MODEL_ALBEDO_SRV     = {model_albedo_srv};
    constexpr UINT NUM_DESCRIPTORS      = {num_descriptors};
}}
#else // HLSL
// ----------------------------------------------------------------------------
// HLSL specific: register bases as preprocessor macros (literal values)
// ----------------------------------------------------------------------------
#define HEIGHTMAP_REGISTER_BASE t{heightmap_reg_base}
#define SKY_REGISTER_BASE       t{sky_reg_base}
#define ALBEDO_REGISTER_BASE    t{albedo_reg_base}
#endif
"""

    return common.write_file_if_changed(output_path, content)

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description="Generate descriptor layout header")
    common.add_common_args(parser)
    parser.add_argument('--input', '-i', type=Path, default=Path("src/renderer_dx12.cpp"),
                        help="Input renderer file")
    parser.add_argument('--output', '-o', type=Path, default=Path("src/generated/descriptor_layout.h"),
                        help="Output header")
    args = parser.parse_args()

    success = generate_descriptor_header(args.input, args.output, force=args.force)
    sys.exit(0 if success else 1)