#!/usr/bin/env python3
"""XPanel icons: write the Inkscape source documents and let Inkscape build the final icons.

Usage: python3 -I make_icons.py            # writes src/*.svg, builds out/*.svg + out/*.png previews

Every icon is drawn on a 64 x 64 grid from named simple shapes (lines, arcs, circles, rects)
with one stroke weight. Inkscape then converts the strokes to outlines and joins each
monochrome icon into one filled path (object-stroke-to-path + path-union). The colour icons
(logo, app) keep their separate coloured shapes.
"""
import math
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC, OUT = HERE / "src", HERE / "out"
INK = "#f2f2f2"
W = 4  # stroke weight on the 64 grid

HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        'width="64" height="64" viewBox="0 0 64 64">\n')
STROKE = f'fill:none;stroke:{INK};stroke-width:{W};stroke-linecap:round;stroke-linejoin:round'
FILL = f'fill:{INK};stroke:none'


def arc(cx, cy, r, a0, a1):
    """SVG path of a circle arc, angles in degrees (0 = right, 90 = down)."""
    x0, y0 = cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0))
    x1, y1 = cx + r * math.cos(math.radians(a1)), cy + r * math.sin(math.radians(a1))
    large = 1 if (a1 - a0) % 360 > 180 else 0
    return f"M{x0:.2f} {y0:.2f}A{r} {r} 0 {large} 1 {x1:.2f} {y1:.2f}"


def gear_outline(cx=32, cy=32, teeth=8, r_tip=26, r_root=20.5, tip_deg=17, root_deg=26):
    """Closed outline of a gear: flat tooth tips, arcs between teeth."""
    step = 360 / teeth
    pts = []
    for i in range(teeth):
        c = -90 + i * step
        for a, r in ((c - root_deg / 2, r_root), (c - tip_deg / 2, r_tip), (c + tip_deg / 2, r_tip), (c + root_deg / 2, r_root)):
            pts.append((cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))))
    d = f"M{pts[0][0]:.2f} {pts[0][1]:.2f}"
    for k in range(1, len(pts)):
        x, y = pts[k]
        if k % 4 == 0:  # from the last root point of a tooth to the first of the next: an arc
            d += f"A{r_root} {r_root} 0 0 1 {x:.2f} {y:.2f}"
        else:
            d += f"L{x:.2f} {y:.2f}"
    x, y = pts[0]
    return d + f"A{r_root} {r_root} 0 0 1 {x:.2f} {y:.2f}Z"


def s(label, inner):  # stroked part
    return f'<g inkscape:label="{label}" style="{STROKE}">{inner}</g>\n'


def f(label, inner):  # filled part
    return f'<g inkscape:label="{label}" style="{FILL}">{inner}</g>\n'


def hex_mark():
    """XPanel mark: a hexagon outline open at the upper-right corner, with a bolt inside."""
    pts = [(32 + 27 * math.cos(math.radians(-90 + 60 * i)), 32 + 27 * math.sin(math.radians(-90 + 60 * i))) for i in range(6)]
    p, a, b = pts[1], pts[0], pts[2]  # cut the upper-right corner: 42 % along both of its edges
    q1 = (p[0] + (a[0] - p[0]) * 0.42, p[1] + (a[1] - p[1]) * 0.42)
    q2 = (p[0] + (b[0] - p[0]) * 0.42, p[1] + (b[1] - p[1]) * 0.42)
    run = [q2] + pts[2:] + [pts[0], q1]
    return "M" + " L".join(f"{x:.2f} {y:.2f}" for x, y in run)


HEX = hex_mark()
BOLT = "M35 13L20 35H30L27 51L44 28H34Z"
HEX_STYLE = "fill:none;stroke:{c};stroke-width:5;stroke-linecap:round;stroke-linejoin:round"

