#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "textedit.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>
#ifdef TEXTEDIT_DEBUG
#include <cstdio>
#endif
#include "fpdf_edit.h"
#include "fpdf_text.h"

namespace textedit {

namespace {

const double kPi = 3.14159265358979323846;

bool Contains(const std::string& hay, const char* needle) {
    std::string a = hay, b = needle;
    for (auto& c : a) c = (char)std::tolower((unsigned char)c);
    for (auto& c : b) c = (char)std::tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

bool IsBreak(unsigned int c) { return c == '\r' || c == '\n' || c == 0xFFFE || c == 2; }

std::u16string GetObjText(FPDF_PAGEOBJECT obj, FPDF_TEXTPAGE tp) {
    unsigned long n = FPDFTextObj_GetText(obj, tp, nullptr, 0);
    if (n < 2) return u"";
    std::vector<unsigned short> buf(n / 2 + 1);
    FPDFTextObj_GetText(obj, tp, buf.data(), n);
    std::u16string s;
    for (unsigned short c : buf) {
        if (!c) break;
        s.push_back((char16_t)c);
    }
    // Text extraction may add a space or line break next to neighbouring text.
    while (!s.empty() && (s.back() == u' ' || s.back() == u'\r' || s.back() == u'\n')) s.pop_back();
    while (!s.empty() && (s.front() == u' ' || s.front() == u'\r' || s.front() == u'\n')) s.erase(s.begin());
    return s;
}

std::vector<std::u16string> SplitLines(const std::u16string& text) {
    std::vector<std::u16string> out(1);
    for (char16_t c : text) {
        if (c == u'\r') continue;
        if (c == u'\n') out.emplace_back();
        else out.back().push_back(c == u'\t' ? u' ' : c);
    }
    return out;
}

// Creates a text object in a standard font; null on failure.
FPDF_PAGEOBJECT NewStandardText(FPDF_DOCUMENT doc, const std::u16string& s, double size, int family, bool bold,
                                bool italic) {
    FPDF_PAGEOBJECT obj = FPDFPageObj_NewTextObj(doc, StandardFont(family, bold, italic), (float)size);
    if (!obj) return nullptr;
    std::u16string z = s;
    z.push_back(0);
    if (!FPDFText_SetText(obj, (FPDF_WIDESTRING)z.c_str())) {
        FPDFPageObj_Destroy(obj);
        return nullptr;
    }
    return obj;
}

// Width of a text object before it is placed (its bounds at the origin).
double ObjWidth(FPDF_PAGEOBJECT obj) {
    float l, b, r, t;
    if (!FPDFPageObj_GetBounds(obj, &l, &b, &r, &t)) return 0;
    return r - l;
}

// Places `obj` with its baseline origin at (x, y), turned by `angle` and stretched sideways by `hscale`.
void Place(FPDF_PAGEOBJECT obj, double x, double y, double angle, double hscale) {
    double c = std::cos(angle), s = std::sin(angle);
    FPDFPageObj_Transform(obj, hscale * c, hscale * s, -s, c, x, y);
}

}  // namespace

const char* StandardFont(int family, bool bold, bool italic) {
    static const char* names[3][4] = {
        {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"},
        {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"},
        {"Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"},
    };
    int f = std::clamp(family, 0, 2);
    return names[f][(bold ? 1 : 0) + (italic ? 2 : 0)];
}

static bool LineAtTP(FPDF_TEXTPAGE tp, double x, double y, double tol, Line& out) {
    int n = FPDFText_CountChars(tp);
    int hit = FPDFText_GetCharIndexAtPos(tp, x, y, tol, tol);
    // Clicking a space or line end: step to a neighbouring real character.
    if (hit >= 0) {
        unsigned int c = FPDFText_GetUnicode(tp, hit);
        if (IsBreak(c) || c == ' ') {
            if (hit > 0 && !IsBreak(FPDFText_GetUnicode(tp, hit - 1))) hit--;
            else if (hit + 1 < n && !IsBreak(FPDFText_GetUnicode(tp, hit + 1))) hit++;
        }
    }
    if (hit < 0 || hit >= n || IsBreak(FPDFText_GetUnicode(tp, hit))) {
        return false;
    }
    double size = FPDFText_GetFontSize(tp, hit);
    if (size < 1) size = 10;
    float angle = FPDFText_GetCharAngle(tp, hit);
    double ca = std::cos(angle), sa = std::sin(angle);
    double hx, hy;
    FPDFText_GetCharOrigin(tp, hit, &hx, &hy);
    // Position of a character along the line direction and across it, relative to the clicked one.
    auto along = [&](int i, double& a, double& across) {
        double ox, oy;
        FPDFText_GetCharOrigin(tp, i, &ox, &oy);
        a = (ox - hx) * ca + (oy - hy) * sa;
        across = -(ox - hx) * sa + (oy - hy) * ca;
    };
    auto sameRun = [&](int i, int prev) {
        unsigned int c = FPDFText_GetUnicode(tp, i);
        if (IsBreak(c)) return false;
        if (std::fabs(FPDFText_GetCharAngle(tp, i) - angle) > 0.05) return false;
        double a, ac, pa, pac;
        along(i, a, ac);
        along(prev, pa, pac);
        if (std::fabs(ac) > size * 0.5) return false;
        // A wide gap ends the run, so one cell of a table row is picked, not the whole row.
        double gap = std::fabs(a - pa);
        FS_RECTF pb;
        double pw = FPDFText_GetLooseCharBox(tp, prev, &pb) ? std::fabs((pb.right - pb.left) * ca + (pb.top - pb.bottom) * sa) : 0;
        return gap - pw < size * 1.6;
    };
    int first = hit, last = hit;
    while (first > 0 && sameRun(first - 1, first)) first--;
    while (last + 1 < n && sameRun(last + 1, last)) last++;
    // Trim spaces at the ends.
    while (first < last && FPDFText_GetUnicode(tp, first) == ' ') first++;
    while (last > first && FPDFText_GetUnicode(tp, last) == ' ') last--;

    out = Line();
    out.first = first;
    out.count = last - first + 1;
    out.size = size;
    out.angle = angle;
    FPDFText_GetCharOrigin(tp, first, &out.ox, &out.oy);
    bool any = false;
    for (int i = first; i <= last; i++) {
        unsigned int c = FPDFText_GetUnicode(tp, i);
        out.text.push_back(c > 0xFFFF ? u'?' : (char16_t)c);
        FS_RECTF r;
        if (!FPDFText_GetLooseCharBox(tp, i, &r)) continue;
        double l = std::min(r.left, r.right), rr = std::max(r.left, r.right);
        double b = std::min(r.bottom, r.top), t = std::max(r.bottom, r.top);
        if (!any) { out.left = l; out.right = rr; out.bottom = b; out.top = t; any = true; }
        else {
            out.left = std::min(out.left, l); out.right = std::max(out.right, rr);
            out.bottom = std::min(out.bottom, b); out.top = std::max(out.top, t);
        }
    }
    if (!any) {
        return false;
    }
    unsigned int r, g, b, a;
    if (FPDFText_GetFillColor(tp, hit, &r, &g, &b, &a)) { out.r = (uint8_t)r; out.g = (uint8_t)g; out.b = (uint8_t)b; }
    char name[256] = {0};
    int flags = 0;
    FPDFText_GetFontInfo(tp, hit, name, sizeof(name), &flags);
    std::string fn = name;
    int weight = FPDFText_GetFontWeight(tp, hit);
    out.bold = weight >= 600 || Contains(fn, "bold") || Contains(fn, "black") || Contains(fn, "heavy") || (flags & 0x40000);
    out.italic = Contains(fn, "italic") || Contains(fn, "oblique") || (flags & 0x40);
    if ((flags & 1) || Contains(fn, "courier") || Contains(fn, "mono") || Contains(fn, "consol")) out.family = kMono;
    else if (Contains(fn, "times") || Contains(fn, "roman") || Contains(fn, "garamond") || Contains(fn, "georgia") ||
             Contains(fn, "cambria") || Contains(fn, "book") || (Contains(fn, "serif") && !Contains(fn, "sans")) ||
             ((flags & 2) && !Contains(fn, "sans") && !Contains(fn, "arial") && !Contains(fn, "helvet") && !Contains(fn, "calibri")))
        out.family = kSerif;
    FPDF_PAGEOBJECT obj = FPDFText_GetTextObject(tp, hit);
    out.invisible = obj && FPDFTextObj_GetTextRenderMode(obj) == FPDF_TEXTRENDERMODE_INVISIBLE;
    return true;
}

bool LineAt(FPDF_PAGE page, double x, double y, double tol, Line& out) {
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) return false;
    bool ok = LineAtTP(tp, x, y, tol, out);
    FPDFText_ClosePage(tp);
    return ok;
}

bool AddText(FPDF_DOCUMENT doc, FPDF_PAGE page, const std::u16string& text, double x, double y, double size,
             int family, bool bold, bool italic, uint8_t r, uint8_t g, uint8_t b, int rotation) {
    int q = ((rotation % 4) + 4) % 4;
    double angle = q * kPi / 2;
    double c = std::cos(angle), s = std::sin(angle);
    bool any = false;
    auto lines = SplitLines(text);
    for (size_t k = 0; k < lines.size(); k++) {
        if (lines[k].empty()) continue;
        FPDF_PAGEOBJECT obj = NewStandardText(doc, lines[k], size, family, bold, italic);
        if (!obj) continue;
        FPDFPageObj_SetFillColor(obj, r, g, b, 255);
        double down = k * size * 1.2;  // next line is "down" in the text's own direction
        Place(obj, x + down * s, y - down * c, angle, 1);
        FPDFPage_InsertObject(page, obj);
        any = true;
    }
    return any && FPDFPage_GenerateContent(page);
}

namespace {

int ObjectIndex(FPDF_PAGE page, FPDF_PAGEOBJECT o) {
    int n = FPDFPage_CountObjects(page);
    for (int i = 0; i < n; i++)
        if (FPDFPage_GetObject(page, i) == o) return i;
    return -1;  // not a top-level object (inside a form)
}

// A text object in `font` with the look of `like`, whose first character sits at (x, y).
FPDF_PAGEOBJECT CloneStyle(FPDF_DOCUMENT doc, FPDF_PAGEOBJECT like, FPDF_FONT font, const std::u16string& s, double x,
                           double y) {
    float size = 0;
    if (!FPDFTextObj_GetFontSize(like, &size) || size <= 0) return nullptr;
    FPDF_PAGEOBJECT o = FPDFPageObj_CreateTextObj(doc, font, size);
    if (!o) return nullptr;
    std::u16string z = s;
    z.push_back(0);
    if (!FPDFText_SetText(o, (FPDF_WIDESTRING)z.c_str())) {
        FPDFPageObj_Destroy(o);
        return nullptr;
    }
    FS_MATRIX m;
    if (FPDFPageObj_GetMatrix(like, &m)) {
        m.e = (float)x;
        m.f = (float)y;
        FPDFPageObj_SetMatrix(o, &m);
    }
    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(like, &r, &g, &b, &a)) FPDFPageObj_SetFillColor(o, r, g, b, a);
    if (FPDFPageObj_GetStrokeColor(like, &r, &g, &b, &a)) FPDFPageObj_SetStrokeColor(o, r, g, b, a);
    FPDFTextObj_SetTextRenderMode(o, FPDFTextObj_GetTextRenderMode(like));
    return o;
}

// True if each object, placed on the page, reads back as the text it was given.
bool ReadsBack(FPDF_PAGE page, const std::vector<std::pair<FPDF_PAGEOBJECT, std::u16string>>& objs) {
    if (objs.empty()) return true;
    for (auto& o : objs) FPDFPage_InsertObject(page, o.first);
    FPDF_TEXTPAGE chk = FPDFText_LoadPage(page);
    bool ok = chk != nullptr;
    for (auto& o : objs)
        if (ok && GetObjText(o.first, chk) != o.second) {
            ok = false;
#ifdef TEXTEDIT_DEBUG
            std::u16string g = GetObjText(o.first, chk);
            fprintf(stderr, "readback mismatch: got '%s' want '%s'\n", std::string(g.begin(), g.end()).c_str(),
                    std::string(o.second.begin(), o.second.end()).c_str());
#endif
        }
    if (chk) FPDFText_ClosePage(chk);
    for (auto& o : objs) FPDFPage_RemoveObject(page, o.first);
    return ok;
}

}  // namespace

namespace {

// Splits text into lines no wider than `width` (measured by `measure`); \n forces a break.
std::vector<std::u16string> Wrap(const std::u16string& text, double width, const std::function<double(const std::u16string&)>& measure) {
    std::vector<std::u16string> out;
    for (auto& para : SplitLines(text)) {
        std::vector<std::u16string> words;
        std::u16string w;
        for (char16_t c : para) {
            if (c == u' ') { if (!w.empty()) words.push_back(w); w.clear(); }
            else w.push_back(c);
        }
        if (!w.empty()) words.push_back(w);
        if (words.empty()) { out.emplace_back(); continue; }
        std::u16string line;
        for (auto& word : words) {
            std::u16string tryLine = line.empty() ? word : line + u" " + word;
            if (!line.empty() && measure(tryLine) > width) {
                out.push_back(line);
                line = word;
            } else {
                line = tryLine;
            }
        }
        out.push_back(line);
    }
    return out;
}

Result ReplaceRuns(FPDF_DOCUMENT doc, FPDF_PAGE page, const std::vector<Line>& runs, const std::u16string& text, bool wrap,
                   double wrapWidth, double leading, int* linesUsed) {
    if (runs.empty()) return kFailed;
    const Line& line = runs[0];
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) return kFailed;
    int n = FPDFText_CountChars(tp);
    for (auto& r : runs)
        if (r.first < 0 || r.first + r.count > n) {
            FPDFText_ClosePage(tp);
            return kFailed;
        }
    auto inRun = [&](int i) {
        for (auto& r : runs) if (i >= r.first && i < r.first + r.count) return true;
        return false;
    };
    bool invisible = false;
    for (auto& r : runs) invisible = invisible || r.invisible;
    // The text objects the runs touch, in page order.
    std::vector<FPDF_PAGEOBJECT> objs;
    for (auto& r : runs)
        for (int i = r.first; i < r.first + r.count; i++) {
            FPDF_PAGEOBJECT o = FPDFText_GetTextObject(tp, i);
            if (o && std::find(objs.begin(), objs.end(), o) == objs.end()) objs.push_back(o);
        }
    bool clean = !invisible && !objs.empty();
    for (FPDF_PAGEOBJECT o : objs)
        if (ObjectIndex(page, o) < 0) clean = false;

