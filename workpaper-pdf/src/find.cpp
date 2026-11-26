#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "find.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include "fpdf_doc.h"
#include "fpdf_text.h"

namespace find {

namespace {

bool IsSpace(char16_t c) { return c == u' ' || c == u'\r' || c == u'\n' || c == u'\t' || c == 0xA0 || c == 2 || c == 0xFFFE; }

bool IsAmountQuery(const std::u16string& q) {
    bool digit = false;
    for (char16_t c : q) {
        if (c >= u'0' && c <= u'9') digit = true;
        else if (c != u'.' && c != u',' && c != u'$' && c != u'-' && c != u'(' && c != u')') return false;
    }
    return digit;
}

char16_t Fold(char16_t c) {
    if (c < 0x80) return (char16_t)std::tolower((int)c);
    return (char16_t)std::towlower((wint_t)c);
}

// Normalized text plus, for each normalized character, the index of the page character it came from.
void Normalize(const std::u16string& in, bool amount, std::u16string& out, std::vector<int>* map) {
    out.clear();
    if (map) map->clear();
    bool lastSpace = true;
    for (size_t i = 0; i < in.size(); i++) {
        char16_t c = in[i];
        if (amount && (c == u',' || c == u'$')) continue;
        if (IsSpace(c)) {
            if (lastSpace) continue;
            c = u' ';
            lastSpace = true;
        } else {
            lastSpace = false;
            // Typographic dashes and quotes match their plain forms.
            if (c == 0x2010 || c == 0x2011 || c == 0x2012 || c == 0x2013 || c == 0x2014 || c == 0x2212) c = u'-';
            else if (c == 0x2018 || c == 0x2019) c = u'\'';
            else if (c == 0x201C || c == 0x201D) c = u'"';
            c = Fold(c);
        }
        out.push_back(c);
        if (map) map->push_back((int)i);
    }
    while (!out.empty() && out.back() == u' ') {
        out.pop_back();
        if (map) map->pop_back();
    }
}

}  // namespace

std::vector<Match> FindOnPage(FPDF_PAGE page, int index, const std::u16string& query) {
    std::vector<Match> out;
    bool amount = IsAmountQuery(query);
    std::u16string q;
    Normalize(query, amount, q, nullptr);
    if (q.empty() || !page) return out;
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(page);
    if (!tp) return out;
    int n = FPDFText_CountChars(tp);
    if (n > 0) {
        std::vector<unsigned short> buf(n + 1);
        FPDFText_GetText(tp, 0, n, buf.data());
        std::u16string text(buf.begin(), buf.begin() + n);
        std::u16string norm;
        std::vector<int> map;
        Normalize(text, amount, norm, &map);
        size_t pos = 0;
        while ((pos = norm.find(q, pos)) != std::u16string::npos) {
            // An amount must not be part of a longer number ("100" shouldn't match inside "2,100.50").
            if (amount) {
                auto isNum = [](char16_t c) { return (c >= u'0' && c <= u'9') || c == u'.'; };
                bool before = pos > 0 && isNum(norm[pos - 1]) && isNum(q.front());
                bool after = pos + q.size() < norm.size() && isNum(norm[pos + q.size()]) && isNum(q.back());
                if (before || after) { pos++; continue; }
            }
            int first = map[pos], last = map[pos + q.size() - 1];
            Match m;
            m.page = index;
            int rects = FPDFText_CountRects(tp, first, last - first + 1);
            for (int k = 0; k < rects; k++) {
                double l, t, r, b;
                if (!FPDFText_GetRect(tp, k, &l, &t, &r, &b)) continue;
                m.boxes.push_back({l, b, r, t});
                int x1, y1, x2, y2;
                const int kU = 100000;
                FPDF_PageToDevice(page, 0, 0, kU, kU, 0, l, t, &x1, &y1);
                FPDF_PageToDevice(page, 0, 0, kU, kU, 0, r, b, &x2, &y2);
                m.view.push_back({std::min(x1, x2) / (double)kU, std::min(y1, y2) / (double)kU,
                                  std::max(x1, x2) / (double)kU, std::max(y1, y2) / (double)kU});
            }
            if (!m.boxes.empty()) out.push_back(m);
            pos += q.size();
        }
    }
    FPDFText_ClosePage(tp);
    return out;
}

std::vector<Match> FindAll(FPDF_DOCUMENT doc, const std::u16string& query) {
    std::vector<Match> all;
    int n = FPDF_GetPageCount(doc);
    for (int i = 0; i < n; i++) {
        FPDF_PAGE p = FPDF_LoadPage(doc, i);
        if (!p) continue;
        auto m = FindOnPage(p, i, query);
        all.insert(all.end(), m.begin(), m.end());
        FPDF_ClosePage(p);
    }
    return all;
}

static void Walk(FPDF_DOCUMENT doc, FPDF_BOOKMARK bm, int level, std::vector<Bookmark>& out, std::set<FPDF_BOOKMARK>& seen) {
    for (; bm && out.size() < 5000; bm = FPDFBookmark_GetNextSibling(doc, bm)) {
        if (!seen.insert(bm).second) return;  // a looped outline
        Bookmark b;
        unsigned long len = FPDFBookmark_GetTitle(bm, nullptr, 0);
        if (len > 2) {
            std::vector<unsigned short> buf(len / 2 + 1);
            FPDFBookmark_GetTitle(bm, buf.data(), len);
            for (unsigned short c : buf) { if (!c) break; b.title.push_back((char16_t)c); }
        }
        b.level = level;
        b.page = -1;
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm);
        if (!dest) {
            FPDF_ACTION a = FPDFBookmark_GetAction(bm);
            if (a && FPDFAction_GetType(a) == PDFACTION_GOTO) dest = FPDFAction_GetDest(doc, a);
        }
        if (dest) b.page = FPDFDest_GetDestPageIndex(doc, dest);
        out.push_back(b);
        if (level < 20) Walk(doc, FPDFBookmark_GetFirstChild(doc, bm), level + 1, out, seen);
    }
}

std::vector<Bookmark> Outline(FPDF_DOCUMENT doc) {
    std::vector<Bookmark> out;
    std::set<FPDF_BOOKMARK> seen;
    Walk(doc, FPDFBookmark_GetFirstChild(doc, nullptr), 0, out, seen);
    return out;
}

}  // namespace find
