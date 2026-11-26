#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "convert.h"
#include <objbase.h>
#include <oleauto.h>
#include <wincodec.h>
#include <algorithm>
#include <memory>
#include "pdf.h"
#include "pdfops.h"

static std::wstring Ext(const std::wstring& p) {
    size_t dot = p.find_last_of(L'.');
    size_t slash = p.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return L"";
    std::wstring e = p.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}
static std::wstring FileName(const std::wstring& p) {
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? p : p.substr(slash + 1);
}

static bool IsWordExt(const std::wstring& e) {
    return e == L".doc" || e == L".docx" || e == L".docm" || e == L".rtf" || e == L".odt" || e == L".dot" || e == L".dotx";
}
static bool IsTextExt(const std::wstring& e) { return e == L".txt" || e == L".csv" || e == L".log" || e == L".md"; }
static bool IsImageExt(const std::wstring& e) {
    return e == L".png" || e == L".gif" || e == L".jpg" || e == L".jpeg" || e == L".bmp" || e == L".tif" || e == L".tiff";
}

bool IsPdfPath(const std::wstring& p) { return Ext(p) == L".pdf"; }
bool CanConvert(const std::wstring& p) {
    std::wstring e = Ext(p);
    return e == L".pdf" || IsWordExt(e) || IsTextExt(e) || IsImageExt(e);
}

static bool ReadBytes(const std::wstring& p, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 512LL * 1024 * 1024) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = out.empty() || ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr);
    CloseHandle(h);
    return ok && got == out.size();
}

// ---------------------------------------------------------------- images (Windows Imaging Component)

template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

static int InsertImageFile(FPDF_DOCUMENT doc, int at, const std::wstring& path, std::wstring& err) {
    std::wstring e = Ext(path);
    Com<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        err = L"the Windows image decoder is not available";
        return 0;
    }
    Com<IWICBitmapDecoder> dec;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) {
        err = L"the image could not be read";
        return 0;
    }
    UINT frames = 1;
    dec->GetFrameCount(&frames);
    // Multi-page TIFF scans become several pages; animated GIFs use the first frame.
    if (e == L".gif") frames = 1;
    frames = std::min<UINT>(frames, 500);
    int added = 0;
    for (UINT f = 0; f < frames; f++) {
        Com<IWICBitmapFrameDecode> frame;
        if (FAILED(dec->GetFrame(f, &frame))) break;
        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (!w || !h) continue;
        if ((e == L".jpg" || e == L".jpeg") && frames == 1) {
            // Keep JPEG photos as JPEG so the PDF stays small.
            std::vector<uint8_t> bytes;
            if (ReadBytes(path, bytes) && ops::InsertJpeg(doc, at + added, bytes, (int)w, (int)h)) { added++; continue; }
        }
        Com<IWICFormatConverter> conv;
        if (FAILED(wic->CreateFormatConverter(&conv)) ||
            FAILED(conv->Initialize(frame.p, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            err = L"the image format is not supported";
            break;
        }
        std::vector<uint8_t> bgra((size_t)w * h * 4);
        if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)bgra.size(), bgra.data()))) { err = L"the image could not be decoded"; break; }
        // Flatten transparency onto white.
        UINT stride = ((w * 3) + 3) & ~3u;
        std::vector<uint8_t> bgr((size_t)stride * h);
        for (UINT y = 0; y < h; y++) {
            const uint8_t* s = &bgra[(size_t)y * w * 4];
            uint8_t* d = &bgr[(size_t)y * stride];
            for (UINT x = 0; x < w; x++, s += 4, d += 3) {
                unsigned a = s[3], inv = 255 - a;
                d[0] = (uint8_t)std::min(255u, s[0] + inv);
                d[1] = (uint8_t)std::min(255u, s[1] + inv);
                d[2] = (uint8_t)std::min(255u, s[2] + inv);
            }
        }
        if (ops::InsertImage(doc, at + added, bgr.data(), (int)w, (int)h, (int)stride)) added++;
        else { err = L"the image could not be added"; break; }
    }
    return added;
}

// ---------------------------------------------------------------- Word (automation of the installed copy)

static HRESULT Invoke(IDispatch* obj, WORD kind, const wchar_t* name, VARIANT* result, int argc, VARIANT* args) {
    DISPID id;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = obj->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) return hr;
    // COM wants arguments last-to-first.
    std::vector<VARIANT> rev(args, args + argc);
    std::reverse(rev.begin(), rev.end());
    DISPPARAMS dp = {rev.empty() ? nullptr : rev.data(), nullptr, (UINT)argc, 0};
    DISPID named = DISPID_PROPERTYPUT;
    if (kind & DISPATCH_PROPERTYPUT) { dp.rgdispidNamedArgs = &named; dp.cNamedArgs = 1; }
    return obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, kind, &dp, result, nullptr, nullptr);
}