ICONS = {
    # Operating Mode: lightning bolt
    "opmode": s("bolt", '<path d="M36 5L15 36H30L27 59L49 27H34Z"/>'),
    # Control Mode: gamepad
    "ctrlmode": s("body", '<path d="M18 18H46C54 18 59 24 60 33L61 42C62 50 54 53 49 47L44 41H20L15 47C10 53 2 50 3 42L4 33C5 24 10 18 18 18Z"/>')
              + s("dpad", '<path d="M18 25V35M13 30H23"/>')
              + f("buttons", '<circle cx="44" cy="27" r="3"/><circle cx="50" cy="33" r="3"/>'),
    # Vibration: handheld between two zigzag waves
    "vibration": s("device", '<rect x="21" y="10" width="22" height="44" rx="4"/>')
               + s("waves", '<path d="M13 19L7 25.5L13 32L7 38.5L13 45M51 19L57 25.5L51 32L57 38.5L51 45"/>'),
    # Manual TDP / settings: three sliders
    "sliders": s("rails", '<path d="M14 8V56M32 8V56M50 8V56"/>')
             + f("knobs", '<rect x="7" y="15" width="14" height="10" rx="3"/><rect x="25" y="36" width="14" height="10" rx="3"/>'
                          '<rect x="43" y="22" width="14" height="10" rx="3"/>'),
    # Keyboard
    "keyboard": s("case", '<rect x="4" y="16" width="56" height="32" rx="5"/>')
              + f("keys", "".join(f'<rect x="{x}" y="22.5" width="5" height="5" rx="1"/>' for x in (11, 19, 27, 35, 43, 51))
                          + "".join(f'<rect x="{x}" y="30" width="5" height="5" rx="1"/>' for x in (11, 19, 27, 35))
                          + '<rect x="43" y="30" width="13" height="5" rx="1"/><rect x="19" y="38" width="26" height="4.5" rx="1"/>'),
    # Real-time Monitor: screen with a graph
    "monitor": s("screen", '<rect x="4" y="10" width="56" height="36" rx="4"/>')
             + s("graph", '<path d="M11 37L21 27L29 33L41 19L53 29"/>')
             + s("stand", '<path d="M32 46V55M21 55H43"/>'),
    # Charge Limit: battery with a bolt
    "charge": s("battery", '<rect x="5" y="18" width="48" height="28" rx="5"/>')
            + f("tip", '<rect x="55" y="26" width="5" height="12" rx="2"/>')
            + f("bolt", '<path d="M31 21L21 34H28.5L25 43L37 29H29.5Z"/>'),
    # Show Desktop: four windows
    "desktop": f("panes", '<rect x="8" y="8" width="22" height="22" rx="2"/><rect x="34" y="8" width="22" height="22" rx="2"/>'
                          '<rect x="8" y="34" width="22" height="22" rx="2"/><rect x="34" y="34" width="22" height="22" rx="2"/>'),
    # Lighting: three overlapping rings
    "rgb": s("rings", '<circle cx="32" cy="22" r="13"/><circle cx="22.5" cy="39" r="13"/><circle cx="41.5" cy="39" r="13"/>'),
    # Refresh Rate: screen with "Hz" drawn as lines
    "refresh": s("screen", '<rect x="4" y="13" width="56" height="38" rx="4"/>')
             + s("H", '<path d="M18 24V40M28 24V40M18 32H28"/>')
             + s("z", '<path d="M35 28H46L35 40H46"/>'),
    # Fan Curve: hub and three blades
    "fan": s("hub", '<circle cx="32" cy="32" r="5"/>')
         + "".join(s(f"blade{i}", f'<path transform="rotate({a} 32 32)" d="M32 27C29.5 16 33.5 7 43 8C50 9.5 47 21 37 29"/>')
                   for i, a in enumerate((0, 120, 240))),
    # Wi-Fi: three arcs and a dot
    "wifi": s("arcs", "".join(f'<path d="{arc(32, 50, r, -135, -45)}"/>' for r in (34, 23, 12)))
          + f("dot", '<circle cx="32" cy="50" r="4"/>'),
    # Bluetooth rune
    "bluetooth": s("rune", '<path d="M20 20L44 42L32 54V10L44 22L20 44"/>'),
    # Screenshot: corner marks and a lens
    "screenshot": s("corners", '<path d="M6 18V8H16M48 8H58V18M58 46V56H48M16 56H6V46"/>')
                + s("lens", '<circle cx="32" cy="32" r="10"/>'),
    # Brightness
    "sun": s("disc", '<circle cx="32" cy="32" r="9"/>')
         + s("rays", "".join(f'<path d="M{32 + 18 * math.cos(math.radians(a)):.2f} {32 + 18 * math.sin(math.radians(a)):.2f}'
                             f'L{32 + 25 * math.cos(math.radians(a)):.2f} {32 + 25 * math.sin(math.radians(a)):.2f}"/>'
                             for a in range(0, 360, 45))),
    # Volume and muted
    "volume": s("speaker", '<path d="M8 24H18L32 12V52L18 40H8Z"/>')
            + s("waves", f'<path d="{arc(32, 32, 10, -45, 45)}"/><path d="{arc(32, 32, 19, -45, 45)}"/>'),
    "muted": s("speaker", '<path d="M8 24H18L32 12V52L18 40H8Z"/>')
           + s("cross", '<path d="M42 25L56 39M56 25L42 39"/>'),
    # Power
    "power": s("stem", '<path d="M32 6V30"/>') + s("ring", f'<path d="{arc(32, 35, 22, -55, 235)}"/>'),
    # Display Mode: a monitor behind a handheld
    "display": s("monitor", '<rect x="24" y="8" width="36" height="24" rx="3"/>')
             + s("stand", '<path d="M42 32V38M38 38H46"/>')
             + s("handheld", '<rect x="4" y="36" width="28" height="20" rx="6"/>')
             + f("screen", '<rect x="10" y="41" width="16" height="10" rx="1.5"/>'),
    # Gear (Settings / Control Center)
    "gear": s("teeth", f'<path d="{gear_outline()}"/>') + s("hole", '<circle cx="32" cy="32" r="8.5"/>'),
    # XPanel mark (white): open hexagon + bolt
    "logo": f'<g inkscape:label="hex" style="{HEX_STYLE.format(c=INK)}"><path d="{HEX}"/></g>\n' + f("bolt", f'<path d="{BOLT}"/>'),
}