    // Text outside the runs that shares an object with them is rebuilt, word by word, at its exact position.
    struct Piece { FPDF_PAGEOBJECT obj; std::u16string text; FPDF_PAGEOBJECT from; };
    std::vector<Piece> pieces;
    std::vector<std::pair<FPDF_PAGEOBJECT, std::u16string>> check;
    if (clean) {
        for (FPDF_PAGEOBJECT o : objs) {
            FPDF_FONT font = FPDFTextObj_GetFont(o);
            std::u16string word;
            double wx = 0, wy = 0;
            auto flush = [&]() {
                if (word.empty()) return;
                FPDF_PAGEOBJECT p = font ? CloneStyle(doc, o, font, word, wx, wy) : nullptr;
                if (!p) clean = false;
                else {
                    pieces.push_back({p, word, o});
                    check.push_back({p, word});
                }
                word.clear();
            };
            for (int i = 0; i < n && clean; i++) {
                if (FPDFText_GetTextObject(tp, i) != o) continue;
                if (FPDFText_IsGenerated(tp, i)) { flush(); continue; }  // a generated space or line break separates words
                unsigned int c = FPDFText_GetUnicode(tp, i);
                if (inRun(i) || c == ' ' || c > 0xFFFF) { flush(); continue; }
                if (word.empty()) FPDFText_GetCharOrigin(tp, i, &wx, &wy);
                word.push_back((char16_t)c);
            }
            flush();
        }
    }

