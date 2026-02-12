#!/usr/bin/env python3
"""
Generate static mesh data for greybox primitives.
Output: src/generated/mesh_data.h
"""

import os
import math
from pathlib import Path
from collections import namedtuple

# Simple vertex representation
Vertex = namedtuple('Vertex', ['position', 'uv'])
Vec3 = namedtuple('Vec3', ['x', 'y', 'z'])
Vec2 = namedtuple('Vec2', ['x', 'y'])

def gen_cube():
    """Generate 24 vertices (4 per face) and 36 indices for a cube.
       Winding: counter-clockwise when viewed from outside."""
    vertices = []
    indices = []

    s = 0.5  # half‑size

    corners = [
        Vec3(-s, -s, -s),  # 0 back  bottom left
        Vec3( s, -s, -s),  # 1 back  bottom right
        Vec3( s, -s,  s),  # 2 front bottom right
        Vec3(-s, -s,  s),  # 3 front bottom left
        Vec3(-s,  s, -s),  # 4 back  top left
        Vec3( s,  s, -s),  # 5 back  top right
        Vec3( s,  s,  s),  # 6 front top right
        Vec3(-s,  s,  s),  # 7 front top left
    ]

    uvs = [
        Vec2(0, 1),  # bottom-left
        Vec2(1, 1),  # bottom-right
        Vec2(1, 0),  # top-right
        Vec2(0, 0),  # top-left
    ]

    # Each face: vertex indices in COUNTER-CLOCKWISE order when viewed from OUTSIDE.
    faces = [
        # bottom (y = -s)
        ([0, 1, 2, 3], uvs),
        # top    (y =  s)
        ([7, 6, 5, 4], uvs),
        # front  (z =  s)
        ([2, 6, 7, 3], uvs),
        # back   (z = -s)
        ([0, 4, 5, 1], uvs),
        # left   (x = -s)
        ([3, 7, 4, 0], uvs),
        # right  (x =  s)
        ([6, 2, 1, 5], uvs),
    ]

    base_index = 0
    for corner_idxs, face_uvs in faces:
        for i in range(4):
            pos = corners[corner_idxs[i]]
            uv = face_uvs[i]
            vertices.append(Vertex(pos, uv))

        # Two triangles: (0,1,2) and (0,2,3) – both CCW from outside with our vertex order.
        indices.append(base_index + 0)
        indices.append(base_index + 1)
        indices.append(base_index + 2)
        indices.append(base_index + 0)
        indices.append(base_index + 2)
        indices.append(base_index + 3)

        base_index += 4

    return vertices, indices


def gen_cylinder(slices=12):
    """
    Generate a cylinder mesh (radius=0.5, height=1.0).
    Sides: quads, top/bottom: triangle fans.
    Winding: counter-clockwise when viewed from outside.
    """
    vertices = []
    indices = []

    radius = 0.5
    half_height = 0.5
    slices = max(3, slices)

    angle_step = 2.0 * math.pi / slices

    # --- bottom center ---
    bottom_center = Vec3(0, -half_height, 0)
    bottom_center_idx = len(vertices)
    vertices.append(Vertex(bottom_center, Vec2(0.5, 0.5)))

    # --- top center ---
    top_center = Vec3(0, half_height, 0)
    top_center_idx = len(vertices)
    vertices.append(Vertex(top_center, Vec2(0.5, 0.5)))

    # --- bottom ring & top ring vertices ---
    bottom_ring_start = len(vertices)
    top_ring_start = bottom_ring_start + slices

    for i in range(slices):
        angle = i * angle_step
        x = radius * math.cos(angle)
        z = radius * math.sin(angle)

        # bottom vertex (y = -half_height)
        bottom_pos = Vec3(x, -half_height, z)
        bottom_uv = Vec2(i / slices, 0.0)
        vertices.append(Vertex(bottom_pos, bottom_uv))

        # top vertex (y =  half_height)
        top_pos = Vec3(x, half_height, z)
        top_uv = Vec2(i / slices, 1.0)
        vertices.append(Vertex(top_pos, top_uv))

    # --- side quads (reverse winding to be CCW from outside) ---
    for i in range(slices):
        next_i = (i + 1) % slices

        b0 = bottom_ring_start + i * 2      # bottom this slice
        b1 = bottom_ring_start + next_i * 2 # bottom next slice
        t0 = bottom_ring_start + i * 2 + 1  # top this slice
        t1 = bottom_ring_start + next_i * 2 + 1 # top next slice

        # Triangle 1: (b0, t1, b1)
        indices.append(b0)
        indices.append(t1)
        indices.append(b1)
        # Triangle 2: (b0, t0, t1)
        indices.append(b0)
        indices.append(t0)
        indices.append(t1)

    # --- bottom cap (triangle fan) – reverse winding ---
    for i in range(slices):
        next_i = (i + 1) % slices
        b0 = bottom_ring_start + i * 2
        b1 = bottom_ring_start + next_i * 2
        indices.append(bottom_center_idx)
        indices.append(b0)
        indices.append(b1)

    # --- top cap (triangle fan) – reverse winding ---
    for i in range(slices):
        next_i = (i + 1) % slices
        t0 = bottom_ring_start + i * 2 + 1
        t1 = bottom_ring_start + next_i * 2 + 1
        indices.append(top_center_idx)
        indices.append(t1)
        indices.append(t0)

    return vertices, indices


