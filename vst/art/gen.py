#!/usr/bin/env python3
"""Keyscope's page backgrounds, drawn as SVG: a synthwave sunset (starry sky, a striped sun, a neon grid floor
running to the horizon) with the circle of fifths drawn into the sun on the KEY page.

    python3 vst/art/gen.py      # writes vst/art/wheel.svg and vst/art/scene.svg

The plugin area is 1280 x 628; layout y = svg y + 84. The wheel's geometry must match the ring tiles in
vst/layout.conf (WHEEL_* below are the same numbers)."""
import math, os, random

W, H, Y_OFF = 1280, 628, 84
HORIZON = 476                          # layout y 560
WHEEL_CX, WHEEL_CY = 330, 398 - Y_OFF  # layout (330, 398)
R_OUT, R_IN = 250, 160                 # ring tile centres
HERE = os.path.dirname(os.path.abspath(__file__))

PINK, MAGENTA, ORANGE, NAVY, VIOLET = "#ff7ae6", "#ff2fb4", "#ffa040", "#0a0520", "#b03cff"


def defs():
    return f"""<defs>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#07031a"/><stop offset="0.55" stop-color="#1a0a40"/>
    <stop offset="1" stop-color="#4a0f62"/></linearGradient>
  <linearGradient id="sun" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="{MAGENTA}"/><stop offset="0.55" stop-color="#ff5a7a"/>
    <stop offset="1" stop-color="{ORANGE}"/></linearGradient>
  <radialGradient id="bloom"><stop offset="0" stop-color="#ff4fc8" stop-opacity="0.45"/>
    <stop offset="1" stop-color="#ff4fc8" stop-opacity="0"/></radialGradient>
  <linearGradient id="floor" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2a0848"/><stop offset="1" stop-color="#05020f"/></linearGradient>
  <linearGradient id="fog" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#ff7ae6" stop-opacity="0"/><stop offset="0.5" stop-color="#ff7ae6" stop-opacity="0.55"/>
    <stop offset="1" stop-color="#ff7ae6" stop-opacity="0"/></linearGradient>
  <linearGradient id="gridfade" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#fff" stop-opacity="0.25"/><stop offset="1" stop-color="#fff" stop-opacity="1"/></linearGradient>
  <mask id="gridmask"><rect x="0" y="{HORIZON}" width="{W}" height="{H - HORIZON}" fill="url(#gridfade)"/></mask>
  <filter id="glow" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="3" result="b"/>
    <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="soft"><feGaussianBlur stdDeviation="6"/></filter>
</defs>"""


def sky(seed):
    rnd = random.Random(seed)
    out = [f'<rect width="{W}" height="{HORIZON}" fill="url(#sky)"/>']
    for _ in range(90):
        x, y = rnd.uniform(0, W), rnd.uniform(0, HORIZON - 60) ** 1.15 / (HORIZON - 60) ** 0.15
        r, a = rnd.choice((0.6, 0.8, 1.0, 1.4)), rnd.uniform(0.25, 0.9)
        out.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="#fff" opacity="{a:.2f}"/>')
    return "\n".join(out)


def sun(cx, cy, r, opacity):
    """A disc fading magenta to orange, its lower half cut by bands that thicken towards the bottom."""
    cuts = []
    y, gap, k = cy + r * 0.05, 2.0, 0
    while y < cy + r:
        cuts.append(f'<rect x="{cx - r - 2}" y="{y:.1f}" width="{2 * r + 4}" height="{gap:.1f}" fill="#000"/>')
        k += 1
        y += 10 + k * 1.5
        gap += 2.2
    return f"""<mask id="suncut{cx}"><rect width="{W}" height="{H}" fill="#fff"/>{''.join(cuts)}</mask>
<circle cx="{cx}" cy="{cy}" r="{r * 1.7:.0f}" fill="url(#bloom)"/>
<circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#sun)" mask="url(#suncut{cx})" opacity="{opacity}"/>"""