    // The font for the new text: the original when it has every character, otherwise a close standard font.
    FPDF_FONT font = clean ? FPDFTextObj_GetFont(objs[0]) : nullptr;
    if (font) {
        std::set<unsigned int> have;
        for (int i = 0; i < n; i++) {
            FPDF_PAGEOBJECT o = FPDFText_GetTextObject(tp, i);
            if (o && FPDFTextObj_GetFont(o) == font) have.insert(FPDFText_GetUnicode(tp, i));
        }
        for (char16_t ch : text)
            if (ch != u' ' && ch != u'\n' && ch != u'\r' && !have.count(ch)) { font = nullptr; break; }
    }
    if (font) {
        std::u16string probeText;
        for (char16_t ch : text) if (ch != u'\n' && ch != u'\r') probeText.push_back(ch);
        FPDF_PAGEOBJECT probe = probeText.empty() ? nullptr : CloneStyle(doc, objs[0], font, probeText, 0, 0);
        if (!probeText.empty() && (!probe || !ReadsBack(page, {{probe, probeText}}))) font = nullptr;
        if (probe) FPDFPageObj_Destroy(probe);
    }
    // Stand-in font: match the width of the original lettering by comparing the old text set in it.
    double hscale = 1;
    if (!font) {
        std::u16string old;
        double want = 0;
        for (auto& r : runs) {
            old += r.text;
            want += std::fabs((r.right - r.left) * std::cos(r.angle)) + std::fabs((r.top - r.bottom) * std::sin(r.angle));
        }
        FPDF_PAGEOBJECT probe = NewStandardText(doc, old, line.size, line.family, line.bold, line.italic);
        if (probe) {
            double got = ObjWidth(probe);
            if (got > 1 && want > 1) hscale = std::clamp(want / got, 0.8, 1.15);
            FPDFPageObj_Destroy(probe);
        }
    }
    auto make = [&](const std::u16string& s, double x, double y) -> FPDF_PAGEOBJECT {
        if (font) return CloneStyle(doc, objs[0], font, s, x, y);
        FPDF_PAGEOBJECT o = NewStandardText(doc, s, line.size, line.family, line.bold, line.italic);
        if (!o) return nullptr;
        Place(o, x, y, line.angle, hscale);
        FPDFPageObj_SetFillColor(o, line.r, line.g, line.b, 255);
        return o;
    };
    auto measure = [&](const std::u16string& s) -> double {
        FPDF_PAGEOBJECT o = make(s, 0, 0);
        if (!o) return 0;
        float l, b, r, t;
        double w = FPDFPageObj_GetBounds(o, &l, &b, &r, &t) ? std::hypot(r - l, 0.0) : 0;
        if (std::fabs(std::sin(line.angle)) > 0.5) w = t - b;
        FPDFPageObj_Destroy(o);
        return w;
    };

