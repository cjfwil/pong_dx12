#!/usr/bin/env python3
"""
meta_mesh.py – Generate mesh_data.h with primitive geometry.

Parses PRIMITIVES list, runs each generator, writes vertex/index arrays,
enum, lookup table, and display names. Now uses common.py.
"""

import math
from collections import namedtuple
from pathlib import Path
import sys

# ----------------------------------------------------------------------
# Local imports
# ----------------------------------------------------------------------
try:
    import common
except ImportError:
    sys.path.insert(0, str(Path(__file__).parent))
    import common

# ----------------------------------------------------------------------
# Geometry definitions (unchanged from original meta_mesh.py)
# ----------------------------------------------------------------------
Vertex = namedtuple('Vertex', ['position', 'uv'])
Vec3 = namedtuple('Vec3', ['x', 'y', 'z'])
Vec2 = namedtuple('Vec2', ['x', 'y'])

def gen_cube():
    """24 vertices, 36 indices."""
    vertices = []
    indices = []
    s = 0.5
    corners = [
        Vec3(-s, -s, -s), Vec3( s, -s, -s), Vec3( s, -s,  s), Vec3(-s, -s,  s),
        Vec3(-s,  s, -s), Vec3( s,  s, -s), Vec3( s,  s,  s), Vec3(-s,  s,  s)
    ]
    uvs = [Vec2(0,1), Vec2(1,1), Vec2(1,0), Vec2(0,0)]
    faces = [
        ([0,1,2,3], uvs), ([7,6,5,4], uvs), ([2,6,7,3], uvs),
        ([0,4,5,1], uvs), ([3,7,4,0], uvs), ([6,2,1,5], uvs)
    ]
    base = 0
    for corner_idxs, face_uvs in faces:
        for i in range(4):
            vertices.append(Vertex(corners[corner_idxs[i]], face_uvs[i]))
        indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])
        base += 4
    return vertices, indices

def gen_cylinder(slices):
    """
    Generate a cylinder mesh (radius=0.5, height=1.0).
    - sides: correct UVs with seam duplicated (u from 0 to 1, v from 0 to 1)
    - caps: planar UV mapping (no radial distortion)
    - winding: counter‑clockwise when viewed from outside.
    """
    vertices = []
    indices = []

    radius = 0.5
    half_h = 0.5
    slices = max(3, slices)

    # --- centers (caps) ---
    bottom_center_idx = len(vertices)
    vertices.append(Vertex(Vec3(0, -half_h, 0), Vec2(0.5, 0.5)))  # bottom center
    top_center_idx = len(vertices)
    vertices.append(Vertex(Vec3(0,  half_h, 0), Vec2(0.5, 0.5)))  # top center

    # --- SIDE VERTICES -------------------------------------------------
    # Interleaved bottom/top vertices for the side wall.
    # We use slices+1 vertices per ring to create a clean seam at u=1.0.
    side_start = len(vertices)  # first side vertex (bottom of slice 0)

    for i in range(slices + 1):
        u = i / slices                     # 0.0 ... 1.0
        angle = i * (2.0 * math.pi / slices)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)

        # bottom vertex (y = -half_h)
        vertices.append(Vertex(Vec3(x, -half_h, z), Vec2(u, 0.0)))
        # top vertex    (y =  half_h)
        vertices.append(Vertex(Vec3(x,  half_h, z), Vec2(u, 1.0)))

    # --- CAP VERTICES (separate, with planar UVs) --------------------
    bottom_cap_start = len(vertices)
    for i in range(slices + 1):
        angle = i * (2.0 * math.pi / slices)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        uv = Vec2((x / radius + 1) * 0.5, (z / radius + 1) * 0.5)
        vertices.append(Vertex(Vec3(x, -half_h, z), uv))

    top_cap_start = len(vertices)
    for i in range(slices + 1):
        angle = i * (2.0 * math.pi / slices)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        uv = Vec2((x / radius + 1) * 0.5, (z / radius + 1) * 0.5)
        vertices.append(Vertex(Vec3(x,  half_h, z), uv))

    # --- SIDE QUADS ----------------------------------------------------
    # Two triangles per quad, both CCW when viewed from outside.
    for i in range(slices):
        # indices into the interleaved side vertices
        b0 = side_start + i * 2
        b1 = side_start + (i + 1) * 2
        t0 = b0 + 1
        t1 = b1 + 1

        # Triangle 1: (b0, b1, t1)
        indices.append(b0)
        indices.append(t1)
        indices.append(b1)

        # Triangle 2: (b0, t1, t0)
        indices.append(b0)
        indices.append(t0)
        indices.append(t1)

    # --- BOTTOM CAP (triangle fan) ------------------------------------
    # Outward normal = -y. Viewed from below, CCW = (center, b0, b1)
    for i in range(slices):
        c0 = bottom_cap_start + i
        c1 = bottom_cap_start + i + 1
        indices.append(bottom_center_idx)
        indices.append(c0)
        indices.append(c1)

    # --- TOP CAP (triangle fan) ---------------------------------------
    # Outward normal = +y. Viewed from above, CCW = (center, c1, c0)
    for i in range(slices):
        c0 = top_cap_start + i
        c1 = top_cap_start + i + 1
        indices.append(top_center_idx)
        indices.append(c1)
        indices.append(c0)

    return vertices, indices

