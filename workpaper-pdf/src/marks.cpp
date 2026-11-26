#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "marks.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <vector>
#include "fpdf_annot.h"
#include "fpdf_edit.h"

namespace marks {

struct Rgb {
    const wchar_t* name;
    uint8_t r, g, b;
};
static const Rgb kColors[] = {
    {L"Red", 220, 20, 20}, {L"Blue", 0, 82, 230}, {L"Green", 0, 140, 46},
    {L"Black", 0, 0, 0},   {L"Purple", 128, 31, 173}, {L"Orange", 242, 120, 0},
};
static const wchar_t* kSizeNames[] = {L"Small", L"Medium", L"Large"};
static const double kSizeScales[] = {0.5625, 1.0, 1.4};
static const double kLineWidth = 2.2;

int ColorCount() { return (int)(sizeof(kColors) / sizeof(kColors[0])); }
const wchar_t* ColorName(int i) { return kColors[std::clamp(i, 0, ColorCount() - 1)].name; }
void ColorRgb(int i, uint8_t& r, uint8_t& g, uint8_t& b) {
    const Rgb& c = kColors[std::clamp(i, 0, ColorCount() - 1)];
    r = c.r; g = c.g; b = c.b;
}
int SizeCount() { return 3; }
const wchar_t* SizeName(int i) { return kSizeNames[std::clamp(i, 0, 2)]; }
double SizeScale(int i) { return kSizeScales[std::clamp(i, 0, 2)]; }

std::string NumberText(int n) { return std::to_string(n); }
std::string LetterText(int i) { return std::string(1, (char)('A' + std::clamp(i, 0, 25))); }

// ---------------------------------------------------------------- drawing helpers

// Collects page objects for one mark, drawn in a local box with (0,0) at the bottom left.
struct Canvas {
    FPDF_DOCUMENT doc;
    uint8_t r, g, b;
    std::vector<FPDF_PAGEOBJECT> objs;
    FPDF_PAGEOBJECT path = nullptr;
    double width = kLineWidth;

