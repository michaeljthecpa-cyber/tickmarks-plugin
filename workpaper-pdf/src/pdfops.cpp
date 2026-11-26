#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "pdfops.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "fpdf_edit.h"
#include "fpdf_ppo.h"
#include "fpdf_save.h"

namespace ops {

static const float kLetterW = 612, kLetterH = 792;

static void SortUnique(std::vector<int>& v, int count) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    v.erase(std::remove_if(v.begin(), v.end(), [count](int i) { return i < 0 || i >= count; }), v.end());
}

bool MovePages(FPDF_DOCUMENT doc, std::vector<int> pages, int dest) {
    int n = FPDF_GetPageCount(doc);
    SortUnique(pages, n);
    if (pages.empty()) return false;
    dest = std::clamp(dest, 0, n - (int)pages.size());
    return FPDF_MovePages(doc, pages.data(), (unsigned long)pages.size(), dest) != 0;
}

bool DeletePages(FPDF_DOCUMENT doc, std::vector<int> pages) {
    int n = FPDF_GetPageCount(doc);
    SortUnique(pages, n);
    if (pages.empty() || (int)pages.size() >= n) return false;
    for (auto it = pages.rbegin(); it != pages.rend(); ++it) FPDFPage_Delete(doc, *it);
    return true;
}

bool RotatePages(FPDF_DOCUMENT doc, const std::vector<int>& pages, int quarterTurns) {
    bool any = false;
    for (int i : pages) {
        FPDF_PAGE p = FPDF_LoadPage(doc, i);
        if (!p) continue;
        int r = ((FPDFPage_GetRotation(p) + quarterTurns) % 4 + 4) % 4;
        FPDFPage_SetRotation(p, r);
        FPDF_ClosePage(p);
        any = true;
    }
    return any;
}

int InsertFrom(FPDF_DOCUMENT doc, FPDF_DOCUMENT src, const std::vector<int>& pages, int at) {
    int before = FPDF_GetPageCount(doc);
    at = std::clamp(at, 0, before);
    bool ok = pages.empty() ? FPDF_ImportPagesByIndex(doc, src, nullptr, 0, at)
                            : FPDF_ImportPagesByIndex(doc, src, pages.data(), (unsigned long)pages.size(), at);
    if (!ok) return 0;
    return FPDF_GetPageCount(doc) - before;
}

int DuplicatePages(FPDF_DOCUMENT doc, std::vector<int> pages) {
    int n = FPDF_GetPageCount(doc);
    SortUnique(pages, n);
    if (pages.empty()) return -1;
    // Import from a saved copy: importing a document into itself is not supported.
    std::vector<uint8_t> buf;
    if (!SaveToBuffer(doc, buf)) return -1;
    FPDF_DOCUMENT copy = FPDF_LoadMemDocument64(buf.data(), buf.size(), nullptr);
    if (!copy) return -1;
    int at = pages.back() + 1;
    int added = InsertFrom(doc, copy, pages, at);
    FPDF_CloseDocument(copy);
    return added > 0 ? at : -1;
}

bool InsertBlank(FPDF_DOCUMENT doc, int at, int like) {
    int n = FPDF_GetPageCount(doc);
    double w = kLetterW, h = kLetterH;
    FS_SIZEF s;
    if (like >= 0 && like < n && FPDF_GetPageSizeByIndexF(doc, like, &s)) { w = s.width; h = s.height; }
    FPDF_PAGE p = FPDFPage_New(doc, std::clamp(at, 0, n), w, h);
    if (!p) return false;
    FPDF_ClosePage(p);
    return true;
}

// Picks a page shape for an image and returns the placement rectangle in points.
static void FitImage(int w, int h, double& pw, double& ph, double& x, double& y, double& iw, double& ih) {
    bool landscape = w > h * 1.15;
    pw = landscape ? kLetterH : kLetterW;
    ph = landscape ? kLetterW : kLetterH;
    const double margin = 36;
    double s = std::min((pw - 2 * margin) / w, (ph - 2 * margin) / h);
    s = std::min(s, 2 * 0.75);  // never blow a small image up past twice its on-screen size
    iw = w * s;
    ih = h * s;
    x = (pw - iw) / 2;
    y = ph - margin - ih;  // top of the page
}

