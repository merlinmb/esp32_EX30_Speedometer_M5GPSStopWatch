"""Render the screen designs to PNG for the README (docs/images).

Usage: python tools/render_designs.py
Needs: pip install playwright && playwright install chromium
The pictures are mock-ups drawn in HTML/SVG to match the firmware layouts in src/ui.cpp.
"""
import math, json, os, sys, pathlib

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "docs" / "images"
FONTS_DIR = HERE / "fonts"
C = 233  # centre of 466x466 round AMOLED

BG = "#000000"
TXT = "#FFFFFF"
SUB = "#9AA3AD"
TRACK = "#1C2229"
TEAL = "#2EE6C8"
AMBER = "#FFB020"
RED = "#FF5A4E"

def _ff(family, weight, file):
    return f"@font-face{{font-family:'{family}';font-weight:{weight};src:url('{(FONTS_DIR / file).as_uri()}')}}"
FONTS = "<style>" + _ff("Barlow Condensed", "500 700", "BarlowCondensed-SemiBold.ttf") + _ff("Barlow", 500, "Barlow-Medium.ttf") + _ff("Barlow", "600 700", "Barlow-SemiBold.ttf") + "</style>"


def pol(r, deg):
    a = math.radians(deg)
    return C + r * math.sin(a), C - r * math.cos(a)


def arc(r, d0, d1):
    x0, y0 = pol(r, d0)
    x1, y1 = pol(r, d1)
    large = 1 if (d1 - d0) > 180 else 0
    return f"M {x0:.1f} {y0:.1f} A {r} {r} 0 {large} 1 {x1:.1f} {y1:.1f}"


def ang(v, vmax=120):
    return -135 + 270 * v / vmax


def page(title, body, w=466, h=466, props=None, round_=True):
    radius = "border-radius: 50%; overflow: hidden;" if round_ else ""
    return f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><title>{title}</title>{FONTS}