    void Begin(double x, double y) {
        path = FPDFPageObj_CreateNewPath((float)x, (float)y);
        FPDFPageObj_SetStrokeColor(path, r, g, b, 255);
        FPDFPageObj_SetFillColor(path, r, g, b, 255);
        FPDFPageObj_SetStrokeWidth(path, (float)width);
        FPDFPageObj_SetLineCap(path, FPDF_LINECAP_ROUND);
        FPDFPageObj_SetLineJoin(path, FPDF_LINEJOIN_ROUND);
        FPDFPath_SetDrawMode(path, FPDF_FILLMODE_NONE, 1);
        objs.push_back(path);
    }
    void Move(double x, double y) {
        if (!path) Begin(x, y);
        else FPDFPath_MoveTo(path, (float)x, (float)y);
    }
    void Line(double x, double y) { FPDFPath_LineTo(path, (float)x, (float)y); }
    void Curve(double x1, double y1, double x2, double y2, double x3, double y3) {
        FPDFPath_BezierTo(path, (float)x1, (float)y1, (float)x2, (float)y2, (float)x3, (float)y3);
    }
    void Close() { FPDFPath_Close(path); }
    void Fill() { FPDFPath_SetDrawMode(path, FPDF_FILLMODE_WINDING, 1); }
    void End() { path = nullptr; }
    void Seg(double x1, double y1, double x2, double y2) { Move(x1, y1); Line(x2, y2); }
    void Ellipse(double cx, double cy, double rx, double ry) {
        const double c = 0.5523;
        Move(cx + rx, cy);
        Curve(cx + rx, cy + ry * c, cx + rx * c, cy + ry, cx, cy + ry);
        Curve(cx - rx * c, cy + ry, cx - rx, cy + ry * c, cx - rx, cy);
        Curve(cx - rx, cy - ry * c, cx - rx * c, cy - ry, cx, cy - ry);
        Curve(cx + rx * c, cy - ry, cx + rx, cy - ry * c, cx + rx, cy);
    }
    void Check(double x, double k) { Move(x + 3 * k, 10 * k); Line(x + 8 * k, 4.5 * k); Line(x + 17 * k, 16.5 * k); }
    void Tilde(double y, double w) {
        Move(3, y);
        Curve(w * 0.33, y + 4, w * 0.5, y - 1, w * 0.5, y);
        Curve(w * 0.5, y + 1, w * 0.67, y - 4, w - 3, y);
    }
    // Centred bold text in the box (0,0)-(w,h).
    void Text(const std::string& s, double size, double w, double h) {
        End();
        FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Helvetica-Bold");
        FPDF_PAGEOBJECT t = FPDFPageObj_CreateTextObj(doc, font, (float)size);
        std::vector<unsigned short> u(s.begin(), s.end());
        u.push_back(0);
        FPDFText_SetText(t, u.data());
        FPDFPageObj_SetFillColor(t, r, g, b, 255);
        float l, bo, ri, to;
        FPDFPageObj_GetBounds(t, &l, &bo, &ri, &to);
        // Cap height of Helvetica is ~0.72 em; centre on that, not on the glyph bounds.
        double tx = (w - (ri - l)) / 2 - l, ty = (h - size * 0.72) / 2;
        FPDFPageObj_Transform(t, 1, 0, 0, 1, tx, ty);
        objs.push_back(t);
        FPDFFont_Close(font);
    }
};

typedef void (*DrawFn)(Canvas& c, double w, double h);

static void DrawCheck(Canvas& c, double, double) { c.Check(0, 1); }
static void DrawDoubleCheck(Canvas& c, double, double) { c.Check(0, 0.85); c.Check(7, 0.85); }
static void DrawCircledCheck(Canvas& c, double w, double h) {
    c.Ellipse(w / 2, h / 2, w / 2 - 1.6, h / 2 - 1.6);
    c.End();
    c.width = 1.6;
    c.Move(w * 0.27, h * 0.52); c.Line(w * 0.43, h * 0.33); c.Line(w * 0.73, h * 0.70);
}
static void DrawPlus(Canvas& c, double w, double h) { c.Seg(w / 2, 3, w / 2, h - 3); c.Seg(3, h / 2, w - 3, h / 2); }
static void DrawMinus(Canvas& c, double w, double h) { c.Seg(3, h / 2, w - 3, h / 2); }
static void DrawEquals(Canvas& c, double w, double h) { c.Seg(3, h / 2 + 3.5, w - 3, h / 2 + 3.5); c.Seg(3, h / 2 - 3.5, w - 3, h / 2 - 3.5); }
static void DrawApprox(Canvas& c, double w, double h) { c.Tilde(h / 2 + 3.2, w); c.Tilde(h / 2 - 3.2, w); }
static void DrawNotEqual(Canvas& c, double w, double h) { DrawEquals(c, w, h); c.Seg(w * 0.68, h - 2.5, w * 0.32, 2.5); }
static void DrawX(Canvas& c, double w, double h) { c.Seg(4, 4, w - 4, h - 4); c.Seg(4, h - 4, w - 4, 4); }
static void DrawArrow(Canvas& c, double w, double h) {
    c.Seg(2, h / 2, w - 4, h / 2);
    c.Move(w - 9, h / 2 + 5); c.Line(w - 2.5, h / 2); c.Line(w - 9, h / 2 - 5);
}
static void DrawStar(Canvas& c, double w, double h) {
    const double pi = 3.14159265358979;
    double cx = w / 2, cy = h / 2 - 0.6, ro = w / 2 - 1.2, ri = ro * 0.42;
    c.width = 1.4;
    for (int i = 0; i < 10; ++i) {
        double r = (i % 2 == 0) ? ro : ri, a = pi / 2 + i * pi / 5;
        if (i == 0) c.Move(cx + r * cos(a), cy + r * sin(a));
        else c.Line(cx + r * cos(a), cy + r * sin(a));
    }
    c.Close();
    c.Fill();
}
static void DrawRing(Canvas& c, double w, double h) { c.Ellipse(w / 2, h / 2, w / 2 - 1.5, h / 2 - 1.5); }

struct Symbol {
    const char* id;
    const wchar_t* label;
    DrawFn draw;       // vector drawing, or null for a text mark
    const char* text;  // text mark content
    double w, h;       // box at Medium size (text marks size themselves)
};

static const Symbol kSymbols[] = {
    {"check", L"Check", DrawCheck, nullptr, 20, 20},
    {"dblcheck", L"Double check", DrawDoubleCheck, nullptr, 26, 20},
    {"circcheck", L"Circled check", DrawCircledCheck, nullptr, 20, 20},
    {"plus", L"Plus", DrawPlus, nullptr, 20, 20},
    {"minus", L"Minus", DrawMinus, nullptr, 20, 20},
    {"equals", L"Equals", DrawEquals, nullptr, 20, 20},
    {"approx", L"Approximately equal", DrawApprox, nullptr, 20, 20},
    {"notequal", L"Not equal", DrawNotEqual, nullptr, 20, 20},
    {"footed", L"Footed (F)", nullptr, "F", 0, 20},
    {"crossfooted", L"Cross-footed (CF)", nullptr, "CF", 0, 20},
    {"question", L"Question (?)", nullptr, "?", 0, 20},
    {"exclaim", L"Exclamation (!)", nullptr, "!", 0, 20},
    {"x", L"X", DrawX, nullptr, 20, 20},
    {"ok", L"OK", nullptr, "OK", 0, 20},
    {"na", L"N/A", nullptr, "N/A", 0, 20},
    {"py", L"PY: agrees to prior year", nullptr, "PY", 0, 20},
    {"tb", L"TB: agrees to trial balance", nullptr, "TB", 0, 20},
    {"conf", L"C: confirmed", nullptr, "C", 0, 20},
    {"arrow", L"Arrow", DrawArrow, nullptr, 26, 16},
    {"star", L"Star", DrawStar, nullptr, 20, 20},
    {"circle", L"Circle (around an amount)", DrawRing, nullptr, 64, 24},
};

int SymbolCount() { return (int)(sizeof(kSymbols) / sizeof(kSymbols[0])); }
const char* SymbolId(int i) { return kSymbols[std::clamp(i, 0, SymbolCount() - 1)].id; }
const wchar_t* SymbolLabel(int i) { return kSymbols[std::clamp(i, 0, SymbolCount() - 1)].label; }

// Helvetica-Bold advance widths (1/1000 em) for ASCII 32..126.
static const short kHelvBold[95] = {
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
    556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611,
    611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584};
static double TextWidth(const std::string& s, double size) {
    double w = 0;
    for (unsigned char c : s) w += (c >= 32 && c < 127) ? kHelvBold[c - 32] : 556;
    return w * size / 1000.0;
}

static double SymbolTextSize(const char* t) { return (t[1] == 0 && (t[0] == '?' || t[0] == '!')) ? 15 : 13; }
static double CircledTextSize(const std::string& t) { return t.size() > 1 ? 9.5 : 11.5; }

void BaseSize(Kind kind, int symbol, const std::string& text, double& w, double& h) {
    if (kind == kSymbol) {
        const Symbol& s = kSymbols[std::clamp(symbol, 0, SymbolCount() - 1)];
        h = s.h;
        w = s.draw ? s.w : std::max(20.0, TextWidth(s.text, SymbolTextSize(s.text)) + 6);
    } else {
        h = 20;
        w = std::max(20.0, TextWidth(text, CircledTextSize(text)) + 9);
    }
}

static std::string PdfDate() {
    time_t now = time(nullptr);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "D:%Y%m%d%H%M%S", &lt);
    return buf;
}

