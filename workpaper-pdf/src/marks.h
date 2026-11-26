// Tickmarks: auditor's marks drawn as standard PDF Stamp annotations with their own appearance,
// so they show and print the same in any PDF reader (Acrobat included). Portable C++.
#pragma once
#include <cstdint>
#include <string>
#include "fpdfview.h"

namespace marks {

enum Kind { kSymbol = 0, kNumber, kLetter, kPrepared, kReviewed };

// Sign-off stamps (kPrepared, kReviewed) show a heading ("PREPARED BY") over `text`,
// usually the person's initials and the date, e.g. "MJ  11/26/2026". Latin-1 text.
const char* SignoffHeading(Kind kind);

int SymbolCount();
const char* SymbolId(int i);
const wchar_t* SymbolLabel(int i);

int ColorCount();
const wchar_t* ColorName(int i);
void ColorRgb(int i, uint8_t& r, uint8_t& g, uint8_t& b);

int SizeCount();
const wchar_t* SizeName(int i);
double SizeScale(int i);

// Text shown for a number (1..) or letter (0 = A) mark.
std::string NumberText(int n);
std::string LetterText(int i);

// Size of the mark in points at scale 1 (Medium). `text` is used for number, letter and sign-off marks.
void BaseSize(Kind kind, int symbol, const std::string& text, double& w, double& h);

// Adds a mark as a Stamp annotation filling the page-space box (left, bottom, right, top).
// `rotation` is the page's /Rotate in quarter turns, so the mark looks upright on screen.
// Returns false if the annotation could not be created.
// `nm` is the annotation's unique name (see annots::NewId), used to find it again for undo.
bool AddMark(FPDF_DOCUMENT doc, FPDF_PAGE page, Kind kind, int symbol, const std::string& text, int color,
             double left, double bottom, double right, double top, int rotation, const std::u16string& author,
             const std::string& nm);

// True if the annotation is one of our tickmarks (sign-off stamps included).
bool IsMark(FPDF_ANNOTATION annot);

}  // namespace marks
