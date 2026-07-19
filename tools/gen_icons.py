#!/usr/bin/env python3
"""Generate LVGL A8 (alpha-only) icon bitmaps for the dashboard UI.

Icons are drawn with PIL at 8x supersampling and downsampled for smooth
anti-aliased edges. Alpha-only format means LVGL recolors them at runtime
via style img_recolor — one bitmap works for every state color.

Usage: python3 tools/gen_icons.py   (writes src/ui/icons.c and icons.h)
"""

from PIL import Image, ImageDraw
import os

SS = 8          # supersampling factor
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "src", "ui")

icons = {}      # name -> (size, PIL image 'L')


def canvas(size):
    img = Image.new("L", (size * SS, size * SS), 0)
    return img, ImageDraw.Draw(img), size * SS


def register(name, size, img):
    icons[name] = (size, img.resize((size, size), Image.LANCZOS))


# ── coolant: thermometer dipped in wavy liquid ───────────────────────────
def icon_coolant(size=40):
    img, d, S = canvas(size)
    u = S / 40.0
    cx = 20 * u
    stem_w = 7 * u
    # stem
    d.rounded_rectangle([cx - stem_w / 2, 3 * u, cx + stem_w / 2, 22 * u],
                        radius=stem_w / 2, fill=255)
    # bulb
    d.ellipse([cx - 6 * u, 18 * u, cx + 6 * u, 30 * u], fill=255)
    # tick marks to the right of the stem
    for ty in (7, 12, 17):
        d.rounded_rectangle([cx + 6 * u, ty * u, cx + 12 * u, (ty + 2.4) * u],
                            radius=1.2 * u, fill=255)
    # two wavy liquid lines below
    import math
    for wy in (33, 37.5):
        pts = []
        for i in range(81):
            x = 3 * u + (34 * u) * i / 80
            y = wy * u + 1.6 * u * math.sin(i / 80 * math.pi * 3)
            pts.append((x, y))
        d.line(pts, fill=255, width=int(2.6 * u))
    register("icon_coolant", size, img)


# ── oil: classic oil can with drip ───────────────────────────────────────
def icon_oil(size=40):
    img, d, S = canvas(size)
    u = S / 40.0
    # can body
    d.rounded_rectangle([6 * u, 18 * u, 26 * u, 32 * u], radius=2.5 * u, fill=255)
    # filler neck
    d.rectangle([12 * u, 14 * u, 18 * u, 20 * u], fill=255)
    d.rectangle([10.5 * u, 12.5 * u, 19.5 * u, 15.5 * u], fill=255)
    # spout (from can top-right up to the right)
    d.polygon([(24 * u, 20 * u), (36 * u, 12 * u), (38.5 * u, 15.5 * u),
               (26 * u, 25 * u)], fill=255)
    # handle (arc on the left)
    d.arc([-1 * u, 17 * u, 9 * u, 29 * u], 90, 270, fill=255, width=int(2.6 * u))
    # drip below spout
    d.polygon([(35 * u, 20 * u), (37.8 * u, 25.5 * u), (32.2 * u, 25.5 * u)], fill=255)
    d.ellipse([32.2 * u, 23 * u, 37.8 * u, 28.5 * u], fill=255)
    register("icon_oil", size, img)


# ── turbo: snail housing with impeller ───────────────────────────────────
def icon_turbo(size=40):
    img, d, S = canvas(size)
    u = S / 40.0
    cx = cy = 19 * u
    # housing ring
    d.ellipse([cx - 14 * u, cy - 14 * u, cx + 14 * u, cy + 14 * u],
              outline=255, width=int(3.4 * u))
    # outlet duct, tangent at the top going right
    d.rectangle([cx, cy - 14 * u, 38 * u, cy - 10.6 * u], fill=255)
    d.rectangle([36 * u - 3.4 * u, cy - 14 * u, 36 * u, cy - 4 * u], fill=255)
    # impeller: hub + 6 blades
    import math
    d.ellipse([cx - 3.2 * u, cy - 3.2 * u, cx + 3.2 * u, cy + 3.2 * u], fill=255)
    for k in range(6):
        a = math.radians(k * 60 + 15)
        x2 = cx + 9.5 * u * math.cos(a)
        y2 = cy + 9.5 * u * math.sin(a)
        d.line([(cx, cy), (x2, y2)], fill=255, width=int(2.6 * u))
    register("icon_turbo", size, img)