static VARIANT Str(const std::wstring& s) { VARIANT v; VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(s.c_str()); return v; }
static VARIANT Int(int i) { VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = i; return v; }
static VARIANT Bool(bool b) { VARIANT v; VariantInit(&v); v.vt = VT_BOOL; v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE; return v; }

static bool WordToPdf(const std::wstring& src, const std::wstring& out, std::wstring& err) {
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Word.Application", &clsid))) {
        err = L"Microsoft Word is needed to add Word files, and it isn't installed on this computer";
        return false;
    }
    IDispatch* app = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&app)) || !app) {
        err = L"Microsoft Word could not be started";
        return false;
    }
    VARIANT none = Int(0);
    Invoke(app, DISPATCH_PROPERTYPUT, L"DisplayAlerts", nullptr, 1, &none);
    bool ok = false;
    VARIANT docs;
    VariantInit(&docs);
    if (SUCCEEDED(Invoke(app, DISPATCH_PROPERTYGET, L"Documents", &docs, 0, nullptr)) && docs.vt == VT_DISPATCH) {
        // Documents.Open(FileName, ConfirmConversions, ReadOnly, AddToRecentFiles)
        VARIANT a[4] = {Str(src), Bool(false), Bool(true), Bool(false)};
        VARIANT d;
        VariantInit(&d);
        HRESULT hr = Invoke(docs.pdispVal, DISPATCH_METHOD, L"Open", &d, 4, a);
        for (auto& v : a) VariantClear(&v);
        if (SUCCEEDED(hr) && d.vt == VT_DISPATCH) {
            // ExportAsFixedFormat(OutputFileName, ExportFormat = wdExportFormatPDF)
            VARIANT e[2] = {Str(out), Int(17)};
            hr = Invoke(d.pdispVal, DISPATCH_METHOD, L"ExportAsFixedFormat", nullptr, 2, e);
            for (auto& v : e) VariantClear(&v);
            ok = SUCCEEDED(hr);
            if (!ok) err = L"Word could not export it to PDF";
            VARIANT save = Int(0);  // wdDoNotSaveChanges
            Invoke(d.pdispVal, DISPATCH_METHOD, L"Close", nullptr, 1, &save);
        } else {
            err = L"Word could not open it (it may be password protected or damaged)";
        }
        VariantClear(&d);
    }
    VariantClear(&docs);
    VARIANT save = Int(0);
    Invoke(app, DISPATCH_METHOD, L"Quit", nullptr, 1, &save);
    app->Release();
    return ok;
}

// ---------------------------------------------------------------- PDF and text

static int InsertPdfFile(FPDF_DOCUMENT doc, int at, const std::wstring& path, std::wstring& err) {
    PdfDoc src;
    if (!src.Open(path, err)) return 0;
    int n = ops::InsertFrom(doc, src.Handle(), {}, at);
    if (!n) err = L"its pages could not be copied";
    return n;
}

static int InsertTextFile(FPDF_DOCUMENT doc, int at, const std::wstring& path, std::wstring& err) {
    std::vector<uint8_t> bytes;
    if (!ReadBytes(path, bytes)) { err = L"the file could not be read"; return 0; }
    int n = ops::InsertText(doc, at, ops::DecodeText(bytes));
    if (!n) err = L"the text could not be laid out";
    return n;
}

int InsertFiles(FPDF_DOCUMENT doc, int at, const std::vector<std::wstring>& files, std::wstring& errors,
                void (*progress)(const wchar_t*)) {
    int total = 0;
    for (const auto& f : files) {
        std::wstring e = Ext(f), err;
        int n = 0;
        if (e == L".pdf") n = InsertPdfFile(doc, at + total, f, err);
        else if (IsImageExt(e)) n = InsertImageFile(doc, at + total, f, err);
        else if (IsTextExt(e)) n = InsertTextFile(doc, at + total, f, err);
        else if (IsWordExt(e)) {
            if (progress) progress((L"Converting " + FileName(f) + L" with Microsoft Word...").c_str());
            wchar_t dir[MAX_PATH], tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, dir);
            GetTempFileNameW(dir, L"wpp", 0, tmp);
            std::wstring out = std::wstring(tmp) + L".pdf";
            DeleteFileW(tmp);
            if (WordToPdf(f, out, err)) n = InsertPdfFile(doc, at + total, out, err);
            DeleteFileW(out.c_str());
        } else {
            err = L"this file type can't be added (use PDF, Word, text or image files)";
        }
        if (n == 0) errors += FileName(f) + L": " + (err.empty() ? std::wstring(L"nothing was added") : err) + L"\n";
        total += n;
    }
    if (progress) progress(nullptr);
    return total;
}
