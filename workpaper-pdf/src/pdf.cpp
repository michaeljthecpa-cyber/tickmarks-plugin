#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "pdf.h"
#include "pdfops.h"
#include "fpdf_edit.h"

void PdfInit() {
    FPDF_LIBRARY_CONFIG cfg = {};
    cfg.version = 2;
    FPDF_InitLibraryWithConfig(&cfg);
}

void PdfShutdown() { FPDF_DestroyLibrary(); }

PdfDoc::~PdfDoc() { Close(); }

void PdfDoc::Close() {
    if (doc) FPDF_CloseDocument(doc);
    doc = nullptr;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    memory.reset();
    sizes.clear();
}

void PdfDoc::RefreshSizes() {
    int n = doc ? FPDF_GetPageCount(doc) : 0;
    sizes.assign(n > 0 ? n : 0, PageSize());
    for (int i = 0; i < n; i++) {
        FS_SIZEF s;
        if (FPDF_GetPageSizeByIndexF(doc, i, &s) && s.width > 1 && s.height > 1) {
            sizes[i].w = s.width;
            sizes[i].h = s.height;
        }
    }
}

bool PdfDoc::OpenMemory(std::shared_ptr<std::vector<uint8_t>> bytes, const std::wstring& p, const std::wstring& t) {
    Close();
    doc = FPDF_LoadMemDocument64(bytes->data(), bytes->size(), nullptr);
    if (!doc) return false;
    memory = bytes;
    path = p;
    title = t;
    RefreshSizes();
    return true;
}

bool PdfDoc::NewEmpty(const std::wstring& t) {
    Close();
    doc = FPDF_CreateNewDocument();
    path.clear();
    title = t;
    return doc != nullptr;
}

bool PdfDoc::SaveTo(const std::wstring& target, std::wstring& err) {
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    if (!ops::SaveToBuffer(doc, *bytes)) {
        err = L"The document could not be prepared for saving.";
        return false;
    }
    std::wstring tmp = target + L".saving";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"Could not write to that folder. Check that you have permission, or choose Save As.";
        return false;
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, bytes->data(), (DWORD)bytes->size(), &wrote, nullptr) && wrote == bytes->size();
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        err = L"Saving failed part way (is the disk full?). The original file was not changed.";
        return false;
    }
    // Let go of the original file before replacing it, then keep editing the saved copy.
    std::wstring keepTitle;
    size_t slash = target.find_last_of(L"\\/");
    keepTitle = slash == std::wstring::npos ? target : target.substr(slash + 1);
    OpenMemory(bytes, target, keepTitle);
    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        err = L"The file is open in another program, so it could not be replaced. Close it there, or choose Save As.";
        return false;
    }
    return true;
}

int PdfDoc::GetBlock(void* param, unsigned long pos, unsigned char* buf, unsigned long size) {
    HANDLE h = (HANDLE)param;
    OVERLAPPED ov = {};
    ov.Offset = pos;
    DWORD got = 0;
    if (!ReadFile(h, buf, size, &got, &ov)) return 0;
    return got == size ? 1 : 0;
}

bool PdfDoc::Open(const std::wstring& p, std::wstring& err) {
    Close();
    path = p;
    size_t slash = p.find_last_of(L"\\/");
    title = slash == std::wstring::npos ? p : p.substr(slash + 1);

    // Share read/write/delete so other programs (and later our own Save) are not blocked.
    file = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        err = L"The file could not be opened. It may have been moved, or you may not have permission to read it.";
        return false;
    }
    LARGE_INTEGER len = {};
    GetFileSizeEx(file, &len);
    if (len.QuadPart > 0xFFFFFFFFLL) {
        err = L"Files larger than 4 GB are not supported yet.";
        Close();
        return false;
    }
    access.m_FileLen = (unsigned long)len.QuadPart;
    access.m_GetBlock = &PdfDoc::GetBlock;
    access.m_Param = file;

    doc = FPDF_LoadCustomDocument(&access, nullptr);
    if (!doc) {
        unsigned long code = FPDF_GetLastError();
        if (code == FPDF_ERR_PASSWORD)
            err = L"This PDF is password protected. Password-protected files are not supported yet.";
        else if (code == FPDF_ERR_FORMAT)
            err = L"This file is not a valid PDF, or it is damaged.";
        else if (code == FPDF_ERR_SECURITY)
            err = L"This PDF uses a security handler that is not supported.";
        else
            err = L"The PDF could not be read.";
        Close();
        return false;
    }
    RefreshSizes();
    if (sizes.empty()) {
        err = L"This PDF has no pages.";
        Close();
        return false;
    }
    return true;
}

HBITMAP PdfDoc::Render(int index, int fullW, int fullH, int rx, int ry, int rw, int rh) const {
    if (!doc || index < 0 || index >= PageCount() || rw <= 0 || rh <= 0) return nullptr;
    FPDF_PAGE page = FPDF_LoadPage(doc, index);
    if (!page) return nullptr;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = rw;
    bi.bmiHeader.biHeight = -rh;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hb) {
        FPDF_ClosePage(page);
        return nullptr;
    }
    FPDF_BITMAP fb = FPDFBitmap_CreateEx(rw, rh, FPDFBitmap_BGRx, bits, rw * 4);
    FPDFBitmap_FillRect(fb, 0, 0, rw, rh, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(fb, page, -rx, -ry, fullW, fullH, 0, FPDF_ANNOT | FPDF_LCD_TEXT);
    FPDFBitmap_Destroy(fb);
    FPDF_ClosePage(page);
    GdiFlush();
    return hb;
}
