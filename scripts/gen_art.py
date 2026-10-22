#!/usr/bin/env python3
"""Draw the NHL Scores app icons and banner.

Writes:
  resources/images/menu_icon.png     25x25 launcher icon (1-bit, white on black)
  screenshots/icon_144.png           app store icons
  screenshots/icon_80.png
  screenshots/icon_25.png
  screenshots/banner.png             720x320 app store banner

Everything is drawn at a large size and scaled down so edges stay smooth.
Needs Pillow:  pip3 install Pillow
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHOTS = os.path.join(ROOT, "screenshots")
RES = os.path.join(ROOT, "resources", "images")

# Pebble 64-color palette values
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
COBALT = (0, 85, 170)
SKY = (85, 170, 255)
RED = (255, 0, 0)
GRAY = (170, 170, 170)

BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SS = 4  # supersampling factor


def font(size):
    return ImageFont.truetype(BOLD, size)


def draw_icon(size):
    """Cobalt rounded square with a hockey stick, puck and a goal-light dot."""
    n = size * SS
    img = Image.new("RGB", (n, n), BLACK)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, n - 1, n - 1), radius=int(n * 0.2), fill=COBALT)

    def p(x, y):
        return (x * n, y * n)

    # shaft
    d.line([p(0.80, 0.16), p(0.36, 0.68)], fill=WHITE, width=int(n * 0.075))
    # blade
    d.line([p(0.38, 0.69), p(0.14, 0.73)], fill=WHITE, width=int(n * 0.10))
    for cx, cy in (p(0.14, 0.73), p(0.38, 0.69), p(0.80, 0.16)):
        r = n * 0.0375 if (cx, cy) == p(0.80, 0.16) else n * 0.05
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=WHITE)

    # puck: dark side, then the top face with a white rim
    cx, cy, rx, ry, h = 0.70 * n, 0.77 * n, 0.15 * n, 0.075 * n, 0.045 * n
    d.ellipse((cx - rx, cy - ry + h, cx + rx, cy + ry + h), fill=BLACK, outline=WHITE,
              width=max(2, int(n * 0.012)))
    d.rectangle((cx - rx, cy, cx + rx, cy + h), fill=BLACK)
    d.line([(cx - rx, cy), (cx - rx, cy + h)], fill=WHITE, width=max(2, int(n * 0.012)))
    d.line([(cx + rx, cy), (cx + rx, cy + h)], fill=WHITE, width=max(2, int(n * 0.012)))
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=BLACK, outline=WHITE,
              width=max(2, int(n * 0.012)))

    # goal light
    gx, gy, gr = 0.22 * n, 0.22 * n, 0.075 * n
    d.ellipse((gx - gr * 1.5, gy - gr * 1.5, gx + gr * 1.5, gy + gr * 1.5), fill=(170, 0, 0))
    d.ellipse((gx - gr, gy - gr, gx + gr, gy + gr), fill=RED)

    return img.resize((size, size), Image.LANCZOS)


def draw_menu_icon():
    """25x25, 1-bit, drawn pixel by pixel: three game rows, each with a puck dot.
    White on black, like the rest of the Pebble launcher icons."""
    img = Image.new("1", (25, 25), 0)
    px = img.load()
    for cy in (5, 12, 19):
        for dy in range(-2, 3):          # 5x5 dot with the corners cut off
            for dx in range(-2, 3):
                if abs(dx) == 2 and abs(dy) == 2:
                    continue
                px[3 + dx, cy + dy] = 1
        for y in range(cy - 1, cy + 2):  # 3px tall bar
            for x in range(9, 24):
                px[x, y] = 1
    return img


def draw_banner():
    w, h = 720, 320
    img = Image.new("RGB", (w, h), BLACK)
    icon = draw_icon(240)
    img.paste(icon, (40, 40))
    d = ImageDraw.Draw(img)

    x = 312
    d.text((x, 22), "NHL", font=font(124), fill=WHITE)
    d.text((x, 138), "Scores", font=font(96), fill=SKY)
    d.rectangle((x + 4, 256, x + 4 + 64, 261), fill=RED)
    tagline = "Standings | Teams | Playoffs"
    size = 26
    while d.textlength(tagline, font=font(size)) > w - x - 24:
        size -= 1
    d.text((x, 270), tagline, font=font(size), fill=GRAY)
    return img


def main():
    os.makedirs(SHOTS, exist_ok=True)
    os.makedirs(RES, exist_ok=True)
    draw_menu_icon().save(os.path.join(RES, "menu_icon.png"))
    for size in (144, 80, 25):
        draw_icon(size).save(os.path.join(SHOTS, "icon_%d.png" % size))
    draw_banner().save(os.path.join(SHOTS, "banner.png"))
    print("done")


if __name__ == "__main__":
    main()
