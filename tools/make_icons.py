"""Generate src/icons.pdf: one 24x24 pt page per toolbar icon (see Icon enum in Tickmarks.h)."""
import os
import pikepdf
from pikepdf import Array, Dictionary, Name

RED = "0.86 0.08 0.08"
INK = "0.25 0.25 0.25"

def circle(cx, cy, r):
    c = 0.5523 * r
    return (f"{cx+r} {cy} m {cx+r} {cy+c} {cx+c} {cy+r} {cx} {cy+r} c "
            f"{cx-c} {cy+r} {cx-r} {cy+c} {cx-r} {cy} c {cx-r} {cy-c} {cx-c} {cy-r} {cx} {cy-r} c "
            f"{cx+c} {cy-r} {cx+r} {cy-c} {cx+r} {cy} c ")

def text(s, size, x, y, color):
    return f"BT /F1 {size} Tf {color} rg {x} {y} Td ({s}) Tj ET "

CHECK = f"2.4 w 1 J 1 j {RED} RG 4 12.5 m 9.5 6.5 l 20 18.5 l S "

ICONS = [
    CHECK,                                                              # 0 toolbar
    CHECK,                                                              # 1 toolbar (active)
    CHECK,                                                              # 2 Symbols
    f"1.6 w {RED} RG " + circle(12, 12, 9.5) + "S " + text("1", 12, 8.7, 7.7, RED),   # 3 Numbers
    f"1.6 w {RED} RG " + circle(12, 12, 9.5) + "S " + text("A", 12, 7.7, 7.7, RED),   # 4 Letters
    text("+1", 13, 3, 7, INK),                                          # 5 Auto-Increment
    f"{RED} rg " + circle(6, 12, 3.6) + "f 0 0.32 0.9 rg " + circle(12, 12, 3.6) +
    "f 0 0.55 0.18 rg " + circle(18, 12, 3.6) + "f ",                   # 6 Color
    text("A", 8, 2, 7, INK) + text("A", 15, 10, 5, INK),               # 7 Size
    text("#", 15, 7, 6, INK),                                           # 8 Counters
    f"1.5 w {INK} RG 1 0.85 0.2 rg 3.5 20.5 m 20.5 20.5 l 20.5 8 l 11 8 l 6 3.5 l 7 8 l "
    "3.5 8 l h B ",                                                     # 9 Note
]

def main(out):
    pdf = pikepdf.new()
    font = pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1,
                                        BaseFont=Name("/Helvetica-Bold"), Encoding=Name.WinAnsiEncoding))
    for ops in ICONS:
        page = pikepdf.Page(Dictionary(Type=Name.Page, MediaBox=Array([0, 0, 24, 24]),
                                       Resources=Dictionary(Font=Dictionary(F1=font)),
                                       Contents=pdf.make_stream(("q " + ops + "Q").encode())))
        pdf.pages.append(page)
    pdf.save(out)

if __name__ == "__main__":
    main(os.path.join(os.path.dirname(__file__), "..", "src", "icons.pdf"))
