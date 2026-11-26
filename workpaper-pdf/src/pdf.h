// Thin wrapper around a PDFium document.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "fpdfview.h"

struct PageSize {
    float w = 612, h = 792;  // points
};

class PdfDoc {
public:
    PdfDoc() = default;
    ~PdfDoc();
    PdfDoc(const PdfDoc&) = delete;
    PdfDoc& operator=(const PdfDoc&) = delete;

    // Opens the file. On failure returns false and sets err.
    bool Open(const std::wstring& path, std::wstring& err);
    void Close();

    int PageCount() const { return (int)sizes.size(); }
    const PageSize& Size(int i) const { return sizes[i]; }

    // Renders the region (rx, ry, rw, rh) of page `index` drawn at full size
    // (fullW x fullH pixels). Returns a 32-bit top-down DIB, or nullptr.
    HBITMAP Render(int index, int fullW, int fullH, int rx, int ry, int rw, int rh) const;

    const std::wstring& Path() const { return path; }
    const std::wstring& Title() const { return title; }

private:
    static int GetBlock(void* param, unsigned long pos, unsigned char* buf, unsigned long size);

    FPDF_DOCUMENT doc = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    FPDF_FILEACCESS access = {};
    std::vector<PageSize> sizes;
    std::wstring path, title;
};

void PdfInit();
void PdfShutdown();
