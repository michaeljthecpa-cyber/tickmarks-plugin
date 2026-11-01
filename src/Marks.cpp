// Tickmarks: drawing the marks and adding them to the page as standard Stamp annotations.
// Each mark carries its own appearance stream, so it shows and prints on any PDF viewer.

#include "Tickmarks.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>

/*-------------------------------------------------------
	Tables
-------------------------------------------------------*/

struct Rgb { const char *name; double r, g, b; };
static const Rgb kColors[] = {
    { "Red",    0.86, 0.08, 0.08 },
    { "Blue",   0.00, 0.32, 0.90 },
    { "Green",  0.00, 0.55, 0.18 },
    { "Black",  0.00, 0.00, 0.00 },
    { "Purple", 0.50, 0.12, 0.68 },
    { "Orange", 0.95, 0.47, 0.00 },
};
static const char  *kSizeNames[]  = { "Small", "Medium", "Large" };
static const double kSizeScales[] = { 0.75, 1.0, 1.4 };

static const double kLineWidth = 2.2;
static const double kTextSize  = 13.0;

// Helvetica-Bold advance widths (1/1000 em) for ASCII 32..126.
static const short kHelvBoldWidths[95] = {
    278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,556,556,556,556,556,556,556,556,
    556,556,333,333,584,584,584,611,975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,333,556,611,556,611,556,333,611,
    611,278,278,556,278,889,611,611,611,611,389,556,333,611,556,778,556,556,500,389,280,389,584 };

static double TextWidth(const std::string &s, double size)
{
    double w = 0;
    for (unsigned char c : s)
        w += (c >= 32 && c < 127) ? kHelvBoldWidths[c - 32] : 556;
    return w * size / 1000.0;
}

