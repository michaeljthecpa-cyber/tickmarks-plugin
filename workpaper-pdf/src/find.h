// Search and document outline. Portable C++ on top of PDFium.
#pragma once
#include <string>
#include <vector>
#include "fpdfview.h"

namespace find {

struct Box { double left, bottom, right, top; };
struct Match {
    int page;
    std::vector<Box> boxes;  // page space, one per line the match covers
    std::vector<Box> view;   // the same boxes as fractions of the page as shown (0..1, top-left origin)
};

// Finds `query` on one page. Case is ignored, runs of spaces and line breaks count as one space, and when the
// query is an amount (digits with . , $ - ( ) only) thousands separators and $ signs are ignored on both sides,
// so "12345.67" finds "$12,345.67".
std::vector<Match> FindOnPage(FPDF_PAGE page, int index, const std::u16string& query);

// All matches in the document, page by page.
std::vector<Match> FindAll(FPDF_DOCUMENT doc, const std::u16string& query);

struct Bookmark {
    std::u16string title;
    int level;  // 0 = top
    int page;   // -1 if it doesn't point at a page in this file
};
std::vector<Bookmark> Outline(FPDF_DOCUMENT doc);

}  // namespace find