static std::u16string U16(const std::string& s) { return std::u16string(s.begin(), s.end()); }

bool AddMark(FPDF_DOCUMENT doc, FPDF_PAGE page, Kind kind, int symbol, const std::string& text, int color,
             double left, double bottom, double right, double top, int rotation, const std::u16string& author,
             const std::string& nm) {
    double w, h;
    BaseSize(kind, symbol, text, w, h);
    Canvas c{doc};
    ColorRgb(color, c.r, c.g, c.b);
    std::string id;
    if (kind == kSymbol) {
        const Symbol& s = kSymbols[std::clamp(symbol, 0, SymbolCount() - 1)];
        id = s.id;
        if (s.draw) s.draw(c, w, h);
        else c.Text(s.text, SymbolTextSize(s.text), w, h);
    } else {
        id = kind == kNumber ? "number" : "letter";
        // Circled, like a hand-drawn reference.
        c.width = 1.4;
        c.Ellipse(w / 2, h / 2, w / 2 - 1.2, h / 2 - 1.2);
        c.Text(text, CircledTextSize(text), w, h);
    }
    if (c.objs.empty()) return false;

    // Local box (0..w, 0..h) -> page box, turned so the mark is upright on a rotated page.
    int q = ((rotation % 4) + 4) % 4;
    double bw = right - left, bh = top - bottom;
    double sx = (q % 2 == 0) ? bw / w : bw / h, sy = (q % 2 == 0) ? bh / h : bh / w;
    double s = std::min(sx, sy);
    // Rotation counter-clockwise by q quarter turns, applied after scaling.
    double a = 0, b = 0, cc = 0, d = 0;
    switch (q) {
    case 0: a = s; d = s; break;
    case 1: b = s; cc = -s; break;
    case 2: a = -s; d = -s; break;
    case 3: b = -s; cc = s; break;
    }
    // Where the local box lands, so it can be shifted onto the page box's centre.
    double xs[4] = {0, w, 0, w}, ys[4] = {0, 0, h, h};
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    for (int i = 0; i < 4; i++) {
        double px = a * xs[i] + cc * ys[i], py = b * xs[i] + d * ys[i];
        minx = std::min(minx, px); maxx = std::max(maxx, px);
        miny = std::min(miny, py); maxy = std::max(maxy, py);
    }
    double e = (left + right) / 2 - (minx + maxx) / 2, f = (bottom + top) / 2 - (miny + maxy) / 2;

    FPDF_ANNOTATION annot = FPDFPage_CreateAnnot(page, FPDF_ANNOT_STAMP);
    if (!annot) {
        for (auto o : c.objs) FPDFPageObj_Destroy(o);
        return false;
    }
    FS_RECTF rect = {(float)(minx + e), (float)(maxy + f), (float)(maxx + e), (float)(miny + f)};
    FPDFAnnot_SetRect(annot, &rect);
    for (auto o : c.objs) {
        // Line widths scale with the mark, as they would in a scaled drawing.
        float lw;
        if (FPDFPageObj_GetStrokeWidth(o, &lw)) FPDFPageObj_SetStrokeWidth(o, (float)(lw * s));
        FPDFPageObj_Transform(o, a, b, cc, d, e, f);
        FPDFAnnot_AppendObject(annot, o);
    }
    std::string now = PdfDate();
    FPDFAnnot_SetStringValue(annot, "T", (FPDF_WIDESTRING)author.c_str());
    FPDFAnnot_SetStringValue(annot, "Subj", (FPDF_WIDESTRING)u"Tickmark");
    FPDFAnnot_SetStringValue(annot, "M", (FPDF_WIDESTRING)U16(now).c_str());
    FPDFAnnot_SetStringValue(annot, "CreationDate", (FPDF_WIDESTRING)U16(now).c_str());
    FPDFAnnot_SetStringValue(annot, "NM", (FPDF_WIDESTRING)U16(nm).c_str());
    FPDFAnnot_SetFlags(annot, FPDF_ANNOT_FLAG_PRINT);
    FPDFPage_CloseAnnot(annot);
    return true;
}

bool IsMark(FPDF_ANNOTATION annot) {
    if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_STAMP) return false;
    unsigned short buf[64] = {};
    unsigned long n = FPDFAnnot_GetStringValue(annot, "NM", buf, sizeof(buf));
    if (n < 14) return false;
    const char* pre = "wpp-tm-";
    for (int i = 0; i < 7; i++) if (buf[i] != (unsigned short)pre[i]) return false;
    return true;
}

}  // namespace marks
