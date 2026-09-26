#!/usr/bin/env python3
"""Generate the PWA icons in web/icons/.

A chainring cog on the dashboard's dark background, drawn procedurally so the
icons stay in the repo without a binary design tool. Run after changing the
palette in web/index.html:

    python3 tools/make_icons.py
"""

import math
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "web" / "icons"

BG = (15, 23, 42, 255)        # --bg-primary  #0f172a
ACCENT = (6, 182, 212, 255)   # --accent-cyan #06b6d4
SUPERSAMPLE = 4


def draw_cog(size: int, maskable: bool) -> Image.Image:
    side = size * SUPERSAMPLE
    image = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    if maskable:
        # Maskable icons are cropped to whatever shape the launcher wants, so
        # the background must bleed to the edges and the art must stay inside
        # the safe circle (80% of the width).
        draw.rectangle([0, 0, side, side], fill=BG)
        scale = 0.80
    else:
        radius = int(side * 0.22)
        draw.rounded_rectangle([0, 0, side - 1, side - 1], radius=radius, fill=BG)
        scale = 0.94

    centre = side / 2.0
    teeth = 10
    tooth_radius = side * 0.040 * scale
    ring_radius = side * 0.325 * scale
    tooth_orbit = side * 0.395 * scale
    hub_radius = side * 0.135 * scale

    for index in range(teeth):
        angle = 2.0 * math.pi * index / teeth
        x = centre + math.cos(angle) * tooth_orbit
        y = centre + math.sin(angle) * tooth_orbit
        draw.ellipse([x - tooth_radius, y - tooth_radius,
                      x + tooth_radius, y + tooth_radius], fill=ACCENT)

    draw.ellipse([centre - ring_radius, centre - ring_radius,
                  centre + ring_radius, centre + ring_radius], fill=ACCENT)
    draw.ellipse([centre - hub_radius, centre - hub_radius,
                  centre + hub_radius, centre + hub_radius], fill=BG)

    return image.resize((size, size), Image.LANCZOS)


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)

    targets = [
        ("icon-192.png", 192, False),
        ("icon-512.png", 512, False),
        ("icon-maskable-512.png", 512, True),
    ]

    for name, size, maskable in targets:
        draw_cog(size, maskable).save(OUT / name, "PNG", optimize=True)
        print(f"make_icons.py: wrote web/icons/{name} ({size}x{size})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