<style>html,body{{margin:0;background:transparent}}</style></head><body>
<div style="width: {w}px; height: {h}px; position: relative; background: {BG}; {radius} font-family: 'Barlow', sans-serif; color: {TXT}">
{body}
</div></body></html>"""


def svg(inner):
    return (f'<svg width="466" height="466" viewBox="0 0 466 466" style="position: absolute; left: 0; top: 0" '
            f'aria-hidden="true">{inner}</svg>')


def sat_icon(color):
    return (f'<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="2" '
            f'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">'
            f'<path d="M4 10a8 8 0 0 1 10-6"/><path d="M7 12a5 5 0 0 1 6-4"/><circle cx="16" cy="16" r="3"/>'
            f'<path d="M13 19l-3 3M19 13l3-3"/></svg>')


def abs_text(x, y, text, size, color, weight=500, family="'Barlow', sans-serif", ls="0", extra=""):
    return (f'<div style="position: absolute; left: 0; width: 466px; top: {y}px; text-align: center; '
            f'font-family: {family}; font-size: {size}px; font-weight: {weight}; color: {color}; '
            f'letter-spacing: {ls}; line-height: 1; {extra}">{text}</div>')


COND = "'Barlow Condensed', sans-serif"

# ---------------- A: Clean ----------------
def clean():
    v = 72
    s = (f'<path d="{arc(218, -135, 135)}" stroke="{TRACK}" stroke-width="8" fill="none" stroke-linecap="round"/>'
         f'<path d="{arc(218, -135, ang(v))}" stroke="{TEAL}" stroke-width="8" fill="none" stroke-linecap="round"/>')
    status = (f'<div style="position: absolute; top: 62px; left: 0; width: 466px; display: flex; justify-content: center; '
              f'align-items: center; gap: 8px; font-size: 16px; font-weight: 500; color: {SUB}; letter-spacing: 0.06em">'
              f'{sat_icon(TEAL)}<span>14 SATS</span><span style="color: #4A525B">|</span><span>10 Hz</span></div>')
    body = (svg(s) + status
            + abs_text(0, 118, "72", 220, TXT, 600, COND, "-0.02em")
            + abs_text(0, 330, "MPH", 22, SUB, 600, "'Barlow', sans-serif", "0.32em")
            + abs_text(0, 382, "18:56 &#183; 12.4 mi", 18, "#7C858F", 500))
    return page("Clean", body, props={"accent": {"editor": "color", "default": TEAL}})


# ---------------- B: Gauge ----------------
def gauge_svg(v, limit, over=False):
    col = RED if over else TEAL
    parts = [f'<path d="{arc(206, -135, 135)}" stroke="{TRACK}" stroke-width="14" fill="none" stroke-linecap="butt"/>',
             f'<path d="{arc(206, -135, ang(v))}" stroke="{col}" stroke-width="14" fill="none" stroke-linecap="butt"/>']
    for t in range(0, 121, 5):
        a = ang(t)
        major = t % 20 == 0
        mid = t % 10 == 0
        r0 = 176 if major else (182 if mid else 186)
        x0, y0 = pol(r0, a)
        x1, y1 = pol(192, a)
        c = "#C9D0D6" if major else "#5A636C"
        wdt = 3 if major else 2
        parts.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="{c}" stroke-width="{wdt}" stroke-linecap="round"/>')
        if major:
            lx, ly = pol(154, a)
            parts.append(f'<text x="{lx:.1f}" y="{ly + 6:.1f}" text-anchor="middle" font-family="Barlow Condensed" '
                         f'font-weight="600" font-size="19" fill="#8E979F">{t}</text>')
    # limit marker
    la = ang(limit)
    p1 = pol(218, la)
    p2 = pol(230, la - 3.2)
    p3 = pol(230, la + 3.2)
    parts.append(f'<polygon points="{p1[0]:.1f},{p1[1]:.1f} {p2[0]:.1f},{p2[1]:.1f} {p3[0]:.1f},{p3[1]:.1f}" fill="{AMBER}"/>')
    x0, y0 = pol(198, la)
    x1, y1 = pol(214, la)
    parts.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="{AMBER}" stroke-width="3"/>')
    if over:
        parts.append(f'<path d="{arc(206, la, ang(v))}" stroke="#FF8A80" stroke-width="14" fill="none"/>')
    return "".join(parts)


def gauge(v=64, limit=70, over=False):
    s = gauge_svg(v, limit, over)
    if over:
        pill = (f'<div style="position: absolute; top: 352px; left: 158px; width: 150px; height: 34px; border-radius: 17px; '
                f'background: {RED}; color: #000; display: flex; align-items: center; justify-content: center; '
                f'font-size: 17px; font-weight: 600; letter-spacing: 0.04em">+{v - limit} OVER {limit}</div>')
    else:
        pill = (f'<div style="position: absolute; top: 352px; left: 168px; width: 130px; height: 34px; border-radius: 17px; '
                f'border: 2px solid {AMBER}; box-sizing: border-box; color: {AMBER}; display: flex; align-items: center; '
                f'justify-content: center; font-size: 16px; font-weight: 600; letter-spacing: 0.08em">LIMIT {limit}</div>')
    body = (svg(s)
            + abs_text(0, 148, str(v), 150, RED if over else TXT, 600, COND, "-0.02em")
            + abs_text(0, 302, "MPH", 20, SUB, 600, "'Barlow', sans-serif", "0.3em")
            + pill)
    return page("Gauge over limit" if over else "Gauge", body,
                props={"accent": {"editor": "color", "default": TEAL}})


# ---------------- C: Info ----------------
def tile(x, y, label, value, unit):
    return (f'<div style="position: absolute; left: {x}px; top: {y}px; width: 120px; display: flex; flex-direction: column; '
            f'align-items: center; gap: 4px">'
            f'<span style="font-size: 12px; font-weight: 600; color: #8A939C; letter-spacing: 0.16em">{label}</span>'
            f'<span style="font-family: {COND}; font-size: 32px; font-weight: 600; color: {TXT}; line-height: 1">{value}'
            f'<span style="font-size: 16px; color: {SUB}; margin-left: 3px">{unit}</span></span></div>')


def info():
    v = 64
    s = (f'<path d="{arc(222, -135, 135)}" stroke="{TRACK}" stroke-width="4" fill="none" stroke-linecap="round"/>'
         f'<path d="{arc(222, -135, ang(v))}" stroke="{TEAL}" stroke-width="4" fill="none" stroke-linecap="round"/>'
         f'<line x1="83" y1="292" x2="383" y2="292" stroke="#232A31" stroke-width="1"/>'
         f'<line x1="233" y1="304" x2="233" y2="408" stroke="#232A31" stroke-width="1"/>')
    top = (f'<div style="position: absolute; top: 46px; left: 0; width: 466px; display: flex; justify-content: center; '
           f'gap: 16px; font-size: 15px; font-weight: 500; color: {SUB}; letter-spacing: 0.05em">'
           f'<span>18:56</span><span style="color: {TEAL}">&#9679; 14 sats</span><span>HDOP 0.7</span></div>')
    body = (svg(s) + top
            + abs_text(0, 82, str(v), 150, TXT, 600, COND, "-0.02em")
            + abs_text(0, 236, "MPH &#183; HEADING NE 042&#176;", 15, SUB, 600, "'Barlow', sans-serif", "0.14em")
            + tile(108, 308, "TRIP", "12.4", "mi") + tile(238, 308, "MAX", "78", "mph")
            + tile(108, 362, "AVG", "41", "mph") + tile(238, 362, "ALT", "112", "m"))
    return page("Info", body, props={"accent": {"editor": "color", "default": TEAL}})


# ---------------- D: Night ----------------
def night():
    v = 48
    NA = "#E8901C"
    parts = []
    n = 46
    for i in range(n):
        t = i * 120 / (n - 1)
        a = ang(t)
        x, y = pol(214, a)
        lit = t <= v
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{3.2 if lit else 2.2}" fill="{NA if lit else "#2A1E10"}"/>')
    status = (f'<div style="position: absolute; top: 74px; left: 0; width: 466px; display: flex; justify-content: center; '
              f'align-items: center; gap: 8px; font-size: 15px; font-weight: 500; color: #8A5A14; letter-spacing: 0.1em">'
              f'<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#8A5A14" stroke-width="2" '
              f'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>'
              f'<span>NIGHT &#183; 15%</span></div>')
    body = (svg("".join(parts)) + status
            + abs_text(0, 128, str(v), 200, NA, 600, COND, "-0.02em")
            + abs_text(0, 324, "MPH", 20, "#9A6418", 600, "'Barlow', sans-serif", "0.32em")
            + abs_text(0, 376, "20:41", 17, "#6E4A14", 500))
    return page("Night", body, props={"accent": {"editor": "color", "default": NA}})



# ---------------- B2: Gauge solid ----------------
def gauge_solid(v=64, limit=70, over=False):
    R, HW = 202, 11
    col = RED if over else TEAL
    parts = [f'<path d="{arc(R, -135, 135)}" stroke="{TRACK}" stroke-width="{HW*2}" fill="none"/>',
             f'<path d="{arc(R, -135, ang(v))}" stroke="{col}" stroke-width="{HW*2}" fill="none"/>']
    if over:
        parts.append(f'<path d="{arc(R, ang(limit), ang(v))}" stroke="#FF9A8E" stroke-width="{HW*2}" fill="none"/>')
    outer = R - HW - 6
    for t in range(0, 121, 20):
        a = ang(t)
        x, y = pol(outer - 4, a)
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3" fill="#C9D0D6"/>')
        lx, ly = pol(outer - 28, a)
        parts.append(f'<text x="{lx:.1f}" y="{ly + 6:.1f}" text-anchor="middle" font-family="Barlow" font-weight="500" font-size="18" fill="#8E979F">{t}</text>')
    la = ang(limit)
    p1, p2, p3 = pol(R + HW + 2, la), pol(R + HW + 14, la - 3.2), pol(R + HW + 14, la + 3.2)
    parts.append(f'<polygon points="{p1[0]:.1f},{p1[1]:.1f} {p2[0]:.1f},{p2[1]:.1f} {p3[0]:.1f},{p3[1]:.1f}" fill="{AMBER}"/>')
    if over:
        pill = (f'<div style="position: absolute; top: 352px; left: 153px; width: 160px; height: 34px; border-radius: 17px; '
                f'background: {RED}; color: #000; display: flex; align-items: center; justify-content: center; '
                f'font-size: 17px; font-weight: 600; letter-spacing: 0.04em">+{v - limit} OVER {limit}</div>')
    else:
        pill = (f'<div style="position: absolute; top: 352px; left: 170px; width: 126px; height: 34px; border-radius: 17px; '
                f'background: {AMBER}; color: #000; display: flex; align-items: center; justify-content: center; '
                f'font-size: 16px; font-weight: 600; letter-spacing: 0.06em">LIMIT {limit}</div>')
    body = (svg("".join(parts))
            + abs_text(0, 152, str(v), 150, RED if over else TXT, 600, COND, "-0.02em")
            + abs_text(0, 300, "MPH", 20, SUB, 600, "'Barlow', sans-serif", "0.3em")
            + pill + abs_text(0, 410, "14 SATS", 16, SUB, 500))
    return page("Gauge solid", body)


# ---------------- Touch zones ----------------
def touch_zones():
    r0 = 95
    parts = [f'<circle cx="233" cy="233" r="232" fill="#0B0F13" stroke="#2A333B" stroke-width="2"/>']
    for d in (45, 135, 225, 315):
        a = math.radians(d)
        x0, y0 = 233 + r0 * math.sin(a), 233 - r0 * math.cos(a)
        x1, y1 = 233 + 232 * math.sin(a), 233 - 232 * math.cos(a)
        parts.append(f'<line x1="{x0:.1f}" y1="{y0:.1f}" x2="{x1:.1f}" y2="{y1:.1f}" stroke="#2A333B" stroke-width="2" stroke-dasharray="6 6"/>')
    parts.append(f'<circle cx="233" cy="233" r="{r0}" fill="#12302C" stroke="{TEAL}" stroke-width="2"/>')
    def lab(x, y, big, small, colour=TXT):
        return (f'<text x="{x}" y="{y}" text-anchor="middle" font-family="Barlow" font-weight="600" font-size="22" fill="{colour}">{big}</text>'
                f'<text x="{x}" y="{y + 24}" text-anchor="middle" font-family="Barlow" font-weight="500" font-size="16" fill="{SUB}">{small}</text>')
    parts += [lab(233, 80, "Brighter", "top"), lab(233, 392, "Dimmer", "bottom"),
              lab(390, 228, "Next", "design"), lab(76, 228, "Previous", "design"),
              lab(233, 228, "Status", "on / off", TEAL)]
    body = f'<svg width="466" height="466" viewBox="0 0 466 466" role="img" aria-label="Touch zones">{"".join(parts)}</svg>'
    return page("Touch zones", body)


DESIGNS = [
    ("design-a-clean.png", clean),
    ("design-b1-gauge.png", lambda: gauge()),
    ("design-b1-gauge-over-limit.png", lambda: gauge(74, 70, True)),
    ("design-b2-gauge-solid.png", lambda: gauge_solid()),
    ("design-b2-gauge-solid-over-limit.png", lambda: gauge_solid(74, 70, True)),
    ("design-c-info.png", info),
    ("design-d-night.png", night),
    ("touch-zones.png", touch_zones),
]


def main():
    from playwright.sync_api import sync_playwright
    OUT.mkdir(parents=True, exist_ok=True)
    tmp = OUT / "_render.html"
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 466, "height": 466}, device_scale_factor=1)
        for name, fn in DESIGNS:
            tmp.write_text(fn(), encoding="utf-8")
            pg.goto(tmp.as_uri())
            pg.wait_for_timeout(250)
            pg.screenshot(path=str(OUT / name), omit_background=True,
                          clip={"x": 0, "y": 0, "width": 466, "height": 466})
            print("wrote", name)
        # Overview strip of the five designs in order
        order = ["design-a-clean.png", "design-b1-gauge.png", "design-b2-gauge-solid.png", "design-c-info.png", "design-d-night.png"]
        labels = ["A Clean", "B1 Gauge", "B2 Gauge solid", "C Info", "D Night"]
        cells = "".join(
            f'<figure style="margin:0;display:flex;flex-direction:column;align-items:center;gap:10px">'
            f'<img src="{(OUT / f).as_uri()}" width="300" height="300"><figcaption style="font:600 20px Barlow;color:#C9D0D6">{l}</figcaption></figure>'
            for f, l in zip(order, labels))
        tmp.write_text(f'<!doctype html><html><head><meta charset="utf-8">{FONTS}</head><body style="margin:0;background:#0B0E11">'
                       f'<div style="display:flex;gap:28px;padding:28px">{cells}</div></body></html>', encoding="utf-8")
        pg.set_viewport_size({"width": 5 * 300 + 4 * 28 + 56, "height": 300 + 56 + 40})
        pg.goto(tmp.as_uri())
        pg.wait_for_timeout(300)
        pg.screenshot(path=str(OUT / "designs-overview.png"), full_page=True)
        print("wrote designs-overview.png")
        b.close()
    tmp.unlink()


if __name__ == "__main__":
    main()