def gen_prism():
    """
    Generate an equilateral triangular prism (radius=0.5, height=1.0).
    Returns (vertices, indices) with per‑face vertices for correct UVs.
    Winding: counter-clockwise when viewed from outside.
    """
    vertices = []
    indices = []

    radius = 0.5
    half_height = 0.5

    # --- bottom face (y = -half_height) ---
    bottom_tri_angles = [math.pi/2, 7*math.pi/6, 11*math.pi/6]
    bottom_tri_pos = []
    for a in bottom_tri_angles:
        x = radius * math.cos(a)
        z = radius * math.sin(a)
        bottom_tri_pos.append(Vec3(x, -half_height, z))

    bottom_uvs = [Vec2(0, 0), Vec2(1, 0), Vec2(0.5, 1)]

    bottom_start = len(vertices)
    for i in range(3):
        vertices.append(Vertex(bottom_tri_pos[i], bottom_uvs[i]))

    # bottom triangle – CCW from below
    indices.append(bottom_start + 0)
    indices.append(bottom_start + 1)
    indices.append(bottom_start + 2)

    # --- top face (y = half_height) ---
    top_tri_pos = []
    for a in bottom_tri_angles:
        x = radius * math.cos(a)
        z = radius * math.sin(a)
        top_tri_pos.append(Vec3(x, half_height, z))

    top_start = len(vertices)
    for i in range(3):
        vertices.append(Vertex(top_tri_pos[i], bottom_uvs[i]))

    # top triangle – CCW from above (reverse winding)
    indices.append(top_start + 0)
    indices.append(top_start + 2)
    indices.append(top_start + 1)

    # --- side faces (3 quads) ---
    for edge in range(3):
        next_edge = (edge + 1) % 3

        bl = bottom_tri_pos[edge]
        tl = top_tri_pos[edge]
        tr = top_tri_pos[next_edge]
        br = bottom_tri_pos[next_edge]

        bl_uv = Vec2(0.0, 0.0)
        tl_uv = Vec2(0.0, 1.0)
        tr_uv = Vec2(1.0, 1.0)
        br_uv = Vec2(1.0, 0.0)

        quad_start = len(vertices)
        vertices.append(Vertex(bl, bl_uv))
        vertices.append(Vertex(tl, tl_uv))
        vertices.append(Vertex(tr, tr_uv))
        vertices.append(Vertex(br, br_uv))

        # two triangles: (0,1,2) and (0,2,3) – both CCW
        indices.append(quad_start + 0)
        indices.append(quad_start + 1)
        indices.append(quad_start + 2)
        indices.append(quad_start + 0)
        indices.append(quad_start + 2)
        indices.append(quad_start + 3)

    return vertices, indices


def gen_sphere(slices=20, stacks=12):
    """
    Generate a UV sphere mesh (radius=0.5).
    Slices = number of meridians, stacks = number of latitude bands.
    Winding: counter-clockwise when viewed from outside.
    """
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
            vertices.append(Vertex(Vec3(x, y, z), Vec2(u, v)))

    # CCW from outside
    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = i * (slices + 1) + j + 1
            c = (i + 1) * (slices + 1) + j
            d = (i + 1) * (slices + 1) + j + 1

            indices.append(a)
            indices.append(d)
            indices.append(b)

            indices.append(a)
            indices.append(c)
            indices.append(d)

    return vertices, indices


def gen_inverted_sphere(slices=20, stacks=12):
    """
    Generate an inverted UV sphere mesh (radius=0.5).
    Identical vertices to gen_sphere, but with clockwise winding
    (front face from inside – ideal for skyboxes).
    """
    vertices = []
    indices = []

    radius = 0.5

    # --- identical vertex generation ---
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

    # --- inverted winding (CW from outside / CCW from inside) ---
    # Use the original unwound order: (a,b,d) and (a,d,c)
    for i in range(stacks):
        for j in range(slices):
            a = i * (slices + 1) + j
            b = i * (slices + 1) + j + 1
            c = (i + 1) * (slices + 1) + j
            d = (i + 1) * (slices + 1) + j + 1

            indices.append(a)
            indices.append(b)
            indices.append(d)

            indices.append(a)
            indices.append(d)
            indices.append(c)

    return vertices, indices


