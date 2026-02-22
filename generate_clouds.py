#!/usr/bin/env python3
"""
Generate a cloud texture as an equirectangular map using 3D simplex noise.
The output is a PNG with alpha (white clouds, transparency).
Includes progress indication.
"""

import numpy as np
from PIL import Image
import opensimplex
import math
import argparse
import sys

def spherical_noise(lon, lat, scale=5.0, octaves=4, persistence=0.5):
    """
    Compute 3D simplex noise for a given longitude (lon) and latitude (lat)
    on a unit sphere. Returns a value in the range [-1, 1].
    """
    # Convert to Cartesian direction vector
    cos_lat = math.cos(lat)
    x = cos_lat * math.cos(lon)
    y = math.sin(lat)
    z = cos_lat * math.sin(lon)

    value = 0.0
    amplitude = 1.0
    frequency = scale
    for _ in range(octaves):
        # opensimplex.noise3 returns values roughly in [-1, 1]
        n = opensimplex.noise3(x * frequency, y * frequency, z * frequency)
        value += n * amplitude
        amplitude *= persistence
        frequency *= 2.0
    return value

def generate_cloud_texture(width, height, scale=4.0, octaves=5,
                           persistence=0.6, coverage=0.4, sharpness=2.0):
    """
    Generate an RGBA image (white clouds with alpha) using 3D noise.
    - width, height: output image dimensions (equirectangular).
    - scale: base frequency of the noise (higher = smaller clouds).
    - octaves: fractal octaves for detail.
    - persistence: amplitude multiplier per octave.
    - coverage: threshold (0 = all clouds, 1 = no clouds).
    - sharpness: contrast exponent for alpha.
    """
    img = np.zeros((height, width, 4), dtype=np.uint8)
    total_rows = height
    next_percent = 10
    for j in range(height):
        # Progress report
        percent = (j + 1) * 100 // total_rows
        if percent >= next_percent:
            print(f"Progress: {percent}%", flush=True)
            next_percent += 10

        lat = (j / (height - 1) - 0.5) * math.pi   # [-π/2, π/2]
        for i in range(width):
            lon = (i / (width - 1)) * 2.0 * math.pi - math.pi   # [-π, π]

            n = spherical_noise(lon, lat, scale, octaves, persistence)

            # Map noise from [-1,1] to [0,1]
            n = (n + 1.0) * 0.5

            # Apply coverage threshold and sharpen
            n = max(0.0, n - coverage) / (1.0 - coverage)
            n = math.pow(n, sharpness)
            n = min(1.0, n)

            alpha = int(n * 255)
            # White clouds, full RGB (can be tinted later if needed)
            img[j, i] = [255, 255, 255, alpha]

    return img

def main():
    parser = argparse.ArgumentParser(description="Generate equirectangular cloud texture using 3D noise.")
    parser.add_argument("--width", type=int, default=2048, help="Output width")
    parser.add_argument("--height", type=int, default=1024, help="Output height")
    parser.add_argument("--scale", type=float, default=4.0, help="Noise scale (smaller = larger features)")
    parser.add_argument("--octaves", type=int, default=5, help="Number of fractal octaves")
    parser.add_argument("--persistence", type=float, default=0.6, help="Persistence per octave")
    parser.add_argument("--coverage", type=float, default=0.4, help="Cloud coverage (0 = full, 1 = none)")
    parser.add_argument("--sharpness", type=float, default=2.0, help="Alpha contrast exponent")
    parser.add_argument("--output", "-o", default="clouds.png", help="Output PNG filename")
    args = parser.parse_args()

    print(f"Generating {args.width}x{args.height} cloud texture...")
    img = generate_cloud_texture(args.width, args.height,
                                 scale=args.scale,
                                 octaves=args.octaves,
                                 persistence=args.persistence,
                                 coverage=args.coverage,
                                 sharpness=args.sharpness)
    print("Saving image...")
    Image.fromarray(img, 'RGBA').save(args.output)
    print(f"Saved to {args.output}")

if __name__ == "__main__":
    main()