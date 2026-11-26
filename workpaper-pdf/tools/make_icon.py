"""Draws the Workpaper PDF app icon (a page with a red tick) and writes res/app.ico."""
from PIL import Image, ImageDraw
import os, sys

def draw(size):
    s = size / 256
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # page with folded corner
    d.rounded_rectangle([40*s, 16*s, 216*s, 240*s], radius=18*s, fill=(31, 111, 235, 255))
    d.rounded_rectangle([56*s, 32*s, 200*s, 224*s], radius=10*s, fill=(255, 255, 255, 255))
    for i, y in enumerate((70, 100, 130)):
        d.rounded_rectangle([76*s, y*s, (180 if i != 2 else 140)*s, (y+10)*s], radius=4*s, fill=(190, 200, 215, 255))
    # tick mark
    w = max(2, int(20*s))
    d.line([(86*s, 172*s), (116*s, 200*s), (176*s, 140*s)], fill=(214, 40, 40, 255), width=w, joint="curve")
    return im

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "res", "app.ico")
big = draw(256)
big.save(out, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("wrote", out)
