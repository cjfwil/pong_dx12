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
    """Generate 24 vertices (4 per face) and 36 indices for a cube."""
    vertices = []
    indices = []

    s = 0.5  # half‑size

    corners = [
        Vec3(-s, -s, -s),  # 0
        Vec3( s, -s, -s),  # 1
        Vec3( s, -s,  s),  # 2
        Vec3(-s, -s,  s),  # 3
        Vec3(-s,  s, -s),  # 4
        Vec3( s,  s, -s),  # 5
        Vec3( s,  s,  s),  # 6
        Vec3(-s,  s,  s),  # 7
    ]

    uvs = [
        Vec2(0, 1),  # bottom-left
        Vec2(1, 1),  # bottom-right
        Vec2(1, 0),  # top-right
        Vec2(0, 0),  # top-left
    ]

    faces = [
        # bottom (y = -s)
        ([3, 2, 1, 0], uvs),
        # top (y =  s)
        ([4, 5, 6, 7], uvs),
        # front (z =  s)
        ([7, 6, 2, 3], uvs),
        # back (z = -s)
        ([0, 1, 5, 4], uvs),
        # left (x = -s)
        ([4, 7, 3, 0], uvs),
        # right (x =  s)
        ([1, 2, 6, 5], uvs),
    ]

    base_index = 0
    for corner_idxs, face_uvs in faces:
        for i in range(4):
            pos = corners[corner_idxs[i]]
            uv = face_uvs[i]
            vertices.append(Vertex(pos, uv))

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
    Returns (vertices, indices).
    """
    vertices = []
    indices = []

    radius = 0.5
    half_height = 0.5
    slices = max(3, slices)  # at least 3

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

    # --- side quads ---
    for i in range(slices):
        next_i = (i + 1) % slices

        b0 = bottom_ring_start + i * 2      # bottom vertex this slice
        b1 = bottom_ring_start + next_i * 2 # bottom vertex next slice
        t0 = bottom_ring_start + i * 2 + 1  # top vertex this slice
        t1 = bottom_ring_start + next_i * 2 + 1 # top vertex next slice

        # triangle 1: (b0, b1, t1)
        indices.append(b0)
        indices.append(b1)
        indices.append(t1)
        # triangle 2: (b0, t1, t0)
        indices.append(b0)
        indices.append(t1)
        indices.append(t0)

    # --- bottom cap (triangle fan) ---
    # bottom center already at bottom_center_idx
    for i in range(slices):
        next_i = (i + 1) % slices
        b0 = bottom_ring_start + i * 2
        b1 = bottom_ring_start + next_i * 2

        # triangle (center, b1, b0) – clockwise when viewed from below (outside)
        indices.append(bottom_center_idx)
        indices.append(b1)
        indices.append(b0)

    # --- top cap (triangle fan) ---
    for i in range(slices):
        next_i = (i + 1) % slices
        t0 = bottom_ring_start + i * 2 + 1
        t1 = bottom_ring_start + next_i * 2 + 1

        # triangle (center, t0, t1) – counter‑clockwise when viewed from above (outside)
        indices.append(top_center_idx)
        indices.append(t0)
        indices.append(t1)

    return vertices, indices


def gen_prism():
    """
    Generate an equilateral triangular prism (radius=0.5, height=1.0).
    Returns (vertices, indices) with per‑face vertices for correct UVs.
    """
    vertices = []
    indices = []

    radius = 0.5
    half_height = 0.5

    # --- bottom face (y = -half_height) ---
    # vertices of an equilateral triangle, pointing up (top vertex at angle 90°)
    # angles: 90°, 210°, 330°
    bottom_tri_angles = [math.pi/2, 7*math.pi/6, 11*math.pi/6]
    bottom_tri_pos = []
    for a in bottom_tri_angles:
        x = radius * math.cos(a)
        z = radius * math.sin(a)
        bottom_tri_pos.append(Vec3(x, -half_height, z))

    # bottom face UVs – map triangle to a 0‑1 square-ish? We'll use a simple 0,0;1,0;0.5,1.
    bottom_uvs = [Vec2(0, 0), Vec2(1, 0), Vec2(0.5, 1)]

    bottom_start = len(vertices)
    for i in range(3):
        vertices.append(Vertex(bottom_tri_pos[i], bottom_uvs[i]))

    # indices for bottom triangle (0,1,2)
    indices.append(bottom_start + 0)
    indices.append(bottom_start + 1)
    indices.append(bottom_start + 2)

    # --- top face (y = half_height) ---
    top_tri_pos = []
    for a in bottom_tri_angles:
        x = radius * math.cos(a)
        z = radius * math.sin(a)
        top_tri_pos.append(Vec3(x, half_height, z))

    # top face UVs (same mapping)
    top_start = len(vertices)
    for i in range(3):
        vertices.append(Vertex(top_tri_pos[i], bottom_uvs[i]))

    # indices for top triangle (3,4,5) – note winding: should be CCW when viewed from above
    indices.append(top_start + 0)
    indices.append(top_start + 1)
    indices.append(top_start + 2)

    # --- side faces (3 quads) ---
    # for each edge, create a quad with proper UVs (0,0)-(1,0)-(1,1)-(0,1)
    for edge in range(3):
        next_edge = (edge + 1) % 3

        # bottom vertices: bottom_tri_pos[edge], bottom_tri_pos[next_edge]
        # top vertices:   top_tri_pos[edge], top_tri_pos[next_edge]

        # order: bottom_left, bottom_right, top_right, top_left
        bl = bottom_tri_pos[edge]
        br = bottom_tri_pos[next_edge]
        tr = top_tri_pos[next_edge]
        tl = top_tri_pos[edge]

        bl_uv = Vec2(0.0, 0.0)
        br_uv = Vec2(1.0, 0.0)
        tr_uv = Vec2(1.0, 1.0)
        tl_uv = Vec2(0.0, 1.0)

        quad_start = len(vertices)
        vertices.append(Vertex(bl, bl_uv))
        vertices.append(Vertex(br, br_uv))
        vertices.append(Vertex(tr, tr_uv))
        vertices.append(Vertex(tl, tl_uv))

        # two triangles: (bl, br, tr) and (bl, tr, tl)
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
    Returns (vertices, indices).
    """
    vertices = []
    indices = []

    radius = 0.5

    for i in range(stacks + 1):
        v = i / stacks
        phi = (1.0 - v) * math.pi          # 0 at north pole, pi at south pole
        y = radius * math.cos(phi)
        sin_phi = math.sin(phi)

        for j in range(slices + 1):
            u = j / slices
            theta = u * 2.0 * math.pi

            x = radius * sin_phi * math.cos(theta)
            z = radius * sin_phi * math.sin(theta)

            pos = Vec3(x, y, z)
            uv = Vec2(u, v)
            vertices.append(Vertex(pos, uv))

    # Indices: two triangles per quad
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
    ("cube",     gen_cube),
    ("cylinder", gen_cylinder, 12),   # slices=12
    ("prism",    gen_prism),
    ("sphere",   gen_sphere, 20, 12), # slices=20, stacks=12
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
        # Mesh data arrays for each primitive
        # ------------------------------------------------------------------
        for name, vertices, indices in primitives_data:
            array_name = name.capitalize()
            vert_count = len(vertices)
            idx_count = len(indices)

            f.write(f"static const Vertex k{array_name}Vertices[{vert_count}] = {{\n")
            for v in vertices:
                f.write(f"    {{{{ {v.position.x:.6f}f, {v.position.y:.6f}f, {v.position.z:.6f}f }}, {{ {v.uv.x:.6f}f, {v.uv.y:.6f}f }}}},\n")
            f.write("};\n\n")

            f.write(f"static const uint32_t k{array_name}Indices[{idx_count}] = {{\n    ")
            for i, idx in enumerate(indices):
                if i != 0 and i % 12 == 0:
                    f.write("\n    ")
                f.write(f"{idx}, ")
            f.write("\n};\n\n")

            f.write(f"static const UINT k{array_name}IndexCount = {idx_count};\n\n")

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