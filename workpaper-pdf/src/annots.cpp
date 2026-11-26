#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "annots.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include "fpdf_annot.h"
#include "fpdf_edit.h"

namespace annots {

static std::u16string U16(const std::string& s) { return std::u16string(s.begin(), s.end()); }

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

std::string NewId(const char* kind) {
    static unsigned serial = 0;
    char nm[80];
    snprintf(nm, sizeof(nm), "wpp-%s-%lx-%u", kind, (unsigned long)time(nullptr), ++serial);
    return nm;
}

static std::string GetNm(FPDF_ANNOTATION a) {
    unsigned short buf[128] = {};
    unsigned long n = FPDFAnnot_GetStringValue(a, "NM", buf, sizeof(buf));
    std::string s;
    for (unsigned long i = 0; i + 1 < n / 2 && buf[i]; i++) s.push_back((char)(buf[i] < 128 ? buf[i] : '?'));
    return s;
}

static std::u16string GetContents(FPDF_ANNOTATION a) {
    unsigned long n = FPDFAnnot_GetStringValue(a, "Contents", nullptr, 0);
    if (n <= 2) return u"";
    std::vector<unsigned short> buf(n / 2 + 1);
    FPDFAnnot_GetStringValue(a, "Contents", buf.data(), (unsigned long)(buf.size() * 2));
    std::u16string s;
    for (size_t i = 0; i < buf.size() && buf[i]; i++) s.push_back((char16_t)buf[i]);
    return s;
}

std::vector<Info> List(FPDF_PAGE page) {
    std::vector<Info> out;
    int n = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < n; i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
        if (!a) continue;
        int st = FPDFAnnot_GetSubtype(a);
        if (st == FPDF_ANNOT_POPUP || st == FPDF_ANNOT_LINK || st == FPDF_ANNOT_WIDGET || st == FPDF_ANNOT_UNKNOWN) {
            FPDFPage_CloseAnnot(a);
            continue;
        }
        if (FPDFAnnot_GetFlags(a) & FPDF_ANNOT_FLAG_HIDDEN) { FPDFPage_CloseAnnot(a); continue; }
        FS_RECTF r;
        if (!FPDFAnnot_GetRect(a, &r)) { FPDFPage_CloseAnnot(a); continue; }
        Info info;
        info.index = i;
        info.subtype = st;
        info.rect = {std::min(r.left, r.right), std::min(r.bottom, r.top), std::max(r.left, r.right), std::max(r.bottom, r.top)};
        info.nm = GetNm(a);
        info.contents = GetContents(a);
        info.movable = st == FPDF_ANNOT_STAMP || st == FPDF_ANNOT_TEXT || st == FPDF_ANNOT_SQUARE || st == FPDF_ANNOT_CIRCLE ||
                       st == FPDF_ANNOT_FREETEXT;
        out.push_back(info);
        FPDFPage_CloseAnnot(a);
    }
    return out;
}

int Find(FPDF_PAGE page, const std::string& nm) {
    if (nm.empty()) return -1;
    int n = FPDFPage_GetAnnotCount(page);
    for (int i = 0; i < n; i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
        if (!a) continue;
        bool match = GetNm(a) == nm;
        FPDFPage_CloseAnnot(a);
        if (match) return i;
    }
    return -1;
}

bool Remove(FPDF_PAGE page, const std::string& nm) {
    int i = Find(page, nm);
    if (i < 0) return false;
    // Remove the note's pop-up too, if it has one.
    FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
    FPDF_ANNOTATION pop = a ? FPDFAnnot_GetLinkedAnnot(a, "Popup") : nullptr;
    int popIndex = pop ? FPDFPage_GetAnnotIndex(page, pop) : -1;
    if (pop) FPDFPage_CloseAnnot(pop);
    if (a) FPDFPage_CloseAnnot(a);
    bool ok = FPDFPage_RemoveAnnot(page, i) != 0;
    if (popIndex >= 0) FPDFPage_RemoveAnnot(page, popIndex > i ? popIndex - 1 : popIndex);
    return ok;
}

bool SetRect(FPDF_PAGE page, const std::string& nm, const Rect& r) {
    int i = Find(page, nm);
    if (i < 0) return false;
    FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
    FS_RECTF fr = {r.left, r.top, r.right, r.bottom};
    bool ok = FPDFAnnot_SetRect(a, &fr) != 0;
    std::string now = PdfDate();
    FPDFAnnot_SetStringValue(a, "M", (FPDF_WIDESTRING)U16(now).c_str());
    FPDFPage_CloseAnnot(a);
    return ok;
}

bool SetContents(FPDF_PAGE page, const std::string& nm, const std::u16string& text) {
    int i = Find(page, nm);
    if (i < 0) return false;
    FPDF_ANNOTATION a = FPDFPage_GetAnnot(page, i);
    bool ok = FPDFAnnot_SetStringValue(a, "Contents", (FPDF_WIDESTRING)text.c_str()) != 0;
    std::string now = PdfDate();
    FPDFAnnot_SetStringValue(a, "M", (FPDF_WIDESTRING)U16(now).c_str());
    FPDFPage_CloseAnnot(a);
    return ok;
}

