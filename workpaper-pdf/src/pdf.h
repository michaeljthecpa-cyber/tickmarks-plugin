// Thin wrapper around a PDFium document.
#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
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
    // Opens a document held in memory (an undo snapshot or a freshly saved copy).
    bool OpenMemory(std::shared_ptr<std::vector<uint8_t>> bytes, const std::wstring& path, const std::wstring& title);
    // Starts an empty document with no pages; the caller adds pages.
    bool NewEmpty(const std::wstring& title);
    void Close();
    // Re-reads page sizes after pages were added, removed, moved or rotated.
    void RefreshSizes();
    // Writes the document to `target` (safely, through a temporary file) and keeps working on it.
    bool SaveTo(const std::wstring& target, std::wstring& err);

    FPDF_DOCUMENT Handle() const { return doc; }
    bool Untitled() const { return path.empty(); }

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
    std::shared_ptr<std::vector<uint8_t>> memory;
};

void PdfInit();
void PdfShutdown();