bool InsertImage(FPDF_DOCUMENT doc, int at, const uint8_t* bgr, int w, int h, int stride) {
    if (w <= 0 || h <= 0) return false;
    double pw, ph, x, y, iw, ih;
    FitImage(w, h, pw, ph, x, y, iw, ih);
    int n = FPDF_GetPageCount(doc);
    FPDF_PAGE page = FPDFPage_New(doc, std::clamp(at, 0, n), pw, ph);
    if (!page) return false;
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGR, const_cast<uint8_t*>(bgr), stride);
    FPDF_PAGEOBJECT img = FPDFPageObj_NewImageObj(doc);
    bool ok = bmp && img && FPDFImageObj_SetBitmap(&page, 1, img, bmp);
    if (ok) {
        FPDFImageObj_SetMatrix(img, iw, 0, 0, ih, x, y);
        FPDFPage_InsertObject(page, img);
        ok = FPDFPage_GenerateContent(page) != 0;
    } else if (img) {
        FPDFPageObj_Destroy(img);
    }
    if (bmp) FPDFBitmap_Destroy(bmp);
    FPDF_ClosePage(page);
    if (!ok) FPDFPage_Delete(doc, std::clamp(at, 0, n));
    return ok;
}

struct MemReader {
    const std::vector<uint8_t>* data;
    static int Get(void* param, unsigned long pos, unsigned char* buf, unsigned long size) {
        auto* d = (const std::vector<uint8_t>*)param;
        if (pos + (size_t)size > d->size()) return 0;
        memcpy(buf, d->data() + pos, size);
        return 1;
    }
};

bool InsertJpeg(FPDF_DOCUMENT doc, int at, const std::vector<uint8_t>& jpeg, int w, int h) {
    if (w <= 0 || h <= 0 || jpeg.empty()) return false;
    double pw, ph, x, y, iw, ih;
    FitImage(w, h, pw, ph, x, y, iw, ih);
    int n = FPDF_GetPageCount(doc);
    FPDF_PAGE page = FPDFPage_New(doc, std::clamp(at, 0, n), pw, ph);
    if (!page) return false;
    FPDF_FILEACCESS fa = {};
    fa.m_FileLen = (unsigned long)jpeg.size();
    fa.m_GetBlock = &MemReader::Get;
    fa.m_Param = (void*)&jpeg;
    FPDF_PAGEOBJECT img = FPDFPageObj_NewImageObj(doc);
    bool ok = img && FPDFImageObj_LoadJpegFileInline(&page, 1, img, &fa);
    if (ok) {
        FPDFImageObj_SetMatrix(img, iw, 0, 0, ih, x, y);
        FPDFPage_InsertObject(page, img);
        ok = FPDFPage_GenerateContent(page) != 0;
    } else if (img) {
        FPDFPageObj_Destroy(img);
    }
    FPDF_ClosePage(page);
    if (!ok) FPDFPage_Delete(doc, std::clamp(at, 0, n));
    return ok;
}

