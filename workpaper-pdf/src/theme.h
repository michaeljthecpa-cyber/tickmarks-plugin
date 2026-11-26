// Look and feel: the light and dark palettes, and smooth (anti-aliased) drawing helpers built on GDI+.
#pragma once
#include <windows.h>
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

struct Palette {
    COLORREF chrome;      // tab strip and status bar
    COLORREF surface;     // toolbar, panes, cards
    COLORREF hover;       // hovered controls
    COLORREF pressed;     // pressed controls
    COLORREF border;      // hairlines between areas
    COLORREF text;        // main text and icons
    COLORREF text2;       // secondary text
    COLORREF muted;       // hints and disabled
    COLORREF accent;      // brand colour: selected tools, primary buttons
    COLORREF accentHover;
    COLORREF accentSoft;  // fill behind selected things
    COLORREF onAccent;    // text on the accent colour
    COLORREF canvas;      // behind the pages
    COLORREF input;       // search and page boxes
    COLORREF shadow;      // page shadow colour
};

inline Palette LightPalette() {
    return Palette{RGB(0xF4, 0xF5, 0xF8), RGB(0xFF, 0xFF, 0xFF), RGB(0xF0, 0xF2, 0xF6), RGB(0xE6, 0xE9, 0xF0),
                   RGB(0xE3, 0xE6, 0xEC), RGB(0x1A, 0x1D, 0x24), RGB(0x4A, 0x51, 0x5E), RGB(0x86, 0x8D, 0x99),
                   RGB(0x4F, 0x46, 0xE5), RGB(0x43, 0x38, 0xCA), RGB(0xEE, 0xED, 0xFD), RGB(0xFF, 0xFF, 0xFF),
                   RGB(0xE9, 0xEB, 0xF0), RGB(0xF1, 0xF2, 0xF6), RGB(0x1A, 0x1F, 0x36)};
}

inline Palette DarkPalette() {
    return Palette{RGB(0x18, 0x19, 0x1D), RGB(0x22, 0x24, 0x29), RGB(0x2D, 0x30, 0x36), RGB(0x36, 0x39, 0x41),
                   RGB(0x33, 0x36, 0x3D), RGB(0xEC, 0xED, 0xF0), RGB(0xC0, 0xC4, 0xCC), RGB(0x86, 0x8B, 0x95),
                   RGB(0x8B, 0x85, 0xFF), RGB(0xA3, 0x9E, 0xFF), RGB(0x34, 0x32, 0x5C), RGB(0x14, 0x14, 0x1C),
                   RGB(0x13, 0x14, 0x17), RGB(0x2B, 0x2D, 0x33), RGB(0x00, 0x00, 0x00)};
}

inline COLORREF Mix(COLORREF a, COLORREF b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

inline void FillSolid(HDC hdc, const RECT& r, COLORREF c) {
    SetDCBrushColor(hdc, c);
    FillRect(hdc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

inline Gdiplus::Color GpColor(COLORREF c, int alpha = 255) {
    return Gdiplus::Color((BYTE)alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

inline void RoundPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0.5f) {
        p.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

// A rounded rectangle with smooth edges. Pass CLR_INVALID for no fill or no outline.
inline void RoundRectAA(HDC hdc, const RECT& r, float radius, COLORREF fill, COLORREF line = CLR_INVALID, float lineW = 1.0f,
                        int fillAlpha = 255) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (fill != CLR_INVALID) {
        Gdiplus::GraphicsPath p;
        RoundPath(p, (float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top), radius);
        Gdiplus::SolidBrush b(GpColor(fill, fillAlpha));
        g.FillPath(&b, &p);
    }
    if (line != CLR_INVALID) {
        float in = lineW / 2;
        Gdiplus::GraphicsPath p;
        RoundPath(p, r.left + in, r.top + in, r.right - r.left - lineW, r.bottom - r.top - lineW, radius - in);
        Gdiplus::Pen pen(GpColor(line), lineW);
        g.DrawPath(&pen, &p);
    }
}

inline void EllipseAA(HDC hdc, const RECT& r, COLORREF fill, COLORREF line = CLR_INVALID, float lineW = 1.0f) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    if (fill != CLR_INVALID) {
        Gdiplus::SolidBrush b(GpColor(fill));
        g.FillEllipse(&b, (float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top));
    }
    if (line != CLR_INVALID) {
        Gdiplus::Pen pen(GpColor(line), lineW);
        float in = lineW / 2;
        g.DrawEllipse(&pen, r.left + in, r.top + in, r.right - r.left - lineW, r.bottom - r.top - lineW);
    }
}

// A soft drop shadow under a rectangle (e.g. a page), built from a few faint layers.
inline void SoftShadow(HDC hdc, const RECT& r, int spread, COLORREF c, int strength) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for (int k = spread; k >= 1; k--) {
        int a = strength * (spread - k + 1) / (spread * spread / 2 + spread);
        Gdiplus::GraphicsPath p;
        RoundPath(p, (float)(r.left - k), (float)(r.top - k + spread / 3), (float)(r.right - r.left + 2 * k),
                  (float)(r.bottom - r.top + 2 * k), (float)k + 1);
        Gdiplus::SolidBrush b(GpColor(c, std::clamp(a, 1, 255)));
        g.FillPath(&b, &p);
    }
}