# -------------------------------------------------------------------------
# Primitive registry – add new primitives here (name, generator, *args)
# The enum order will follow the order in this list.
# -------------------------------------------------------------------------
PRIMITIVES = [
    ("cube",             gen_cube),
    ("cylinder",         gen_cylinder, 12),          # slices=12
    ("prism",            gen_prism),
    ("sphere",           gen_sphere, 20, 12),        # slices=20, stacks=12
    ("inverted_sphere",  gen_inverted_sphere, 20, 12), # inverted version
]


def write_header(path, primitives_data):
    """
    Write the generated mesh data and primitive enum to a C header file.
    primitives_data: list of (name, vertices, indices) for each generated primitive.
    """
    with open(path, 'w') as f:
        f.write("// Generated by meta_meshes.py - DO NOT EDIT\n")
        f.write("#pragma once\n")
        f.write('#include "renderer_dx12.h" // for Vertex\n\n')

        # ------------------------------------------------------------------
        # Primitive type enum – built dynamically from generated primitives
        # ------------------------------------------------------------------
        f.write("enum PrimitiveType\n")
        f.write("{\n")
        for name, _, _ in primitives_data:
            enum_name = f"PRIMITIVE_{name.upper()}"
            f.write(f"    {enum_name},\n")
        f.write("    PRIMITIVE_COUNT\n")
        f.write("};\n\n")

        # ------------------------------------------------------------------
        # PrimitiveMeshData struct definition
        # ------------------------------------------------------------------
        f.write("// Mesh data structure for a primitive\n")
        f.write("struct PrimitiveMeshData\n")
        f.write("{\n")
        f.write("    const Vertex* vertices;\n")
        f.write("    UINT vertexCount;\n")
        f.write("    const uint32_t* indices;\n")
        f.write("    UINT indexCount;\n")
        f.write("};\n\n")

        # ------------------------------------------------------------------
        # Mesh data arrays and count constants for each primitive
        # ------------------------------------------------------------------
        for name, vertices, indices in primitives_data:
            # Convert name like "inverted_sphere" -> "InvertedSphere"
            array_name = ''.join(part.capitalize() for part in name.split('_'))
            vert_count = len(vertices)
            idx_count = len(indices)

            # Vertex array
            f.write(f"static const Vertex k{array_name}Vertices[{vert_count}] = {{\n")
            for v in vertices:
                f.write(f"    {{{{ {v.position.x:.6f}f, {v.position.y:.6f}f, {v.position.z:.6f}f }}, {{ {v.uv.x:.6f}f, {v.uv.y:.6f}f }}}},\n")
            f.write("};\n\n")

            # Vertex count constant
            f.write(f"static const UINT k{array_name}VertexCount = {vert_count};\n\n")

            # Index array
            f.write(f"static const uint32_t k{array_name}Indices[{idx_count}] = {{\n    ")
            for i, idx in enumerate(indices):
                if i != 0 and i % 12 == 0:
                    f.write("\n    ")
                f.write(f"{idx}, ")
            f.write("\n};\n\n")

            # Index count constant
            f.write(f"static const UINT k{array_name}IndexCount = {idx_count};\n\n")

        # ------------------------------------------------------------------
        # Primitive lookup table (indexed by PrimitiveType)
        # ------------------------------------------------------------------
        f.write("// Lookup table for all primitives - order matches PrimitiveType\n")
        f.write("static const PrimitiveMeshData kPrimitiveMeshData[PRIMITIVE_COUNT] =\n")
        f.write("{\n")
        for name, vertices, indices in primitives_data:
            array_name = ''.join(part.capitalize() for part in name.split('_'))
            f.write(f"    {{ k{array_name}Vertices, k{array_name}VertexCount, k{array_name}Indices, k{array_name}IndexCount }},\n")
        f.write("};\n\n")

        # ------------------------------------------------------------------
        # Primitive display names (for UI, in same order as enum)
        # ------------------------------------------------------------------
        f.write("// Display names for primitives - order matches PrimitiveType\n")
        f.write("static const char* g_primitiveNames[PRIMITIVE_COUNT] = \n")
        f.write("{\n")
        for name, _, _ in primitives_data:
            # Convert "inverted_sphere" -> "Inverted Sphere"
            display_name = ' '.join(part.capitalize() for part in name.split('_'))
            f.write(f'    "{display_name}",\n')
        f.write("};\n\n")

    print(f"✓ Wrote {path}")


def main():
    out_dir = Path("src/generated")
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "mesh_data.h"

    primitives_data = []

    for entry in PRIMITIVES:
        name = entry[0]
        func = entry[1]

        # Extract arguments if any
        if len(entry) > 2:
            args = entry[2:]
        else:
            args = []

        print(f"Generating {name}...")
        vertices, indices = func(*args)
        print(f"  {len(vertices)} vertices, {len(indices)} indices")

        primitives_data.append((name, vertices, indices))

    write_header(out_path, primitives_data)
    print(f"✅ Mesh generation complete.")


if __name__ == "__main__":
    main()