// Splits text into display lines of at most `cols` characters, wrapping at spaces where possible.
static std::vector<std::u16string> WrapLines(const std::u16string& text, size_t cols) {
    std::vector<std::u16string> out;
    std::u16string line;
    auto flushPara = [&](std::u16string para) {
        while (para.size() > cols) {
            size_t cut = para.rfind(u' ', cols);
            if (cut == std::u16string::npos || cut < cols / 2) cut = cols;
            out.push_back(para.substr(0, cut));
            size_t next = cut;
            while (next < para.size() && para[next] == u' ') next++;
            para = para.substr(next);
        }
        out.push_back(para);
    };
    for (size_t i = 0; i < text.size(); i++) {
        char16_t c = text[i];
        if (c == u'\r') {
            if (i + 1 < text.size() && text[i + 1] == u'\n') i++;
            flushPara(line);
            line.clear();
        } else if (c == u'\n') {
            flushPara(line);
            line.clear();
        } else if (c == u'\t') {
            do line.push_back(u' '); while (line.size() % 4);
        } else if (c == u'\f') {
            flushPara(line);
            line.clear();
            out.push_back(u"\f");
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) flushPara(line);
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

int InsertText(FPDF_DOCUMENT doc, int at, const std::u16string& text) {
    const double margin = 54, size = 10, leading = 12;
    const size_t cols = (size_t)((kLetterW - 2 * margin) / (size * 0.6));
    const int rows = (int)((kLetterH - 2 * margin) / leading);
    std::vector<std::u16string> lines = WrapLines(text, cols);
    if (lines.empty()) lines.push_back(u"");
    FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Courier");
    int n = FPDF_GetPageCount(doc);
    at = std::clamp(at, 0, n);
    int pages = 0;
    size_t li = 0;
    while (li < lines.size()) {
        FPDF_PAGE page = FPDFPage_New(doc, at + pages, kLetterW, kLetterH);
        if (!page) break;
        int row = 0;
        for (; li < lines.size() && row < rows; li++) {
            if (lines[li] == u"\f") { li++; break; }
            if (!lines[li].empty()) {
                FPDF_PAGEOBJECT t = FPDFPageObj_CreateTextObj(doc, font, (float)size);
                std::u16string s = lines[li];
                std::vector<unsigned short> w(s.begin(), s.end());
                w.push_back(0);
                FPDFText_SetText(t, w.data());
                FPDFPageObj_Transform(t, 1, 0, 0, 1, margin, kLetterH - margin - size - row * leading);
                FPDFPage_InsertObject(page, t);
            }
            row++;
        }
        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
        pages++;
    }
    if (font) FPDFFont_Close(font);
    return pages;
}

static const char16_t kCp1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                     0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};

std::u16string DecodeText(const std::vector<uint8_t>& b) {
    std::u16string out;
    size_t n = b.size();
    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        for (size_t i = 2; i + 1 < n; i += 2) out.push_back((char16_t)(b[i] | (b[i + 1] << 8)));
        return out;
    }
    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        for (size_t i = 2; i + 1 < n; i += 2) out.push_back((char16_t)((b[i] << 8) | b[i + 1]));
        return out;
    }
    size_t start = (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) ? 3 : 0;
    // Try UTF-8 first.
    bool valid = true;
    std::u16string u;
    for (size_t i = start; i < n && valid;) {
        uint8_t c = b[i];
        uint32_t cp;
        int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { valid = false; break; }
        if (i + len > n) { valid = false; break; }
        for (int k = 1; k < len; k++) {
            if ((b[i + k] & 0xC0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (b[i + k] & 0x3F);
        }
        if (!valid) break;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            u.push_back((char16_t)(0xD800 + (cp >> 10)));
            u.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
        } else {
            u.push_back((char16_t)cp);
        }
        i += len;
    }
    if (valid) return u;
    for (size_t i = start; i < n; i++) {
        uint8_t c = b[i];
        out.push_back(c >= 0x80 && c < 0xA0 ? kCp1252[c - 0x80] : (char16_t)c);
    }
    return out;
}

struct BufWriter : FPDF_FILEWRITE {
    std::vector<uint8_t>* out;
    static int Write(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
        auto* w = static_cast<BufWriter*>(self);
        const uint8_t* p = (const uint8_t*)data;
        w->out->insert(w->out->end(), p, p + size);
        return 1;
    }
};

bool SaveToBuffer(FPDF_DOCUMENT doc, std::vector<uint8_t>& out) {
    out.clear();
    BufWriter w;
    w.version = 1;
    w.WriteBlock = &BufWriter::Write;
    w.out = &out;
    return FPDF_SaveAsCopy(doc, &w, FPDF_NO_INCREMENTAL) != 0;
}

}  // namespace ops
