#!/usr/bin/env python3
"""
meta_mesh.py - Generate mesh_data.h with primitive geometry + NORMALS.

Now outputs:
    position (float3)
    normal   (float3)   ← NEW
    uv       (float2)

All generators compute correct outward normals.
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
# Geometry definitions - Vertex now includes normal
# ----------------------------------------------------------------------
Vertex = namedtuple('Vertex', ['position', 'normal', 'uv'])
Vec3 = namedtuple('Vec3', ['x', 'y', 'z'])
Vec2 = namedtuple('Vec2', ['x', 'y'])

def normalize(v):
    """Return normalized Vec3."""
    length = math.sqrt(v.x*v.x + v.y*v.y + v.z*v.z)
    if length == 0:
        return Vec3(0,0,0)
    return Vec3(v.x/length, v.y/length, v.z/length)

def cross(a, b):
    """Cross product of two Vec3."""
    return Vec3(
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    )

def sub(a, b):
    """a - b."""
    return Vec3(a.x-b.x, a.y-b.y, a.z-b.z)

# ----------------------------------------------------------------------
# Cube
# ----------------------------------------------------------------------
def gen_cube():
    """24 vertices, 36 indices. Hard edges - one normal per face."""
    vertices = []
    indices = []
    s = 0.5
    corners = [
        Vec3(-s, -s, -s), Vec3( s, -s, -s), Vec3( s, -s,  s), Vec3(-s, -s,  s),
        Vec3(-s,  s, -s), Vec3( s,  s, -s), Vec3( s,  s,  s), Vec3(-s,  s,  s)
    ]
    uvs = [Vec2(0,1), Vec2(1,1), Vec2(1,0), Vec2(0,0)]

    # Face corner indices (order matters for CCW winding)
    faces = [
        ([0,1,2,3], uvs),   # bottom (-Y)
        ([7,6,5,4], uvs),   # top    (+Y)
        ([2,6,7,3], uvs),   # right? we compute normals dynamically to be safe
        ([0,4,5,1], uvs),   # left
        ([3,7,4,0], uvs),   # front
        ([6,2,1,5], uvs)    # back
    ]

    base = 0
    for corner_idxs, face_uvs in faces:
        # Compute face normal from first three corners (CCW)
        p0 = corners[corner_idxs[0]]
        p1 = corners[corner_idxs[1]]
        p2 = corners[corner_idxs[2]]
        normal = normalize(cross(sub(p1, p0), sub(p2, p0)))
        for i in range(4):
            vertices.append(Vertex(
                position = corners[corner_idxs[i]],
                normal   = normal,
                uv       = face_uvs[i]
            ))
        indices.extend([base+0, base+1, base+2, base+0, base+2, base+3])
        base += 4
    return vertices, indices

# ----------------------------------------------------------------------
# Cylinder
# ----------------------------------------------------------------------
def gen_cylinder(slices):
    """
    Cylinder radius 0.5, height 1.0.
    - Side: smooth normals (radial)
    - Caps: flat normals (0,±1,0)
    """
    vertices = []
    indices = []

    radius = 0.5
    half_h = 0.5
    slices = max(3, slices)

    # --- centers (caps) ---
    bottom_center_idx = len(vertices)
    vertices.append(Vertex(
        Vec3(0, -half_h, 0),
        Vec3(0, -1, 0),          # normal points down
        Vec2(0.5, 0.5)
    ))
    top_center_idx = len(vertices)
    vertices.append(Vertex(
        Vec3(0,  half_h, 0),
        Vec3(0,  1, 0),          # normal points up
        Vec2(0.5, 0.5)
    ))

    # --- SIDE VERTICES (smooth normals) ---
    side_start = len(vertices)
    for i in range(slices + 1):
        u = i / slices
        angle = i * (2.0 * math.pi / slices)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        # radial normal (pointing outward)
        n = normalize(Vec3(x, 0, z))

        # bottom ring
        vertices.append(Vertex(
            Vec3(x, -half_h, z),
            n,
            Vec2(u, 0.0)
        ))
        # top ring
        vertices.append(Vertex(
            Vec3(x,  half_h, z),
            n,
            Vec2(u, 1.0)
        ))

    # --- CAP VERTICES (flat normals, planar UVs) ---
    bottom_cap_start = len(vertices)
    for i in range(slices + 1):
        angle = i * (2.0 * math.pi / slices)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        uv = Vec2((x / radius + 1) * 0.5, (z / radius + 1) * 0.5)
        vertices.append(Vertex(
            Vec3(x, -half_h, z),
            Vec3(0, -1, 0),
            uv
        ))

    top_cap_start = len(vertices)
    for i in range(slices + 1):
        angle = i * (2.0 * math.pi / slices)
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)
        uv = Vec2((x / radius + 1) * 0.5, (z / radius + 1) * 0.5)
        vertices.append(Vertex(
            Vec3(x,  half_h, z),
            Vec3(0,  1, 0),
            uv
        ))

    # --- INDICES (same as before) ---
    # side quads
    for i in range(slices):
        b0 = side_start + i * 2
        b1 = side_start + (i + 1) * 2
        t0 = b0 + 1
        t1 = b1 + 1
        indices.extend([b0, t1, b1, b0, t0, t1])

    # bottom cap (triangle fan)
    for i in range(slices):
        c0 = bottom_cap_start + i
        c1 = bottom_cap_start + i + 1
        indices.extend([bottom_center_idx, c0, c1])

    # top cap (triangle fan)
    for i in range(slices):
        c0 = top_cap_start + i
        c1 = top_cap_start + i + 1
        indices.extend([top_center_idx, c1, c0])

    return vertices, indices

# ----------------------------------------------------------------------
# Triangular Prism
# ----------------------------------------------------------------------
def gen_prism():
    """Equilateral triangular prism. Hard edges - one normal per face."""
    vertices = []
    indices = []
    radius = 0.5
    half_h = 0.5
    angles = [math.pi/2, 7*math.pi/6, 11*math.pi/6]
    bottom_tri = [Vec3(radius*math.cos(a), -half_h, radius*math.sin(a)) for a in angles]
    top_tri    = [Vec3(radius*math.cos(a),  half_h, radius*math.sin(a)) for a in angles]
    bottom_uvs = [Vec2(0,0), Vec2(1,0), Vec2(0.5,1)]
    top_uvs    = [Vec2(0,0), Vec2(1,0), Vec2(0.5,1)]   # planar mapping

    # --- bottom face (normal -Y) ---
    bottom_start = len(vertices)
    n_bottom = Vec3(0, -1, 0)
    for i in range(3):
        vertices.append(Vertex(bottom_tri[i], n_bottom, bottom_uvs[i]))
    indices.extend([bottom_start, bottom_start+1, bottom_start+2])

    # --- top face (normal +Y) ---
    top_start = len(vertices)
    n_top = Vec3(0, 1, 0)
    for i in range(3):
        vertices.append(Vertex(top_tri[i], n_top, top_uvs[i]))
    indices.extend([top_start, top_start+2, top_start+1])   # reverse winding

    # --- side faces (each is a quad, outward normal) ---
    for e in range(3):
        n = (e+1) % 3
        bl = bottom_tri[e]
        tl = top_tri[e]
        tr = top_tri[n]
        br = bottom_tri[n]

        # compute quad normal (cross product of (br-bl) and (tl-bl))
        edge1 = sub(br, bl)
        edge2 = sub(tl, bl)
        face_normal = normalize(cross(edge1, edge2))

        quad_start = len(vertices)
        vertices.append(Vertex(bl, face_normal, Vec2(0,0)))
        vertices.append(Vertex(tl, face_normal, Vec2(0,1)))
        vertices.append(Vertex(tr, face_normal, Vec2(1,1)))
        vertices.append(Vertex(br, face_normal, Vec2(1,0)))
        indices.extend([
            quad_start,   quad_start+1, quad_start+2,
            quad_start,   quad_start+2, quad_start+3
        ])

    return vertices, indices

# ----------------------------------------------------------------------
# Sphere (smooth normals = normalized position)
# ----------------------------------------------------------------------
def gen_sphere(slices, stacks):
    """UV sphere radius 0.5, smooth normals (outward)."""
    vertices = []
    indices = []
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
            pos = Vec3(x, y, z)
            # normal = normalized position (since sphere centered at origin)
            n = normalize(pos)
            vertices.append(Vertex(pos, n, Vec2(u, v)))

    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = a + 1
            c = (i + 1) * (slices + 1) + j
            d = c + 1
            indices.extend([a, d, b, a, c, d])
    return vertices, indices

# ----------------------------------------------------------------------
# Inverted Sphere (normals point inward)
# ----------------------------------------------------------------------
def gen_inverted_sphere(slices, stacks):
    """Same vertices as sphere, but reversed winding and inward normals."""
    vertices = []
    indices = []
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
            pos = Vec3(x, y, z)
            n = normalize(pos)
            n = Vec3(-n.x, -n.y, -n.z)   # inward
            vertices.append(Vertex(pos, n, Vec2(u, v)))

    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = a + 1
            c = (i + 1) * (slices + 1) + j
            d = c + 1
            indices.extend([a, b, d, a, d, c])   # reverse winding
    return vertices, indices

# ----------------------------------------------------------------------
# Primitive registry - order determines enum values
# ----------------------------------------------------------------------
PRIMITIVES = [
    ("cube",             gen_cube),
    ("cylinder",         gen_cylinder, 32),
    ("prism",            gen_prism),
    ("sphere",           gen_sphere, 64, 32),
    ("inverted_sphere",  gen_inverted_sphere, 64, 32),
]

# ----------------------------------------------------------------------
# Header generator - now writes position, normal, uv
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
    content += '#include "renderer_dx12.cpp"  // for Vertex\n\n'

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

    # Vertex/Index arrays - now includes normal
    for name, verts, idxs in primitives_data:
        array_name = ''.join(p.capitalize() for p in name.split('_'))
        vc = len(verts)
        ic = len(idxs)
        content += f"static const Vertex k{array_name}Vertices[{vc}] = {{\n"
        for v in verts:
            # position
            px, py, pz = v.position.x, v.position.y, v.position.z
            # normal
            nx, ny, nz = v.normal.x, v.normal.y, v.normal.z
            # uv
            ux, uy = v.uv.x, v.uv.y
            content += f"    {{{{ {px:.6f}f, {py:.6f}f, {pz:.6f}f }}, {{ {nx:.6f}f, {ny:.6f}f, {nz:.6f}f }}, {{ {ux:.6f}f, {uy:.6f}f }}}},\n"
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
    content += "// Lookup table - order matches PrimitiveType\n"
    content += "static const PrimitiveMeshData kPrimitiveMeshData[PRIMITIVE_COUNT] =\n{\n"
    for name, _, _ in primitives_data:
        array_name = ''.join(p.capitalize() for p in name.split('_'))
        content += f"    {{ k{array_name}Vertices, k{array_name}VertexCount, k{array_name}Indices, k{array_name}IndexCount }},\n"
    content += "};\n\n"

    # Display names
    content += "// Display names - order matches PrimitiveType\n"
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
    parser = argparse.ArgumentParser(description="Generate mesh_data.h with normals")
    common.add_common_args(parser)
    parser.add_argument('--output', '-o', type=Path,
                        default=Path("src/generated/mesh_data.h"),
                        help="Output path (default: src/generated/mesh_data.h)")
    args = parser.parse_args()

    success = generate_mesh_header(args.output, force=args.force)
    sys.exit(0 if success else 1)