static std::string N(double v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

/*-------------------------------------------------------
	Drawings, in a base box of w x h points at Medium size.
	Colour, line width and caps are already set by the caller.
-------------------------------------------------------*/

typedef std::string (*DrawProc)(double w, double h);

static std::string CheckPath(double x, double k)
{
    return N(x + 3 * k) + " " + N(10 * k) + " m " + N(x + 8 * k) + " " + N(4.5 * k) + " l " +
           N(x + 17 * k) + " " + N(16.5 * k) + " l S ";
}

static std::string Ellipse(double cx, double cy, double rx, double ry)
{
    const double c = 0.5523;
    return N(cx + rx) + " " + N(cy) + " m " +
        N(cx + rx) + " " + N(cy + ry * c) + " " + N(cx + rx * c) + " " + N(cy + ry) + " " + N(cx) + " " + N(cy + ry) + " c " +
        N(cx - rx * c) + " " + N(cy + ry) + " " + N(cx - rx) + " " + N(cy + ry * c) + " " + N(cx - rx) + " " + N(cy) + " c " +
        N(cx - rx) + " " + N(cy - ry * c) + " " + N(cx - rx * c) + " " + N(cy - ry) + " " + N(cx) + " " + N(cy - ry) + " c " +
        N(cx + rx * c) + " " + N(cy - ry) + " " + N(cx + rx) + " " + N(cy - ry * c) + " " + N(cx + rx) + " " + N(cy) + " c S ";
}

static std::string Tilde(double y, double w)
{
    return "3 " + N(y) + " m " + N(w * 0.33) + " " + N(y + 4) + " " + N(w * 0.5) + " " + N(y - 1) + " " + N(w * 0.5) + " " + N(y) + " c " +
           N(w * 0.5) + " " + N(y + 1) + " " + N(w * 0.67) + " " + N(y - 4) + " " + N(w - 3) + " " + N(y) + " c S ";
}

static std::string DrawCheck(double, double)        { return CheckPath(0, 1); }
static std::string DrawDoubleCheck(double, double)  { return CheckPath(0, 0.85) + CheckPath(7, 0.85); }
static std::string DrawCircledCheck(double w, double h)
{
    return Ellipse(w / 2, h / 2, w / 2 - 1.6, h / 2 - 1.6) + "q 1.6 w " + N(w * 0.27) + " " + N(h * 0.52) + " m " +
           N(w * 0.43) + " " + N(h * 0.33) + " l " + N(w * 0.73) + " " + N(h * 0.70) + " l S Q ";
}
static std::string DrawPlus(double w, double h)
{
    return N(w / 2) + " 3 m " + N(w / 2) + " " + N(h - 3) + " l 3 " + N(h / 2) + " m " + N(w - 3) + " " + N(h / 2) + " l S ";
}
static std::string DrawMinus(double w, double h) { return "3 " + N(h / 2) + " m " + N(w - 3) + " " + N(h / 2) + " l S "; }
static std::string DrawEquals(double w, double h)
{
    return "3 " + N(h / 2 + 3.5) + " m " + N(w - 3) + " " + N(h / 2 + 3.5) + " l 3 " + N(h / 2 - 3.5) + " m " +
           N(w - 3) + " " + N(h / 2 - 3.5) + " l S ";
}
static std::string DrawApprox(double w, double h) { return Tilde(h / 2 + 3.2, w) + Tilde(h / 2 - 3.2, w); }
static std::string DrawNotEqual(double w, double h)
{
    return DrawEquals(w, h) + N(w * 0.68) + " " + N(h - 2.5) + " m " + N(w * 0.32) + " 2.5 l S ";
}
static std::string DrawX(double w, double h)
{
    return "4 4 m " + N(w - 4) + " " + N(h - 4) + " l 4 " + N(h - 4) + " m " + N(w - 4) + " 4 l S ";
}
static std::string DrawArrow(double w, double h)
{
    return "2 " + N(h / 2) + " m " + N(w - 4) + " " + N(h / 2) + " l S " + N(w - 9) + " " + N(h / 2 + 5) + " m " +
           N(w - 2.5) + " " + N(h / 2) + " l " + N(w - 9) + " " + N(h / 2 - 5) + " l S ";
}
static std::string DrawStar(double w, double h)
{
    const double pi = 3.14159265358979;
    double cx = w / 2, cy = h / 2 - 0.6, ro = w / 2 - 1.2, ri = ro * 0.42;
    std::string s = "q 1.4 w 1 j ";
    for (int i = 0; i < 10; ++i) {
        double r = (i % 2 == 0) ? ro : ri, a = pi / 2 + i * pi / 5;
        s += N(cx + r * cos(a)) + " " + N(cy + r * sin(a)) + (i == 0 ? " m " : " l ");
    }
    return s + "h B Q ";
}
static std::string DrawRing(double w, double h) { return Ellipse(w / 2, h / 2, w / 2 - 1.5, h / 2 - 1.5); }

struct Symbol {
    const char *id;
    const char *label;
    DrawProc    draw;    // vector drawing, or NULL for a text mark
    const char *text;    // text mark content
    double      w, h;    // base box at Medium size (text marks size themselves)
};

static const Symbol kSymbols[] = {
    { "check",       "Check",                       DrawCheck,        NULL,  20, 20 },
    { "dblcheck",    "Double Check",                DrawDoubleCheck,  NULL,  26, 20 },
    { "circcheck",   "Circled Check",               DrawCircledCheck, NULL,  20, 20 },
    { "plus",        "Plus",                        DrawPlus,         NULL,  20, 20 },
    { "minus",       "Minus",                       DrawMinus,        NULL,  20, 20 },
    { "equals",      "Equals",                      DrawEquals,       NULL,  20, 20 },
    { "approx",      "Approximately Equal",         DrawApprox,       NULL,  20, 20 },
    { "notequal",    "Not Equal",                   DrawNotEqual,     NULL,  20, 20 },
    { "footed",      "Footed (F)",                  NULL,             "F",   0,  20 },
    { "crossfooted", "Cross-footed (CF)",           NULL,             "CF",  0,  20 },
    { "question",    "Question (?)",                NULL,             "?",   0,  20 },
    { "exclaim",     "Exclamation (!)",             NULL,             "!",   0,  20 },
    { "x",           "X",                           DrawX,            NULL,  20, 20 },
    { "ok",          "OK",                          NULL,             "OK",  0,  20 },
    { "na",          "N/A",                         NULL,             "N/A", 0,  20 },
    { "py",          "PY - Agrees to Prior Year",   NULL,             "PY",  0,  20 },
    { "tb",          "TB - Agrees to Trial Balance", NULL,            "TB",  0,  20 },
    { "conf",        "C - Confirmed",               NULL,             "C",   0,  20 },
    { "arrow",       "Arrow",                       DrawArrow,        NULL,  26, 16 },
    { "star",        "Star",                        DrawStar,         NULL,  20, 20 },
    { "circle",      "Circle (around an amount)",   DrawRing,         NULL,  64, 24 },
};

int         SymbolCount()        { return (int)(sizeof kSymbols / sizeof kSymbols[0]); }
const char *SymbolLabel(int i)   { return kSymbols[i].label; }
int         ColorCount()         { return (int)(sizeof kColors / sizeof kColors[0]); }
const char *ColorName(int i)     { return kColors[i].name; }
const char *SizeName(int i)      { return kSizeNames[i]; }

/*-------------------------------------------------------
	Annotation helpers
-------------------------------------------------------*/

static std::string PdfDateNow()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    snprintf(buf, sizeof buf, "D:%04d%02d%02d%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

static CosObj NewNumberArray(CosDoc cd, const double *v, int n)
{
    CosObj arr = CosNewArray(cd, false, n);
    for (int i = 0; i < n; ++i)
        CosArrayInsert(arr, i, CosNewFloat(cd, false, (float)v[i]));
    return arr;
}

static void PutString(CosObj dict, const char *key, const std::string &s)
{
    CosDoc cd = CosObjGetDoc(dict);
    CosDictPutKeyString(dict, key, CosNewString(cd, false, s.c_str(), (ASTArraySize)s.size()));
}

// Sets the annotation's author to the Acrobat identity name (Preferences > Identity).
static void SetAuthor(PDAnnot annot)
{
    ASText name = ASTextNew();
    AVIdentityGetText(kAVIName, name);
    if (ASTextIsEmpty(name))
        AVIdentityGetText(kAVILoginName, name);
    if (!ASTextIsEmpty(name))
        PDAnnotSetTitleASText(annot, name);
    ASTextDestroy(name);
}

// Form XObject holding the mark's drawing. 'rotate' turns it so it reads upright on rotated pages.
static CosObj NewAppearance(CosDoc cd, const std::string &ops, double w, double h, int rotate)
{
    ASStm stm = ASMemStmRdOpen(ops.c_str(), (ASArraySize)ops.size());
    CosObj attrs = CosNewDict(cd, false, 1);
    CosDictPutKeyString(attrs, "Length", CosNewInteger(cd, false, (ASInt32)ops.size()));
    CosObj ap = CosNewStream(cd, true, stm, 0, true, attrs, CosNewNull(), (CosByteMax)ops.size());
    ASStmClose(stm);

    CosObj d = CosStreamDict(ap);
    CosDictPutKeyString(d, "Type", CosNewNameFromString(cd, false, "XObject"));
    CosDictPutKeyString(d, "Subtype", CosNewNameFromString(cd, false, "Form"));
    CosDictPutKeyString(d, "FormType", CosNewInteger(cd, false, 1));
    double bbox[4] = { 0, 0, w, h };
    CosDictPutKeyString(d, "BBox", NewNumberArray(cd, bbox, 4));
    if (rotate) {
        double a = rotate * 3.14159265358979 / 180.0;
        double m[6] = { cos(a), sin(a), -sin(a), cos(a), 0, 0 };
        for (double &v : m) if (fabs(v) < 1e-9) v = 0;
        CosDictPutKeyString(d, "Matrix", NewNumberArray(cd, m, 6));
    }

    CosObj font = CosNewDict(cd, false, 4);
    CosDictPutKeyString(font, "Type", CosNewNameFromString(cd, false, "Font"));
    CosDictPutKeyString(font, "Subtype", CosNewNameFromString(cd, false, "Type1"));
    CosDictPutKeyString(font, "BaseFont", CosNewNameFromString(cd, false, "Helvetica-Bold"));
    CosDictPutKeyString(font, "Encoding", CosNewNameFromString(cd, false, "WinAnsiEncoding"));
    CosObj fonts = CosNewDict(cd, false, 1);
    CosDictPutKeyString(fonts, "HeBo", font);
    CosObj res = CosNewDict(cd, false, 1);
    CosDictPutKeyString(res, "Font", fonts);
    CosDictPutKeyString(d, "Resources", res);
    return ap;
}

static void Redraw(AVPageView pageView)
{
    AVPageViewInvalidateRect(pageView, NULL);
    AVPageViewDrawNow(pageView);
}

/*-------------------------------------------------------
	Placing marks
-------------------------------------------------------*/

bool PlaceMark(AVPageView pageView, AVDevCoord x, AVDevCoord y, MarkMode mode)
{
    bool placed = false;
    DURING
        PDPage page = AVPageViewGetPage(pageView);
        PDDoc pdDoc = PDPageGetDoc(page);
        CosDoc cd = PDDocGetCosDoc(pdDoc);

        // What to draw, in the Medium-size base box.
        std::string id, label, body;
        double w = 20, h = 20;
        if (mode == kModeSymbol) {
            const Symbol &s = kSymbols[(gSettings.symbol >= 0 && gSettings.symbol < SymbolCount()) ? gSettings.symbol : 0];
            id = s.id;
            label = s.label;
            if (s.draw) {
                w = s.w; h = s.h;
                body = s.draw(w, h);
            } else {
                double size = (s.text[1] == 0 && (s.text[0] == '?' || s.text[0] == '!')) ? 15 : kTextSize;
                double tw = TextWidth(s.text, size);
                w = tw + 6 > 20 ? tw + 6 : 20;
                body = "BT /HeBo " + N(size) + " Tf " + N((w - tw) / 2) + " " + N((h - size * 0.72) / 2) + " Td (" + s.text + ") Tj ET ";
            }
        } else {
            std::string txt;
            if (mode == kModeNumber) {
                txt = std::to_string(gSettings.nextNumber);
                id = "number";
                label = "Number " + txt;
            } else {
                txt = std::string(1, (char)('A' + gSettings.nextLetter));
                id = "letter";
                label = "Letter " + txt;
            }
            double size = 14, tw = TextWidth(txt, size);
            w = tw + 7 > 22 ? tw + 7 : 22;
            body = "BT /HeBo " + N(size) + " Tf " + N((w - tw) / 2) + " " + N((h - size * 0.72) / 2) + " Td (" + txt + ") Tj ET ";
        }

        const Rgb &c = kColors[(gSettings.color >= 0 && gSettings.color < ColorCount()) ? gSettings.color : 0];
        double scale = kSizeScales[(gSettings.size >= 0 && gSettings.size < 3) ? gSettings.size : 1];
        std::string rgb = N(c.r) + " " + N(c.g) + " " + N(c.b);
        std::string ops = "q " + N(scale) + " 0 0 " + N(scale) + " 0 0 cm " + N(kLineWidth) + " w 1 J 1 j " +
                          rgb + " RG " + rgb + " rg " + body + "Q";

        double W = w * scale, H = h * scale;
        int rotate = (int)PDPageGetRotate(page);
        bool sideways = (rotate == 90 || rotate == 270);

        ASFixedPoint pt;
        AVPageViewDevicePointToPage(pageView, x, y, &pt);
        double px = ASFixedToFloat(pt.h), py = ASFixedToFloat(pt.v);
        double rw = sideways ? H : W, rh = sideways ? W : H;
        ASFixedRect rect;
        rect.left   = FloatToASFixed(px - rw / 2);
        rect.right  = FloatToASFixed(px + rw / 2);
        rect.bottom = FloatToASFixed(py - rh / 2);
        rect.top    = FloatToASFixed(py + rh / 2);

        PDAnnot annot = PDPageAddNewAnnot(page, -2, ASAtomFromString("Stamp"), &rect);
        CosObj a = PDAnnotGetCosObj(annot);
        CosDictPutKeyString(a, "F", CosNewInteger(cd, false, 4));          // print
        CosDictPutKeyString(a, "Name", CosNewNameFromString(cd, false, ("Tickmark_" + id).c_str()));
        PutString(a, "Subj", "Tickmark");
        PutString(a, "Contents", label);
        std::string now = PdfDateNow();
        PutString(a, "CreationDate", now);
        PutString(a, "M", now);
        double col[3] = { c.r, c.g, c.b };
        CosDictPutKeyString(a, "C", NewNumberArray(cd, col, 3));
        CosObj apDict = CosNewDict(cd, false, 1);
        CosDictPutKeyString(apDict, "N", NewAppearance(cd, ops, W, H, rotate));
        CosDictPutKeyString(a, "AP", apDict);
        SetAuthor(annot);

        PDDocSetFlags(pdDoc, PDDocNeedsSave);
        Redraw(pageView);
        placed = true;
    HANDLER
        char msg[256];
        ASGetErrorString(ERRORCODE, msg, sizeof msg);
        AVAlertNote(msg);
    END_HANDLER

    if (placed && gSettings.autoIncrement) {
        if (mode == kModeNumber) {
            if (gSettings.nextNumber < kMaxNumber) ++gSettings.nextNumber;
            else if (gSettings.wrap) gSettings.nextNumber = 1;
            SaveSettings();
        } else if (mode == kModeLetter) {
            if (gSettings.nextLetter < 25) ++gSettings.nextLetter;
            else if (gSettings.wrap) gSettings.nextLetter = 0;
            SaveSettings();
        }
    }
    return placed;
}

bool PlaceNote(AVPageView pageView, AVDevCoord x, AVDevCoord y)
{
    bool placed = false;
    DURING
        PDPage page = AVPageViewGetPage(pageView);
        PDDoc pdDoc = PDPageGetDoc(page);
        CosDoc cd = PDDocGetCosDoc(pdDoc);

        ASFixedPoint pt;
        AVPageViewDevicePointToPage(pageView, x, y, &pt);
        double px = ASFixedToFloat(pt.h), py = ASFixedToFloat(pt.v);

        ASFixedRect rect;
        rect.left = FloatToASFixed(px);      rect.right = FloatToASFixed(px + 20);
        rect.bottom = FloatToASFixed(py - 20); rect.top = FloatToASFixed(py);
        PDAnnot note = PDPageAddNewAnnot(page, -2, ASAtomFromString("Text"), &rect);
        CosObj n = PDAnnotGetCosObj(note);
        CosDictPutKeyString(n, "Name", CosNewNameFromString(cd, false, "Comment"));
        CosDictPutKeyString(n, "F", CosNewInteger(cd, false, 4 | 8 | 16));   // print, no zoom, no rotate
        double yellow[3] = { 1.0, 0.82, 0.0 };
        CosDictPutKeyString(n, "C", NewNumberArray(cd, yellow, 3));
        std::string now = PdfDateNow();
        PutString(n, "CreationDate", now);
        PutString(n, "M", now);
        PutString(n, "Contents", "");
        SetAuthor(note);

        ASFixedRect prect;
        prect.left = FloatToASFixed(px + 24);  prect.right = FloatToASFixed(px + 224);
        prect.bottom = FloatToASFixed(py - 120); prect.top = FloatToASFixed(py);
        PDAnnot popup = PDPageAddNewAnnot(page, -2, ASAtomFromString("Popup"), &prect);
        CosObj p = PDAnnotGetCosObj(popup);
        CosDictPutKeyString(p, "Parent", n);
        CosDictPutKeyString(p, "Open", CosNewBoolean(cd, false, true));
        CosDictPutKeyString(n, "Popup", p);
        CosDictPutKeyString(n, "Open", CosNewBoolean(cd, false, true));

        PDDocSetFlags(pdDoc, PDDocNeedsSave);
        Redraw(pageView);
        placed = true;
    HANDLER
        char msg[256];
        ASGetErrorString(ERRORCODE, msg, sizeof msg);
        AVAlertNote(msg);
    END_HANDLER
    return placed;
}