def mountains(x0, x1, seed, peak):
    rnd = random.Random(seed)
    pts, x = [(x0, HORIZON)], x0
    while x < x1:
        x += rnd.uniform(40, 90)
        pts.append((min(x, x1), HORIZON - rnd.uniform(peak * 0.35, peak)))
        x += rnd.uniform(40, 90)
        pts.append((min(x, x1), HORIZON - rnd.uniform(0, peak * 0.3)))
    pts.append((x1, HORIZON))
    d = "M" + " L".join("%.0f,%.0f" % p for p in pts) + " Z"
    return (f'<path d="{d}" fill="#12063a"/>'
            f'<path d="{d}" fill="none" stroke="{PINK}" stroke-width="1.6" stroke-opacity="0.75" filter="url(#glow)"/>')


def floor():
    out = [f'<rect x="0" y="{HORIZON}" width="{W}" height="{H - HORIZON}" fill="url(#floor)"/>', '<g mask="url(#gridmask)" '
           f'stroke="{PINK}" stroke-width="1.4" filter="url(#glow)">']
    vx = W / 2
    for i in range(-24, 25):   # lines to the vanishing point
        out.append(f'<line x1="{vx + i * 18:.1f}" y1="{HORIZON}" x2="{vx + i * 150:.1f}" y2="{H}"/>')
    z = 1.0
    while True:                # rows: evenly spaced on the ground, closer together towards the horizon
        y = HORIZON + 900 / (z * 9)
        if y > H:
            z += 0.6
            continue
        out.append(f'<line x1="0" y1="{y:.1f}" x2="{W}" y2="{y:.1f}"/>')
        z += 0.6
        if y - HORIZON < 3:
            break
    out.append("</g>")
    out.append(f'<rect x="0" y="{HORIZON - 14}" width="{W}" height="28" fill="url(#fog)"/>')
    out.append(f'<line x1="0" y1="{HORIZON}" x2="{W}" y2="{HORIZON}" stroke="#ffd0f6" stroke-width="1.5" filter="url(#glow)"/>')
    return "\n".join(out)


def wheel():
    """Rings, spokes and a dark centre for the key and chord readouts; the tiles themselves are MPC's."""
    cx, cy = WHEEL_CX, WHEEL_CY
    out = [f'<circle cx="{cx}" cy="{cy}" r="{R_OUT + 40}" fill="#0a0520" fill-opacity="0.35"/>',
           f'<g fill="none" filter="url(#glow)">',
           f'<circle cx="{cx}" cy="{cy}" r="{R_OUT + 36}" stroke="{PINK}" stroke-width="2"/>',
           f'<circle cx="{cx}" cy="{cy}" r="{(R_OUT + R_IN) / 2:.0f}" stroke="{VIOLET}" stroke-width="1.5" stroke-dasharray="4 6"/>',
           f'<circle cx="{cx}" cy="{cy}" r="{R_IN - 42}" stroke="{PINK}" stroke-width="2"/>']
    for i in range(12):        # spokes between neighbouring keys
        a = math.radians(i * 30 + 15)
        x0, y0 = cx + (R_IN - 42) * math.sin(a), cy - (R_IN - 42) * math.cos(a)
        x1, y1 = cx + (R_OUT + 36) * math.sin(a), cy - (R_OUT + 36) * math.cos(a)
        out.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="{VIOLET}" stroke-width="1.2" stroke-opacity="0.8"/>')
    out.append("</g>")
    out.append(f'<circle cx="{cx}" cy="{cy}" r="{R_IN - 44}" fill="#07031a" fill-opacity="0.88"/>')
    return "\n".join(out)


def page(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n'
            f'{defs()}\n{body}\n</svg>\n')


def main():
    wheel_page = "\n".join([sky(5), sun(WHEEL_CX, WHEEL_CY, R_OUT + 36, 0.85), mountains(640, W, 3, 120), floor(), wheel()])
    scene_page = "\n".join([sky(9), sun(W // 2, HORIZON - 40, 210, 0.9), mountains(0, 470, 4, 150), mountains(810, W, 6, 150),
                            floor()])
    for name, svg in (("wheel.svg", wheel_page), ("scene.svg", scene_page)):
        open(os.path.join(HERE, name), "w").write(page(svg))


if __name__ == "__main__":
    main()