def gen_prism():
    """Equilateral triangular prism."""
    vertices, indices = [], []
    radius, half_h = 0.5, 0.5
    angles = [math.pi/2, 7*math.pi/6, 11*math.pi/6]
    bottom_tri = [Vec3(radius*math.cos(a), -half_h, radius*math.sin(a)) for a in angles]
    top_tri    = [Vec3(radius*math.cos(a),  half_h, radius*math.sin(a)) for a in angles]
    bottom_uvs = [Vec2(0,0), Vec2(1,0), Vec2(0.5,1)]
    # bottom face
    bottom_start = len(vertices)
    for i in range(3):
        vertices.append(Vertex(bottom_tri[i], bottom_uvs[i]))
    indices.extend([bottom_start, bottom_start+1, bottom_start+2])
    # top face
    top_start = len(vertices)
    for i in range(3):
        vertices.append(Vertex(top_tri[i], bottom_uvs[i]))
    indices.extend([top_start, top_start+2, top_start+1])
    # side faces
    for e in range(3):
        n = (e+1)%3
        bl = bottom_tri[e]
        tl = top_tri[e]
        tr = top_tri[n]
        br = bottom_tri[n]
        quad_start = len(vertices)
        vertices.append(Vertex(bl, Vec2(0,0)))
        vertices.append(Vertex(tl, Vec2(0,1)))
        vertices.append(Vertex(tr, Vec2(1,1)))
        vertices.append(Vertex(br, Vec2(1,0)))
        indices.extend([quad_start, quad_start+1, quad_start+2,
                        quad_start, quad_start+2, quad_start+3])
    return vertices, indices

def gen_sphere(slices, stacks):
    """UV sphere radius=0.5."""
    vertices, indices = [], []
    radius = 0.5
    for i in range(stacks + 1):
        v = i / stacks
        phi = (1.0 - v) * math.pi
        y = radius * math.cos(phi)
        sin_phi = math.sin(phi)
        for j in range(slices + 1):
            u = j / slices
            theta = u * 2.0 * math.pi
            x = radius * sin_phi * math.cos(theta)
            z = radius * sin_phi * math.sin(theta)
            vertices.append(Vertex(Vec3(x, y, z), Vec2(u, v)))
    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = a + 1
            c = (i + 1) * (slices + 1) + j
            d = c + 1
            indices.extend([a, d, b, a, c, d])
    return vertices, indices

def gen_inverted_sphere(slices, stacks):
    """Same vertices, reversed winding (for skyboxes)."""
    vertices, indices = [], []
    radius = 0.5
    for i in range(stacks + 1):
        v = i / stacks
        phi = (1.0 - v) * math.pi
        y = radius * math.cos(phi)
        sin_phi = math.sin(phi)
        for j in range(slices + 1):
            u = j / slices
            theta = u * 2.0 * math.pi
            x = radius * sin_phi * math.cos(theta)
            z = radius * sin_phi * math.sin(theta)
            vertices.append(Vertex(Vec3(x, y, z), Vec2(u, v)))
    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = a + 1
            c = (i + 1) * (slices + 1) + j
            d = c + 1
            indices.extend([a, b, d, a, d, c])
    return vertices, indices

