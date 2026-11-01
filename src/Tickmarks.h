// Tickmarks plug-in for Adobe Acrobat: shared declarations.
#pragma once

#include "PIHeaders.h"

// What the Tickmarks tool places on the next click.
enum MarkMode { kModeSymbol = 0, kModeNumber, kModeLetter, kModeNote };

enum { kMaxNumber = 40 };

// Pages of icons.pdf, in order.
enum Icon {
    kIconToolbar = 0, kIconToolbarActive, kIconSymbols, kIconNumbers, kIconLetters,
    kIconAuto, kIconColor, kIconSize, kIconCounters, kIconNote, kIconCount
};

struct TickmarkSettings {
    int  nextNumber;     // 1..kMaxNumber
    int  nextLetter;     // 0..25 (A..Z)
    bool autoIncrement;
    bool wrap;           // 40 -> 1 and Z -> A, else stop at the end
    int  color;          // index into the colour table
    int  size;           // 0 Small, 1 Medium, 2 Large
    int  symbol;         // index of the last chosen symbol
};

extern TickmarkSettings gSettings;
void LoadSettings();
void SaveSettings();

// Marks.cpp
int         SymbolCount();
const char *SymbolLabel(int i);
int         ColorCount();
const char *ColorName(int i);
const char *SizeName(int i);

// Places a mark centred on the device point, or sized to fit 'dragged' (page space) when the
// user dragged out a box. Returns true if a mark was added.
bool PlaceMark(AVPageView pageView, AVDevCoord x, AVDevCoord y, MarkMode mode, const ASFixedRect *dragged);
// Adds a sticky note with its pop-up open at the device point.
bool PlaceNote(AVPageView pageView, AVDevCoord x, AVDevCoord y);