static void Stamp(FPDF_ANNOTATION a, const std::u16string& author, const std::string& nm, const char* subj) {
    std::string now = PdfDate();
    FPDFAnnot_SetStringValue(a, "T", (FPDF_WIDESTRING)author.c_str());
    FPDFAnnot_SetStringValue(a, "Subj", (FPDF_WIDESTRING)U16(subj).c_str());
    FPDFAnnot_SetStringValue(a, "M", (FPDF_WIDESTRING)U16(now).c_str());
    FPDFAnnot_SetStringValue(a, "CreationDate", (FPDF_WIDESTRING)U16(now).c_str());
    FPDFAnnot_SetStringValue(a, "NM", (FPDF_WIDESTRING)U16(nm).c_str());
    FPDFAnnot_SetFlags(a, FPDF_ANNOT_FLAG_PRINT);
}

bool AddHighlight(FPDF_PAGE page, const std::vector<Rect>& lines, uint8_t r, uint8_t g, uint8_t b,
                  const std::u16string& author, const std::string& nm) {
    if (lines.empty()) return false;
    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(page, FPDF_ANNOT_HIGHLIGHT);
    if (!a) return false;
    FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color, r, g, b, 255);
    Rect all = lines[0];
    for (const Rect& l : lines) {
        FS_QUADPOINTSF q = {l.left, l.top, l.right, l.top, l.left, l.bottom, l.right, l.bottom};
        FPDFAnnot_AppendAttachmentPoints(a, &q);
        all.left = std::min(all.left, l.left);
        all.bottom = std::min(all.bottom, l.bottom);
        all.right = std::max(all.right, l.right);
        all.top = std::max(all.top, l.top);
    }
    FS_RECTF fr = {all.left, all.top, all.right, all.bottom};
    FPDFAnnot_SetRect(a, &fr);
    Stamp(a, author, nm, "Highlight");
    FPDFPage_CloseAnnot(a);
    return true;
}

bool AddNote(FPDF_PAGE page, float x, float y, const std::u16string& text, uint8_t r, uint8_t g, uint8_t b,
             const std::u16string& author, const std::string& nm) {
    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(page, FPDF_ANNOT_TEXT);
    if (!a) return false;
    FS_RECTF fr = {x, y, x + 20, y - 20};
    FPDFAnnot_SetRect(a, &fr);
    FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color, r, g, b, 255);
    FPDFAnnot_SetStringValue(a, "Contents", (FPDF_WIDESTRING)text.c_str());
    Stamp(a, author, nm, "Note");
    FPDFPage_CloseAnnot(a);
    return true;
}

bool AddShape(FPDF_PAGE page, bool oval, const Rect& rc, uint8_t r, uint8_t g, uint8_t b, float width,
              const std::u16string& author, const std::string& nm) {
    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(page, oval ? FPDF_ANNOT_CIRCLE : FPDF_ANNOT_SQUARE);
    if (!a) return false;
    FS_RECTF fr = {rc.left, rc.top, rc.right, rc.bottom};
    FPDFAnnot_SetRect(a, &fr);
    FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color, r, g, b, 255);
    FPDFAnnot_SetBorder(a, 0, 0, width);
    Stamp(a, author, nm, oval ? "Oval" : "Box");
    FPDFPage_CloseAnnot(a);
    return true;
}

bool AddInk(FPDF_PAGE page, const std::vector<std::vector<FS_POINTF>>& strokes, uint8_t r, uint8_t g, uint8_t b,
            float width, const std::u16string& author, const std::string& nm) {
    if (strokes.empty()) return false;
    FPDF_ANNOTATION a = FPDFPage_CreateAnnot(page, FPDF_ANNOT_INK);
    if (!a) return false;
    float l = 1e9f, bo = 1e9f, ri = -1e9f, t = -1e9f;
    for (const auto& s : strokes) {
        if (s.empty()) continue;
        FPDFAnnot_AddInkStroke(a, s.data(), s.size());
        for (const auto& p : s) {
            l = std::min(l, p.x); ri = std::max(ri, p.x);
            bo = std::min(bo, p.y); t = std::max(t, p.y);
        }
    }
    float pad = width;
    FS_RECTF fr = {l - pad, t + pad, ri + pad, bo - pad};
    FPDFAnnot_SetRect(a, &fr);
    FPDFAnnot_SetColor(a, FPDFANNOT_COLORTYPE_Color, r, g, b, 255);
    FPDFAnnot_SetBorder(a, 0, 0, width);
    Stamp(a, author, nm, "Pen");
    FPDFPage_CloseAnnot(a);
    return true;
}

}  // namespace annots
