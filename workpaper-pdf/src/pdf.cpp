#include "pdf.h"

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
    sizes.clear();
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
    int n = FPDF_GetPageCount(doc);
    sizes.resize(n > 0 ? n : 0);
    for (int i = 0; i < n; i++) {
        FS_SIZEF s;
        if (FPDF_GetPageSizeByIndexF(doc, i, &s) && s.width > 1 && s.height > 1) {
            sizes[i].w = s.width;
            sizes[i].h = s.height;
        }
    }
    if (n <= 0) {
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