# ── dpf: filter box with particles and through-flow ──────────────────────
def icon_dpf(size=40):
    img, d, S = canvas(size)
    u = S / 40.0
    # inlet / outlet pipes
    d.rectangle([1 * u, 17.5 * u, 8 * u, 22.5 * u], fill=255)
    d.rectangle([32 * u, 17.5 * u, 39 * u, 22.5 * u], fill=255)
    # filter body
    d.rounded_rectangle([7 * u, 9 * u, 33 * u, 31 * u], radius=4 * u,
                        outline=255, width=int(3 * u))
    # particles inside (3x2 grid of dots)
    for ix, px in enumerate((14, 20, 26)):
        for py in (16, 24):
            r = 1.9 * u
            d.ellipse([px * u - r, py * u - r, px * u + r, py * u + r], fill=255)
    register("icon_dpf", size, img)


# ── glow plug: coil wire (dashboard glow indicator) ──────────────────────
def icon_glow(size=40, name="icon_glow"):
    img, d, S = canvas(size)
    u = S / 40.0
    import math
    # coil: 3 real loops (prolate cycloid, like cursive "eee") — the classic
    # glow plug dashboard symbol
    loops = 3
    a = 1.55 * u                   # loop spacing = 2*pi*a ≈ 9.7u
    b = 5.2 * u                    # loop diameter = 2b ≈ 10.4u (b > a → loops)
    x0 = 6.0 * u
    pts = []
    for i in range(601):
        t = i / 600.0 * loops * 2 * math.pi
        x = x0 + a * t - b * math.sin(t)
        y = 21.5 * u - b * math.cos(t)
        pts.append((x, y))
    d.line(pts, fill=255, width=int(2.8 * u))
    # lead-in stubs at the loop-top height (curve starts/ends at top of loop)
    y_top = 21.5 * u - b
    x_end = x0 + a * loops * 2 * math.pi
    d.line([(2 * u, y_top), (x0 + 0.5 * u, y_top)], fill=255, width=int(2.8 * u))
    d.line([(x_end - 0.5 * u, y_top), (38 * u, y_top)], fill=255, width=int(2.8 * u))
    register(name, size, img)


# ── clock: ring with hands (trip time) ───────────────────────────────────
def icon_clock(size=40, name="icon_clock"):
    img, d, S = canvas(size)
    u = S / 40.0
    cx = cy = 20 * u
    d.ellipse([cx - 15 * u, cy - 15 * u, cx + 15 * u, cy + 15 * u],
              outline=255, width=int(3.2 * u))
    # hands: minute up-right, hour to the left
    d.line([(cx, cy), (cx + 7 * u, cy - 8 * u)], fill=255, width=int(3 * u))
    d.line([(cx, cy), (cx - 6 * u, cy)], fill=255, width=int(3 * u))
    d.ellipse([cx - 2 * u, cy - 2 * u, cx + 2 * u, cy + 2 * u], fill=255)
    register(name, size, img)


# ── road: perspective road with dashed centerline (trip distance) ────────
def icon_road(size=40, name="icon_road"):
    img, d, S = canvas(size)
    u = S / 40.0
    # left and right edges converging toward the top
    d.line([(6 * u, 36 * u), (15 * u, 5 * u)], fill=255, width=int(3.2 * u))
    d.line([(34 * u, 36 * u), (25 * u, 5 * u)], fill=255, width=int(3.2 * u))
    # dashed centerline, dashes shrinking with distance
    for y0, y1, w in ((34, 28.5, 3.4), (25, 20.5, 2.9), (17, 13.5, 2.4), (10, 7.5, 2.0)):
        d.line([(20 * u, y0 * u), (20 * u, y1 * u)], fill=255, width=int(w * u))
    register(name, size, img)


# ── pressure: manometer with ticks and needle (DPF pressure) ─────────────
def icon_pressure(size=40, name="icon_pressure"):
    img, d, S = canvas(size)
    u = S / 40.0
    import math
    cx, cy = 20 * u, 22 * u
    r = 15 * u
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255, width=int(3 * u))
    # ticks at up-left / up / up-right
    for ang in (210, 270, 330):
        a = math.radians(ang)
        x1 = cx + (r - 1.5 * u) * math.cos(a)
        y1 = cy + (r - 1.5 * u) * math.sin(a)
        x2 = cx + (r - 6 * u) * math.cos(a)
        y2 = cy + (r - 6 * u) * math.sin(a)
        d.line([(x1, y1), (x2, y2)], fill=255, width=int(2.4 * u))
    # needle pointing up-right + hub
    a = math.radians(300)
    d.line([(cx, cy), (cx + 10 * u * math.cos(a), cy + 10 * u * math.sin(a))],
           fill=255, width=int(3 * u))
    d.ellipse([cx - 2.5 * u, cy - 2.5 * u, cx + 2.5 * u, cy + 2.5 * u], fill=255)
    register(name, size, img)