COLOR_ICONS = {
    # App icon: dark tile, cyan open hexagon, white bolt
    "app": ('<g inkscape:label="tile"><rect x="2" y="2" width="60" height="60" rx="12" style="fill:#0f0f12"/></g>\n'
            '<g inkscape:label="mark" transform="translate(9 9) scale(0.72)">'
            f'<path inkscape:label="hex" style="{HEX_STYLE.format(c="#22c7e8")}" d="{HEX}"/>'
            f'<path inkscape:label="bolt" style="fill:#f2f2f2" d="{BOLT}"/></g>\n'),
    # Tray icon: the same mark at full size on a transparent background
    "tray": (f'<g inkscape:label="mark"><path inkscape:label="hex" style="{HEX_STYLE.format(c="#22c7e8")}" d="{HEX}"/>'
             f'<path inkscape:label="bolt" style="fill:#f2f2f2" d="{BOLT}"/></g>\n'),
}


def write_sources():
    SRC.mkdir(exist_ok=True)
    for name, body in {**ICONS, **COLOR_ICONS}.items():
        (SRC / f"{name}.svg").write_text(
            HEAD + f'<g inkscape:groupmode="layer" inkscape:label="{name}">\n{body}</g>\n</svg>\n')


def inkscape(src, actions):
    r = subprocess.run(["inkscape", str(src), f"--actions={actions}"], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"inkscape failed on {src.name}: {r.stderr}")


def clean(path, mono):
    """Keep only what the app needs: the paths, one fill, no Inkscape leftovers."""
    t = path.read_text()
    if mono:
        t = re.sub(r'style="[^"]*"', f'style="fill:{INK}"', t)
    t = re.sub(r'\s+id="[^"]*"', "", t)
    path.write_text(t)


def build():
    OUT.mkdir(exist_ok=True)
    for name in ICONS:
        out = OUT / f"{name}.svg"
        inkscape(SRC / f"{name}.svg",
                 "select-all:all;selection-ungroup;select-all:all;selection-ungroup;select-all:all;"
                 "object-to-path;object-stroke-to-path;select-all:all;selection-ungroup;select-all:all;"
                 f"path-union;export-plain-svg;export-filename:{out};export-overwrite;export-do")
        clean(out, True)
    for name in COLOR_ICONS:
        out = OUT / f"{name}.svg"
        inkscape(SRC / f"{name}.svg",
                 f"select-all:all;object-to-path;export-plain-svg;export-filename:{out};export-overwrite;export-do")
        clean(out, False)
    # previews at the real sizes used in the app (24 / 48 px) and large (256 px)
    for svg in OUT.glob("*.svg"):
        for px in (48, 256):
            subprocess.run(["inkscape", str(svg), "--export-type=png", f"--export-width={px}",
                            f"--export-filename={OUT / 'png' / f'{svg.stem}-{px}.png'}"], capture_output=True)


if __name__ == "__main__":
    (OUT / "png").mkdir(parents=True, exist_ok=True)
    write_sources()
    build()
    print("built", len(ICONS) + len(COLOR_ICONS), "icons into", OUT)