    // Lay out the new text: one line where the run was, or wrapped to the paragraph's width.
    std::vector<std::u16string> lines;
    if (!text.empty()) lines = wrap ? Wrap(text, wrapWidth, measure) : std::vector<std::u16string>{text};
    std::vector<FPDF_PAGEOBJECT> neu;
    double c = std::cos(line.angle), s = std::sin(line.angle);
    for (size_t k = 0; k < lines.size(); k++) {
        if (lines[k].empty()) continue;
        // Later lines start where the paragraph's second line started (keeps a first-line indent).
        double x0 = line.ox, y0 = line.oy, down = 0;
        if (k > 0) {
            if (runs.size() > 1) { x0 = runs[1].ox; y0 = runs[1].oy; down = (k - 1) * leading; }
            else down = k * leading;
        }
        double dx = down * s, dy = -down * c;
        FPDF_PAGEOBJECT o = make(lines[k], x0 + dx, y0 + dy);
        if (!o) {
            for (auto p : neu) FPDFPageObj_Destroy(p);
            for (auto& p : pieces) FPDFPageObj_Destroy(p.obj);
            FPDFText_ClosePage(tp);
            return kFailed;
        }
        neu.push_back(o);
    }
    if (linesUsed) *linesUsed = (int)lines.size();
    if (clean && !ReadsBack(page, check)) clean = false;
    FPDFText_ClosePage(tp);

