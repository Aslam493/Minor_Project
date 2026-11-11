"""
Overlays the IIT Delhi logo (white circle badge) onto the top-right corner of a slide image.
Usage: python overlay_logo.py <input.png> <output.png>
"""
import sys
from PIL import Image, ImageDraw

LOGO_PATH = "/home/aslam/Minor_Project/slides_midterm_image/IIT_Delhi_logo.png"

def overlay(src_path, dst_path, size_ratio=0.055, margin_ratio=0.018):
    base = Image.open(src_path).convert("RGBA")
    w, h = base.size
    d = int(min(w, h) * size_ratio)
    margin = int(min(w, h) * margin_ratio)

    logo = Image.open(LOGO_PATH).convert("RGBA")
    logo.thumbnail((int(d * 0.78), int(d * 0.78)), Image.LANCZOS)

    # white circular badge
    badge = Image.new("RGBA", (d, d), (0, 0, 0, 0))
    draw = ImageDraw.Draw(badge)
    draw.ellipse((0, 0, d, d), fill=(255, 255, 255, 255))
    shadow = Image.new("RGBA", (d + 12, d + 12), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).ellipse((6, 8, d + 6, d + 8), fill=(0, 0, 0, 40))

    lx = (d - logo.width) // 2
    ly = (d - logo.height) // 2
    badge.paste(logo, (lx, ly), logo)

    x = w - d - margin
    y = margin
    base.alpha_composite(shadow, (x - 6, y - 4))
    base.alpha_composite(badge, (x, y))
    base.convert("RGB").save(dst_path, quality=95)
    print(f"Saved: {dst_path}")

if __name__ == "__main__":
    overlay(sys.argv[1], sys.argv[2])
