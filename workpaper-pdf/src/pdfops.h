// Page-level editing operations on a PDFium document. Portable C++ (no Windows headers)
// so they can be unit-tested on any platform.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "fpdfview.h"

namespace ops {

// Moves the given pages (any order, no duplicates) so they sit together, in ascending
// original order, starting at `dest` in the resulting document.
bool MovePages(FPDF_DOCUMENT doc, std::vector<int> pages, int dest);

// Deletes the given pages. Refuses to delete every page.
bool DeletePages(FPDF_DOCUMENT doc, std::vector<int> pages);

// Rotates each page by quarter turns (positive = clockwise).
bool RotatePages(FPDF_DOCUMENT doc, const std::vector<int>& pages, int quarterTurns);

// Copies `pages` of `src` into `doc` so the first copy lands at index `at`.
// Pass an empty list to copy every page. Returns the number of pages inserted.
int InsertFrom(FPDF_DOCUMENT doc, FPDF_DOCUMENT src, const std::vector<int>& pages, int at);

// Inserts copies of `pages` right after the last of them. Returns the index of the first copy, or -1.
int DuplicatePages(FPDF_DOCUMENT doc, std::vector<int> pages);

// Inserts a blank page the size of page `like` (or Letter) at `at`.
bool InsertBlank(FPDF_DOCUMENT doc, int at, int like);

// Inserts one page holding an image, fitted inside a Letter page with half-inch margins.
// `bgr` is 24-bit BGR pixel data, top row first.
bool InsertImage(FPDF_DOCUMENT doc, int at, const uint8_t* bgr, int w, int h, int stride);

// Inserts one page holding a JPEG file (kept as JPEG, so the PDF stays small).
bool InsertJpeg(FPDF_DOCUMENT doc, int at, const std::vector<uint8_t>& jpeg, int w, int h);

// Lays out plain text on Letter pages in a monospace font. Returns pages inserted.
int InsertText(FPDF_DOCUMENT doc, int at, const std::u16string& text);

// Decodes a text file's bytes (UTF-8, UTF-16 with BOM, or Windows-1252) to UTF-16.
std::u16string DecodeText(const std::vector<uint8_t>& bytes);

// Serialises the whole document.
bool SaveToBuffer(FPDF_DOCUMENT doc, std::vector<uint8_t>& out);

}  // namespace ops