    Result res = kReplaced;
    if (clean) {
        // Swap each touched object for its rebuilt pieces, keeping the drawing order.
        for (FPDF_PAGEOBJECT o : objs) {
            int at = ObjectIndex(page, o);
            for (auto& p : pieces)
                if (p.from == o) FPDFPage_InsertObjectAtIndex(page, p.obj, at++);
            FPDFPage_RemoveObject(page, o);
            FPDFPageObj_Destroy(o);
        }
    } else {
        for (auto& p : pieces) FPDFPageObj_Destroy(p.obj);
        res = kCovered;
        double pad = 0.8;
        for (auto& r : runs) {
            FPDF_PAGEOBJECT box = FPDFPageObj_CreateNewRect((float)(r.left - pad), (float)(r.bottom - pad),
                                                            (float)(r.right - r.left + 2 * pad), (float)(r.top - r.bottom + 2 * pad));
            if (!box) continue;
            FPDFPageObj_SetFillColor(box, 255, 255, 255, 255);
            FPDFPath_SetDrawMode(box, FPDF_FILLMODE_WINDING, 0);
            FPDFPage_InsertObject(page, box);
        }
        if (invisible)
            for (auto o : neu) FPDFTextObj_SetTextRenderMode(o, FPDF_TEXTRENDERMODE_FILL);
    }
    for (auto o : neu) FPDFPage_InsertObject(page, o);
    if (!FPDFPage_GenerateContent(page)) return kFailed;
    return res;
}

}  // namespace

Result ReplaceLine(FPDF_DOCUMENT doc, FPDF_PAGE page, const Line& line, const std::u16string& text) {
    return ReplaceRuns(doc, page, {line}, text, false, 0, 0, nullptr);
}

static bool ParagraphAtTP(FPDF_TEXTPAGE tp, double x, double y, double tol, Paragraph& out);

bool ParagraphAt(FPDF_PAGE page, double x, double y, double tol, Paragraph& out) {
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) return false;
    bool ok = ParagraphAtTP(tp, x, y, tol, out);
    FPDFText_ClosePage(tp);
    return ok;
}

