// Comment-style annotations (highlight, sticky note, box, oval, pen) and helpers to find, move
// and remove annotations. Portable C++.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "fpdfview.h"

namespace annots {

// A new unique annotation name, e.g. "wpp-tm-check-1a2b3c-7". Tickmarks use the "tm" kind.
std::string NewId(const char* kind);

struct Rect {
    float left, bottom, right, top;  // page space
};
struct Info {
    int index;
    int subtype;  // FPDF_ANNOT_*
    Rect rect;
    std::string nm;
    std::u16string contents;
    bool movable;  // can be dragged and resized
};

// Lists the page's annotations that a user would see (no pop-ups, links or form fields).
std::vector<Info> List(FPDF_PAGE page);

// Finds an annotation by name; returns its index or -1.
int Find(FPDF_PAGE page, const std::string& nm);
bool Remove(FPDF_PAGE page, const std::string& nm);
bool SetRect(FPDF_PAGE page, const std::string& nm, const Rect& r);
bool SetContents(FPDF_PAGE page, const std::string& nm, const std::u16string& text);

// Text highlight over the given line rectangles (page space).
bool AddHighlight(FPDF_PAGE page, const std::vector<Rect>& lines, uint8_t r, uint8_t g, uint8_t b,
                  const std::u16string& author, const std::string& nm);
// Sticky note with its icon's top-left corner at (x, y).
bool AddNote(FPDF_PAGE page, float x, float y, const std::u16string& text, uint8_t r, uint8_t g, uint8_t b,
             const std::u16string& author, const std::string& nm);
// Box (Square) or oval (Circle) outline.
bool AddShape(FPDF_PAGE page, bool oval, const Rect& rc, uint8_t r, uint8_t g, uint8_t b, float width,
              const std::u16string& author, const std::string& nm);
// Freehand pen strokes.
bool AddInk(FPDF_PAGE page, const std::vector<std::vector<FS_POINTF>>& strokes, uint8_t r, uint8_t g, uint8_t b,
            float width, const std::u16string& author, const std::string& nm);

}  // namespace annots