# ----------------------------------------------------------------------
# Primitive registry – order determines enum values
# ----------------------------------------------------------------------
PRIMITIVES = [
    ("cube",             gen_cube),
    ("cylinder",         gen_cylinder, 32),
    ("prism",            gen_prism),
    ("sphere",           gen_sphere, 64, 32),
    ("inverted_sphere",  gen_inverted_sphere, 64, 32),
]

# ----------------------------------------------------------------------
# Header generator
# ----------------------------------------------------------------------
def generate_mesh_header(
    output_path: Path = Path("src/generated/mesh_data.h"),
    force: bool = False
) -> bool:
    """Generate mesh_data.h from PRIMITIVES."""
    primitives_data = []

    for entry in PRIMITIVES:
        name = entry[0]
        func = entry[1]
        args = entry[2:] if len(entry) > 2 else []
        common.log_info(f"Generating {name}...")
        vertices, indices = func(*args)
        common.log_info(f"  {len(vertices)} vert, {len(indices)} idx")
        primitives_data.append((name, vertices, indices))

    # Build file content
    header = common.make_header(tool_name="meta_mesh.py", comment="GENERATED MESH DATA")
    content = header
    content += "#pragma once\n"
    content += '#include "renderer_dx12.h"  // for Vertex\n\n'

    # Enum
    content += "enum PrimitiveType\n{\n"
    for name, _, _ in primitives_data:
        content += f"    PRIMITIVE_{name.upper()},\n"
    content += "    PRIMITIVE_COUNT\n};\n\n"

    # Mesh data struct
    content += "struct PrimitiveMeshData\n{\n"
    content += "    const Vertex* vertices;\n"
    content += "    UINT vertexCount;\n"
    content += "    const uint32_t* indices;\n"
    content += "    UINT indexCount;\n};\n\n"

    # Vertex/Index arrays and constants
    for name, verts, idxs in primitives_data:
        array_name = ''.join(p.capitalize() for p in name.split('_'))
        vc = len(verts)
        ic = len(idxs)
        content += f"static const Vertex k{array_name}Vertices[{vc}] = {{\n"
        for v in verts:
            content += f"    {{{{ {v.position.x:.6f}f, {v.position.y:.6f}f, {v.position.z:.6f}f }}, {{ {v.uv.x:.6f}f, {v.uv.y:.6f}f }}}},\n"
        content += "};\n\n"
        content += f"static const UINT k{array_name}VertexCount = {vc};\n\n"
        content += f"static const uint32_t k{array_name}Indices[{ic}] = {{\n    "
        for i, idx in enumerate(idxs):
            if i != 0 and i % 12 == 0:
                content += "\n    "
            content += f"{idx}, "
        content += "\n};\n\n"
        content += f"static const UINT k{array_name}IndexCount = {ic};\n\n"

    # Lookup table
    content += "// Lookup table – order matches PrimitiveType\n"
    content += "static const PrimitiveMeshData kPrimitiveMeshData[PRIMITIVE_COUNT] =\n{\n"
    for name, _, _ in primitives_data:
        array_name = ''.join(p.capitalize() for p in name.split('_'))
        content += f"    {{ k{array_name}Vertices, k{array_name}VertexCount, k{array_name}Indices, k{array_name}IndexCount }},\n"
    content += "};\n\n"

    # Display names
    content += "// Display names – order matches PrimitiveType\n"
    content += 'static const char* g_primitiveNames[PRIMITIVE_COUNT] =\n{\n'
    for name, _, _ in primitives_data:
        display = ' '.join(p.capitalize() for p in name.split('_'))
        content += f'    "{display}",\n'
    content += "};\n\n"

    return common.write_file_if_changed(output_path, content)

# ----------------------------------------------------------------------
# CLI
# ----------------------------------------------------------------------
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description="Generate mesh_data.h")
    common.add_common_args(parser)
    parser.add_argument('--output', '-o', type=Path,
                        default=Path("src/generated/mesh_data.h"),
                        help="Output path (default: src/generated/mesh_data.h)")
    args = parser.parse_args()

    success = generate_mesh_header(args.output, force=args.force)
    sys.exit(0 if success else 1)