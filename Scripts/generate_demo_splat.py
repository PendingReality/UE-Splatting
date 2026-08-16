#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Generate the redistributable UESplatting demo fixture as a standard 3DGS PLY."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


SH_C0 = 0.28209479177387814
VERTEX_STRUCT = struct.Struct("<14f")


def clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return max(low, min(high, value))


def lerp_color(a: tuple[float, float, float], b: tuple[float, float, float], t: float) -> tuple[float, float, float]:
    return tuple(clamp(x + (y - x) * t) for x, y in zip(a, b))


def add_splat(
    vertices: list[tuple[float, ...]],
    ue_position_m: tuple[float, float, float],
    color: tuple[float, float, float],
    scale_m: float,
    opacity: float = 0.94,
) -> None:
    ue_x, ue_y, ue_z = ue_position_m

    # Inverse of UESplatting's PLY RDF -> UE transform.
    ply_x = ue_y
    ply_y = -ue_z
    ply_z = ue_x

    dc = tuple((clamp(channel) - 0.5) / SH_C0 for channel in color)
    opacity_logit = math.log(opacity / (1.0 - opacity))
    log_scale = math.log(scale_m)

    vertices.append(
        (
            ply_x,
            ply_y,
            ply_z,
            dc[0],
            dc[1],
            dc[2],
            opacity_logit,
            log_scale,
            log_scale,
            log_scale,
            1.0,
            0.0,
            0.0,
            0.0,
        )
    )


def add_fibonacci_sphere(vertices: list[tuple[float, ...]], count: int = 12000) -> None:
    center = (0.0, 0.0, 1.35)
    radius = 0.58
    golden_angle = math.pi * (3.0 - math.sqrt(5.0))

    for index in range(count):
        y = 1.0 - 2.0 * (index + 0.5) / count
        radial = math.sqrt(max(0.0, 1.0 - y * y))
        theta = index * golden_angle
        x = math.cos(theta) * radial
        z = math.sin(theta) * radial

        color_a = (0.03, 0.72, 0.96)
        color_b = (0.94, 0.20, 0.70)
        color = lerp_color(color_a, color_b, 0.5 + 0.5 * z)
        color = lerp_color(color, (0.95, 0.98, 1.0), max(0.0, y) * 0.22)

        add_splat(
            vertices,
            (center[0] + x * radius, center[1] + y * radius, center[2] + z * radius),
            color,
            0.018,
        )


def add_torus(
    vertices: list[tuple[float, ...]],
    axis_a: tuple[float, float, float],
    axis_b: tuple[float, float, float],
    normal: tuple[float, float, float],
    color_a: tuple[float, float, float],
    color_b: tuple[float, float, float],
    phase: float,
) -> None:
    center = (0.0, 0.0, 1.35)
    major_radius = 1.08
    minor_radius = 0.065
    major_segments = 160
    minor_segments = 36

    for major_index in range(major_segments):
        u = 2.0 * math.pi * major_index / major_segments + phase
        cu, su = math.cos(u), math.sin(u)
        radial = tuple(cu * a + su * b for a, b in zip(axis_a, axis_b))
        for minor_index in range(minor_segments):
            v = 2.0 * math.pi * minor_index / minor_segments
            cv, sv = math.cos(v), math.sin(v)
            position = tuple(
                center[component]
                + (major_radius + minor_radius * cv) * radial[component]
                + minor_radius * sv * normal[component]
                for component in range(3)
            )
            color = lerp_color(color_a, color_b, 0.5 + 0.5 * math.sin(u * 2.0 + v))
            add_splat(vertices, position, color, 0.017)


def add_pedestal(vertices: list[tuple[float, ...]]) -> None:
    radius = 1.42
    top_z = 0.43
    bottom_z = 0.05
    rings = 44
    angular_segments = 180

    for ring in range(rings):
        radial_t = math.sqrt((ring + 0.5) / rings)
        r = radius * radial_t
        count = max(24, int(angular_segments * radial_t))
        for index in range(count):
            theta = 2.0 * math.pi * index / count + ring * 0.19
            accent = 0.5 + 0.5 * math.sin(theta * 6.0 + radial_t * 10.0)
            color = lerp_color((0.055, 0.075, 0.12), (0.12, 0.22, 0.32), accent * 0.5)
            add_splat(vertices, (r * math.cos(theta), r * math.sin(theta), top_z), color, 0.024)

    vertical_segments = 18
    for z_index in range(vertical_segments):
        z = bottom_z + (top_z - bottom_z) * z_index / (vertical_segments - 1)
        for index in range(angular_segments):
            theta = 2.0 * math.pi * index / angular_segments
            band = 0.5 + 0.5 * math.sin(theta * 8.0 + z * 13.0)
            color = lerp_color((0.035, 0.045, 0.075), (0.08, 0.38, 0.48), band * 0.45)
            add_splat(vertices, (radius * math.cos(theta), radius * math.sin(theta), z), color, 0.022)


def build_vertices() -> list[tuple[float, ...]]:
    vertices: list[tuple[float, ...]] = []
    add_pedestal(vertices)
    add_fibonacci_sphere(vertices)
    add_torus(
        vertices,
        (1.0, 0.0, 0.0),
        (0.0, 1.0, 0.0),
        (0.0, 0.0, 1.0),
        (1.0, 0.68, 0.08),
        (1.0, 0.26, 0.10),
        0.0,
    )
    add_torus(
        vertices,
        (1.0, 0.0, 0.0),
        (0.0, 0.0, 1.0),
        (0.0, 1.0, 0.0),
        (0.12, 0.92, 0.78),
        (0.05, 0.46, 1.0),
        math.pi / 7.0,
    )
    add_torus(
        vertices,
        (0.0, 1.0, 0.0),
        (0.0, 0.0, 1.0),
        (1.0, 0.0, 0.0),
        (0.96, 0.18, 0.52),
        (0.54, 0.20, 1.0),
        -math.pi / 9.0,
    )
    return vertices


def write_ply(output_path: Path, vertices: list[tuple[float, ...]]) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    header = "\n".join(
        (
            "ply",
            "format binary_little_endian 1.0",
            "comment Generated by UESplatting Scripts/generate_demo_splat.py",
            f"element vertex {len(vertices)}",
            "property float x",
            "property float y",
            "property float z",
            "property float f_dc_0",
            "property float f_dc_1",
            "property float f_dc_2",
            "property float opacity",
            "property float scale_0",
            "property float scale_1",
            "property float scale_2",
            "property float rot_0",
            "property float rot_1",
            "property float rot_2",
            "property float rot_3",
            "end_header",
            "",
        )
    ).encode("ascii")

    with output_path.open("wb") as output:
        output.write(header)
        for vertex in vertices:
            output.write(VERTEX_STRUCT.pack(*vertex))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "samples" / "Data" / "UESplatting_Demo.ply",
    )
    args = parser.parse_args()

    vertices = build_vertices()
    write_ply(args.output, vertices)
    print(f"Wrote {len(vertices):,} splats to {args.output}")


if __name__ == "__main__":
    main()