static bool ParagraphAtTP(FPDF_TEXTPAGE tp, double x, double y, double tol, Paragraph& out) {
    Line hit;
    if (!LineAtTP(tp, x, y, tol, hit)) return false;
    out = Paragraph();
    out.lines.push_back(hit);
    if (std::fabs(std::sin(hit.angle)) > 0.01 || std::cos(hit.angle) < 0) {
        // Only level text is grouped into paragraphs; sideways text is edited line by line.
        out.left = hit.left; out.right = hit.right; out.leading = hit.size * 1.2;
        out.text = hit.text;
        return true;
    }
    // Look for the lines above and below: same size, same left edge (the first line may be indented),
    // and the same spacing between lines.
    auto probe = [&](double py, Line& ln) {
        for (double dx : {0.5, 2.0, 6.0, 15.0}) {
            if (LineAtTP(tp, out.left + dx, py, 1.0, ln)) return true;
        }
        return false;
    };
    out.left = hit.left;
    out.right = hit.right;
    double leading = 0;
    auto similar = [&](const Line& a, const Line& b) {
        return std::fabs(a.size - b.size) < a.size * 0.12 && std::fabs(a.angle - b.angle) < 0.01 && a.bold == b.bold;
    };
    auto findNext = [&](const Line& from, int dir, Line& ln) -> bool {
        // Scan across one to two line heights for the neighbouring line's middle.
        for (double step = from.size * 0.9; step <= from.size * 2.2; step += from.size * 0.15) {
            double py = (from.bottom + from.top) / 2 + dir * step;
            Line cand;
            if (!LineAtTP(tp, from.left + 1, py, 1.0, cand) && !probe(py, cand)) continue;
            if (cand.first == from.first) continue;
            if (!similar(cand, hit)) return false;
            double gap = std::fabs(cand.oy - from.oy);
            if (leading > 0 && std::fabs(gap - leading) > hit.size * 0.25) return false;
            // Left edges line up (a first-line indent of up to 4 em is allowed).
            if (std::fabs(cand.left - out.left) > hit.size * 4) return false;
            ln = cand;
            if (leading == 0) leading = gap;
            return true;
        }
        return false;
    };
    Line cur = hit, ln;
    while (out.lines.size() < 60 && findNext(cur, 1, ln)) {
        // A shorter line above usually ends the previous paragraph.
        if (ln.right < out.right - hit.size * 6 && out.lines.size() >= 1) break;
        out.lines.insert(out.lines.begin(), ln);
        cur = ln;
    }
    cur = hit;
    while (out.lines.size() < 60 && findNext(cur, -1, ln)) {
        out.lines.push_back(ln);
        cur = ln;
        // A short line is the paragraph's last.
        if (ln.right < out.right - hit.size * 6) break;
    }
    out.left = out.right = 0;
    bool first = true;
    for (auto& l : out.lines) {
        if (first) { out.left = l.left; out.right = l.right; first = false; }
        out.left = std::min(out.left, l.left);
        out.right = std::max(out.right, l.right);
    }
    out.leading = leading > 0 ? leading : hit.size * 1.2;
    for (size_t k = 0; k < out.lines.size(); k++) {
        std::u16string t = out.lines[k].text;
        if (k > 0) {
            // Join lines with a space, or mend a word split with a hyphen at the line end.
            if (!out.text.empty() && out.text.back() == u'-' && out.text.size() > 1 && out.text[out.text.size() - 2] != u' ')
                out.text.pop_back();
            else
                out.text.push_back(u' ');
        }
        out.text += t;
    }
    return true;
}

Result ReplaceParagraph(FPDF_DOCUMENT doc, FPDF_PAGE page, const Paragraph& para, const std::u16string& text, int* linesUsed) {
    if (para.lines.empty()) return kFailed;
    double width = para.right - para.lines[0].ox;
    if (para.lines.size() > 1) width = std::max(width, para.right - para.lines[1].ox);
    return ReplaceRuns(doc, page, para.lines, text, true, width + 0.5, para.leading, linesUsed);
}

}  // namespace textedit
