// Text editing, stage 1: add new text to a page, and replace a line (or a table cell) of existing text.
// Portable C++ on top of PDFium.
#pragma once
#include <cstdint>
#include <string>
#include "fpdfview.h"

namespace textedit {

enum Family { kSans = 0, kSerif, kMono };

// A run of text on one line: a whole line, or one cell of a table row (runs split at wide gaps).
struct Line {
    int first = -1, count = 0;                 // character range in the page's text
    double left = 0, bottom = 0, right = 0, top = 0;  // page-space box around the run
    double ox = 0, oy = 0;                     // baseline origin of the first character
    double size = 10;                          // font size in points
    double angle = 0;                          // text direction, radians counter-clockwise
    std::u16string text;
    uint8_t r = 0, g = 0, b = 0;
    int family = kSans;
    bool bold = false, italic = false;
    bool invisible = false;                    // hidden text, such as the OCR layer of a scan
};

// Finds the run of text under page point (x, y). `tol` is the hit slack in points.
bool LineAt(FPDF_PAGE page, double x, double y, double tol, Line& out);

// Name of the standard PDF font for a family and style (e.g. "Helvetica-Bold").
const char* StandardFont(int family, bool bold, bool italic);

// Adds text (lines separated by \n) with the first baseline starting at page point (x, y).
// `rotation` is the page's /Rotate in quarter turns, so the text reads upright on screen.
bool AddText(FPDF_DOCUMENT doc, FPDF_PAGE page, const std::u16string& text, double x, double y, double size,
             int family, bool bold, bool italic, uint8_t r, uint8_t g, uint8_t b, int rotation);

enum Result { kFailed = 0, kReplaced, kCovered };

// Replaces a run found by LineAt with new text. Where the original text can be taken out cleanly it is
// removed and the new text uses the same font when it can (kReplaced); otherwise the old text is covered
// with a white box and retyped on top (kCovered). Empty `text` deletes the run.
// The caller must reload any text page it holds for this page afterwards.
Result ReplaceLine(FPDF_DOCUMENT doc, FPDF_PAGE page, const Line& line, const std::u16string& text);

}  // namespace textedit
