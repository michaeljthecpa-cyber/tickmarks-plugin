"""Writes tests/sample-60-pages.pdf, a made-up 60-page lead schedule used by the automated speed check."""
from reportlab.lib.pagesizes import letter, landscape
from reportlab.pdfgen import canvas
import sys
out = sys.argv[1] if len(sys.argv) > 1 else "tests/sample-60-pages.pdf"
c = canvas.Canvas(out, pagesize=letter)
for p in range(1, 61):
    land = p == 5
    c.setPageSize(landscape(letter) if land else letter)
    top = 540 if land else 720
    c.setFont("Helvetica-Bold", 20)
    c.drawString(72, top, f"Cash lead schedule - page {p}")
    c.setFont("Helvetica", 11)
    for i in range(30):
        y = top - 30 - i * 20
        if y < 60:
            break
        c.drawString(72, y, f"Account 10{i:02d}   Operating cash   {12345.67 * (i + 1):>14,.2f}")
    c.showPage()
c.save()