# ── battery: block with terminals, + / - ─────────────────────────────────
def icon_battery(size=40):
    img, d, S = canvas(size)
    u = S / 40.0
    # terminals
    d.rectangle([8 * u, 10 * u, 14 * u, 15 * u], fill=255)
    d.rectangle([26 * u, 10 * u, 32 * u, 15 * u], fill=255)
    # body
    d.rounded_rectangle([4 * u, 13 * u, 36 * u, 32 * u], radius=2.5 * u,
                        outline=255, width=int(3 * u))
    # "-" left, "+" right
    d.rectangle([8.5 * u, 21.4 * u, 14.5 * u, 24 * u], fill=255)
    d.rectangle([25.5 * u, 21.4 * u, 31.5 * u, 24 * u], fill=255)
    d.rectangle([27.2 * u, 18 * u, 29.8 * u, 27.5 * u], fill=255)
    register("icon_battery", size, img)


# ── C file output ─────────────────────────────────────────────────────────
def write_c_files():
    c_path = os.path.join(OUT_DIR, "icons.c")
    h_path = os.path.join(OUT_DIR, "icons.h")

    with open(c_path, "w") as f:
        f.write("// Auto-generated by tools/gen_icons.py — do not edit by hand.\n")
        f.write("// LVGL A8 alpha-only bitmaps, recolored at runtime via img_recolor.\n\n")
        f.write('#include "lvgl.h"\n\n')
        for name, (size, im) in icons.items():
            data = im.tobytes()
            f.write(f"static const uint8_t {name}_map[] = {{\n")
            for row in range(size):
                chunk = data[row * size:(row + 1) * size]
                f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            f.write("};\n\n")
            f.write(f"const lv_img_dsc_t {name} = {{\n")
            f.write("    .header.cf = LV_IMG_CF_ALPHA_8BIT,\n")
            f.write("    .header.always_zero = 0,\n")
            f.write("    .header.reserved = 0,\n")
            f.write(f"    .header.w = {size},\n")
            f.write(f"    .header.h = {size},\n")
            f.write(f"    .data_size = {size * size},\n")
            f.write(f"    .data = {name}_map,\n")
            f.write("};\n\n")

    with open(h_path, "w") as f:
        f.write("// Auto-generated by tools/gen_icons.py — do not edit by hand.\n")
        f.write("#ifndef DASH_ICONS_H\n#define DASH_ICONS_H\n\n")
        f.write('#include "lvgl.h"\n\n')
        f.write('#ifdef __cplusplus\nextern "C" {\n#endif\n\n')
        for name in icons:
            f.write(f"extern const lv_img_dsc_t {name};\n")
        f.write('\n#ifdef __cplusplus\n}\n#endif\n\n#endif // DASH_ICONS_H\n')

    print(f"wrote {c_path} and {h_path} ({len(icons)} icons)")


def write_preview():
    """Side-by-side PNG preview so a human can inspect the set."""
    pad = 8
    names = list(icons.keys())
    w = sum(icons[n][0] + pad for n in names) + pad
    h = max(icons[n][0] for n in names) + 2 * pad
    sheet = Image.new("RGB", (w, h), (10, 14, 18))
    x = pad
    for n in names:
        size, im = icons[n]
        tinted = Image.merge("RGB", (
            im.point(lambda a: a * 56 // 255),
            im.point(lambda a: a * 189 // 255),
            im.point(lambda a: a * 248 // 255)))
        sheet.paste(tinted, (x, pad), None)
        x += size + pad
    out = os.path.join(os.path.dirname(__file__), "icons_preview.png")
    sheet.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    icon_coolant()
    icon_oil()
    icon_turbo()
    icon_dpf()
    icon_glow()
    icon_battery()
    icon_clock()
    icon_road()
    # Small variants rendered at target size — LVGL 8 cannot transform (zoom)
    # A8 bitmaps, so runtime lv_img_set_zoom() renders nothing for them.
    icon_glow(20, "icon_glow_sm")         # status-bar glow indicator
    icon_clock(28, "icon_clock_sm")       # mini tile
    icon_road(28, "icon_road_sm")         # mini tile
    icon_pressure(28, "icon_pressure_sm") # mini tile (DPF screen)
    write_c_files()
    write_preview()
